#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace wr {

/* A small JSON writer that builds a document into an owned String. The comma
   and the colon are placed by the writer, so a caller only opens containers and
   writes keys and values. The nesting depth is bounded, which every response in
   this server stays well under. */
class JsonWriter
{
public:
  static constexpr usize MAX_DEPTH = 32;

  explicit JsonWriter(Allocator allocator) : m_out(allocator) {}

  fn object_begin() -> void
  {
    prefix();
    m_out.append("{");
    push();
  }
  fn object_end() -> void
  {
    pop();
    m_out.append("}");
  }
  fn array_begin() -> void
  {
    prefix();
    m_out.append("[");
    push();
  }
  fn array_end() -> void
  {
    pop();
    m_out.append("]");
  }

  fn key(StringView name) -> void
  {
    prefix();
    write_quoted(name);
    m_out.append(":");
    m_after_key = true;
  }

  fn string(StringView value) -> void
  {
    prefix();
    write_quoted(value);
  }
  fn number(i64 value) -> void
  {
    prefix();
    char buffer[32];
    let const length = std::snprintf(buffer, sizeof(buffer), "%lld",
                                     static_cast<long long>(value));
    if (length > 0)
      m_out.append(StringView{buffer, static_cast<usize>(length)});
  }
  fn boolean(bool value) -> void
  {
    prefix();
    m_out.append(value ? "true" : "false");
  }
  fn null() -> void
  {
    prefix();
    m_out.append("null");
  }

  /* A whole key and string value in one call, the common case. */
  fn field(StringView name, StringView value) -> void
  {
    key(name);
    string(value);
  }

  mustuse pure fn view() const noexcept -> StringView { return m_out.view(); }

private:
  fn push() -> void
  {
    if (m_depth < MAX_DEPTH) m_has_member[m_depth] = false;
    m_depth++;
  }
  fn pop() -> void
  {
    if (m_depth > 0) m_depth--;
  }

  /* Place a separating comma before the next element, unless the element is the
     value right after a key or the first member of its container. */
  fn prefix() -> void
  {
    if (m_after_key) {
      m_after_key = false;
      return;
    }
    if (m_depth > 0 && m_depth <= MAX_DEPTH) {
      if (m_has_member[m_depth - 1]) m_out.append(",");
      m_has_member[m_depth - 1] = true;
    }
  }

  fn write_quoted(StringView text) -> void
  {
    m_out.append("\"");
    for (usize i = 0; i < text.count(); i++) {
      let const c = text[i];
      switch (c) {
      case '"': m_out.append("\\\""); break;
      case '\\': m_out.append("\\\\"); break;
      case '\n': m_out.append("\\n"); break;
      case '\r': m_out.append("\\r"); break;
      case '\t': m_out.append("\\t"); break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char escape[8];
          let const length = std::snprintf(
              escape, sizeof(escape), "\\u%04x",
              static_cast<unsigned>(static_cast<unsigned char>(c)));
          if (length > 0)
            m_out.append(StringView{escape, static_cast<usize>(length)});
        } else {
          m_out.push(c);
        }
      }
    }
    m_out.append("\"");
  }

  String m_out;
  bool m_has_member[MAX_DEPTH]{};
  usize m_depth{0};
  bool m_after_key{false};
};

} // namespace wr
