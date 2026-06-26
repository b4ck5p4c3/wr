#pragma once

#include "Common.hpp"
#include "PackedStringKey.hpp"
#include "StringView.hpp"

namespace wr {

/* A compile-time string-keyed lookup table. Each key is packed at build time
   into a PackedStringKey, so a lookup compares a fixed block of words with a
   branchless reduction the way the hash table probes its keys. The length
   filters first, the packed block confirms, and a key longer than the packed
   capacity falls back to a byte compare. The value is any literal type, an enum
   or a small struct. */
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
      usize length = 0;
      while (entries[i].key[length] != '\0')
        length++;

      m_keys[i] = StringView{entries[i].key, length};
      m_packed[i] = pack_key(entries[i].key, length);
      m_values[i] = entries[i].value;
    }
  }

  mustuse pure fn find(StringView key) const noexcept -> const Value *
  {
    let const probe = PackedStringKey::from_view(key);
    for (usize i = 0; i < ENTRY_COUNT; i++) {
      if (m_keys[i].count() != key.count()) continue;
      if (!(m_packed[i] == probe)) continue;

      if (key.count() <= PackedStringKey::BYTE_CAPACITY || m_keys[i] == key) {
        return &m_values[i];
      }
    }
    return nullptr;
  }

private:
  static consteval fn pack_key(const char *text, usize length)
      -> PackedStringKey
  {
    PackedStringKey key{};
    let const count = length < PackedStringKey::BYTE_CAPACITY
                          ? length
                          : PackedStringKey::BYTE_CAPACITY;
    for (usize i = 0; i < count; i++)
      key.words[i / 8] |= static_cast<u64>(static_cast<u8>(text[i]))
                          << (8 * (i % 8));
    return key;
  }

  StringView m_keys[ENTRY_COUNT];
  PackedStringKey m_packed[ENTRY_COUNT]{};
  Value m_values[ENTRY_COUNT]{};
};

} // namespace wr
