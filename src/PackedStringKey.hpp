#pragma once

#include "Common.hpp"
#include "StringView.hpp"

namespace wr {

/* A short byte string packed into sixty-four bytes over eight words, compared
   with a couple of vector instructions. A longer key keeps its leading bytes
   and falls back to a full byte compare. */
class PackedStringKey
{
public:
  static constexpr usize WORD_COUNT = 8;
  static constexpr usize BYTE_CAPACITY = WORD_COUNT * 8;

  u64 words[WORD_COUNT]{};

  hot static fn from_view(StringView text) noexcept -> PackedStringKey
  {
    PackedStringKey key{};
    let const count =
        text.count() < BYTE_CAPACITY ? text.count() : BYTE_CAPACITY;
    for (usize i = 0; i < count; i++)
      key.words[i / 8] |= static_cast<u64>(static_cast<u8>(text[i]))
                          << (8 * (i % 8));
    return key;
  }

  hot mustuse pure fn operator==(const PackedStringKey &other) const noexcept
      -> bool
  {
    /* The branchless reduction lowers to a vector xor over the whole block. */
    u64 difference = 0;
    for (usize i = 0; i < WORD_COUNT; i++)
      difference |= words[i] ^ other.words[i];
    return difference == 0;
  }
};

} // namespace wr
