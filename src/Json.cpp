#include "Json.hpp"

/* rapidjson copies trivially through memcpy, which the strict build flags warn
   on, and the project macros are ordinary words it uses as identifiers, so both
   are neutralized across the header includes. Only the SAX reader is pulled in,
   so the DOM value types are never compiled. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnontrivial-memcall"
#pragma push_macro("fn")
#pragma push_macro("let")
#pragma push_macro("loop")
#pragma push_macro("pure")
#pragma push_macro("hot")
#pragma push_macro("cold")
#pragma push_macro("flatten")
#pragma push_macro("noinline")
#pragma push_macro("forceinline")
#undef fn
#undef let
#undef loop
#undef pure
#undef hot
#undef cold
#undef flatten
#undef noinline
#undef forceinline
#include "rapidjson/memorystream.h"
#include "rapidjson/reader.h"
#pragma pop_macro("forceinline")
#pragma pop_macro("noinline")
#pragma pop_macro("flatten")
#pragma pop_macro("cold")
#pragma pop_macro("hot")
#pragma pop_macro("pure")
#pragma pop_macro("loop")
#pragma pop_macro("let")
#pragma pop_macro("fn")
#pragma clang diagnostic pop

namespace wr {

/* The rapidjson SAX handler. It keeps a stack of the open containers and folds
   each finished value into its parent, so a document is built into a Json tree
   in one pass. */
struct JsonBuilder
{
  struct frame
  {
    Json node;
    String key;
  };

  explicit JsonBuilder(Allocator allocator)
      : m_allocator(allocator), m_stack(allocator), m_result(),
        m_has_root(false)
  {}

  fn leaf(Json::kind kind) -> Json
  {
    Json node;
    node.m_kind = kind;
    return node;
  }

  fn value_done(Json value) -> void
  {
    if (m_stack.count() == 0) {
      m_result = steal(value);
      m_has_root = true;
      return;
    }
    frame &top = m_stack[m_stack.count() - 1];
    if (top.node.m_kind == Json::kind::object) {
      top.node.m_keys.push(steal(top.key));
      top.node.m_values.push(steal(value));
      top.key = wr::String{m_allocator};
    } else {
      top.node.m_values.push(steal(value));
    }
  }

  fn start_container(Json::kind kind) -> void
  {
    frame opened{leaf(kind), wr::String{m_allocator}};
    opened.node.m_keys = ArrayList<wr::String>{m_allocator};
    opened.node.m_values = ArrayList<Json>{m_allocator};
    m_stack.push(steal(opened));
  }

  fn end_container() -> void
  {
    frame finished = steal(m_stack[m_stack.count() - 1]);
    m_stack.pop_back();
    value_done(steal(finished.node));
  }

  fn integer(i64 value) -> bool
  {
    Json node = leaf(Json::kind::integer);
    node.m_integer = value;
    value_done(steal(node));
    return true;
  }

  /* The rapidjson handler interface. Each call returns true to continue. */
  fn Null() -> bool
  {
    value_done(leaf(Json::kind::null));
    return true;
  }
  fn Bool(bool value) -> bool
  {
    Json node = leaf(Json::kind::boolean);
    node.m_boolean = value;
    value_done(steal(node));
    return true;
  }
  fn Int(int value) -> bool { return integer(value); }
  fn Uint(unsigned value) -> bool { return integer(static_cast<i64>(value)); }
  fn Int64(int64_t value) -> bool { return integer(static_cast<i64>(value)); }
  fn Uint64(uint64_t value) -> bool
  {
    if (value > static_cast<uint64_t>(INT64_MAX)) {
      value_done(leaf(Json::kind::real));
      return true;
    }
    return integer(static_cast<i64>(value));
  }
  fn Double(double value) -> bool
  {
    unused(value);
    value_done(leaf(Json::kind::real));
    return true;
  }
  fn RawNumber(const char *str, rapidjson::SizeType length, bool copy) -> bool
  {
    unused(str);
    unused(length);
    unused(copy);
    value_done(leaf(Json::kind::real));
    return true;
  }
  fn String(const char *str, rapidjson::SizeType length, bool copy) -> bool
  {
    unused(copy);
    Json node = leaf(Json::kind::string);
    node.m_string = wr::String{
        m_allocator, StringView{str, length}
    };
    value_done(steal(node));
    return true;
  }
  fn StartObject() -> bool
  {
    start_container(Json::kind::object);
    return true;
  }
  fn Key(const char *str, rapidjson::SizeType length, bool copy) -> bool
  {
    unused(copy);
    m_stack[m_stack.count() - 1].key = wr::String{
        m_allocator, StringView{str, length}
    };
    return true;
  }
  fn EndObject(rapidjson::SizeType member_count) -> bool
  {
    unused(member_count);
    end_container();
    return true;
  }
  fn StartArray() -> bool
  {
    start_container(Json::kind::array);
    return true;
  }
  fn EndArray(rapidjson::SizeType element_count) -> bool
  {
    unused(element_count);
    end_container();
    return true;
  }

  Allocator m_allocator;
  ArrayList<frame> m_stack;
  Json m_result;
  bool m_has_root;
};

fn Json::from(Allocator allocator, StringView text) -> Json
{
  JsonBuilder builder{allocator};
  rapidjson::Reader reader;
  rapidjson::MemoryStream stream{text.data, text.count()};

  /* The iterative parser is selected so a deeply nested body cannot overflow
     the stack, since a request body is parsed before any auth. */
  reader.Parse<rapidjson::kParseIterativeFlag>(stream, builder);

  if (reader.HasParseError() || !builder.m_has_root) return Json{};
  return steal(builder.m_result);
}

fn Json::operator[](StringView key) const noexcept -> const Json &
{
  static const Json INVALID;
  if (m_kind != kind::object) return INVALID;
  for (usize i = 0; i < m_keys.count(); i++)
    if (m_keys[i].view() == key) return m_values[i];

  return INVALID;
}

} // namespace wr
