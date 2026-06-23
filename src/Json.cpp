#include "Json.hpp"

/* mongoose.h uses fn and noinline as ordinary identifiers, which collide with
   the Common.hpp macros, so they are neutralized across the include. */
#pragma push_macro("fn")
#pragma push_macro("noinline")
#undef fn
#undef noinline
#include "mongoose.h"
#pragma pop_macro("noinline")
#pragma pop_macro("fn")

namespace wr {

namespace {

/* The mongoose JSON path for a top-level object member, "$.<key>". */
fn member_path(Allocator allocator, StringView key) -> String
{
  String path{allocator};
  path.append("$.");
  path.append(key);
  return path;
}

} // namespace

fn json_get_string(Allocator allocator, StringView json, StringView key)
    -> Maybe<String>
{
  let const path = member_path(allocator, key);
  let const document = mg_str_n(json.data, json.count());
  char *unescaped = mg_json_get_str(document, path.c_str());
  if (unescaped == nullptr) return None;
  defer { mg_free(unescaped); };

  return Maybe<String>{
      String{allocator, StringView{unescaped}}
  };
}

fn json_get_number(Allocator allocator, StringView json, StringView key)
    -> Maybe<i64>
{
  let const path = member_path(allocator, key);
  let const document = mg_str_n(json.data, json.count());
  double value = 0;
  if (!mg_json_get_num(document, path.c_str(), &value)) return None;
  return Maybe<i64>{static_cast<i64>(value)};
}

} // namespace wr
