#pragma once

#include "Common.hpp"
#include "StringView.hpp"

namespace wr {

/* A compile-time string-keyed lookup table. The entries are written as data and
   read by a linear scan, so a dispatch on a leading path or a name is a table
   lookup. The key length is packed at build time, so a lookup does no strlen.
   The value is any literal type, an enum or a small struct. */
template <typename Value, usize ENTRY_COUNT>
class StaticStringMap
{
public:
  struct Entry
  {
    const char *key;
    Value value;
  };

  consteval StaticStringMap(const Entry (&entries)[ENTRY_COUNT])
  {
    for (usize i = 0; i < ENTRY_COUNT; i++) {
      m_keys[i] = key_view(entries[i].key);
      m_values[i] = entries[i].value;
    }
  }

  mustuse pure fn find(StringView key) const noexcept -> const Value *
  {
    for (usize i = 0; i < ENTRY_COUNT; i++)
      if (m_keys[i] == key) return &m_values[i];
    return nullptr;
  }

private:
  static consteval fn key_view(const char *text) -> StringView
  {
    usize length = 0;
    while (text[length] != '\0')
      length++;
    return StringView{text, length};
  }

  StringView m_keys[ENTRY_COUNT];
  Value m_values[ENTRY_COUNT]{};
};

} // namespace wr
