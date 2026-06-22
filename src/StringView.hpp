#pragma once

#include "Common.hpp"
#include "Maybe.hpp"

namespace wr {

class StringView
{
public:
  const char *data{nullptr};
  usize length{0};

  StringView() = default;
  StringView(const char *bytes, usize count) : data(bytes), length(count) {}
  StringView(const char *cstr) noexcept;

  hot mustuse pure fn count() const noexcept -> usize { return length; }
  hot mustuse pure fn is_empty() const noexcept -> bool { return length == 0; }
  hot mustuse pure fn operator[](usize i) const noexcept -> char
  {
    return data[i];
  }

  hot mustuse pure fn operator==(StringView other) const noexcept -> bool;
  hot flatten mustuse pure fn operator!=(StringView other) const noexcept
      -> bool
  {
    return !(*this == other);
  }

  hot mustuse pure fn find_character(char wanted) const noexcept
      -> Maybe<usize>;

  mustuse pure fn substring(usize start) const noexcept -> StringView;

  mustuse pure fn substring_of_length(usize start, usize count) const noexcept
      -> StringView;

  mustuse pure fn starts_with(StringView prefix) const noexcept -> bool;

  mustuse pure fn is_all_decimal_digits() const noexcept -> bool
  {
    if (length == 0) return false;

    for (usize i = 0; i < length; i++) {
      if (data[i] < '0' || data[i] > '9') return false;
    }
    return true;
  }
};

pure forceinline fn hash_bytes(StringView view) noexcept -> u64
{
  u64 hash = 14695981039346656037ull;
  for (usize i = 0; i < view.length; i++) {
    hash ^= static_cast<unsigned char>(view.data[i]);
    hash *= 1099511628211ull;
  }
  return hash;
}

} /* namespace wr */
