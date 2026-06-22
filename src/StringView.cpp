#include "StringView.hpp"

namespace wr {

StringView::StringView(const char *cstr) noexcept
    : data(cstr), length(cstr != nullptr ? std::strlen(cstr) : 0)
{}

fn StringView::operator==(StringView other) const noexcept -> bool
{
  return length == other.length &&
         (length == 0 || std::memcmp(data, other.data, length) == 0);
}

fn StringView::find_character(char wanted) const noexcept -> Maybe<usize>
{
  if (length == 0) return None;
  let const found =
      std::memchr(data, static_cast<unsigned char>(wanted), length);
  if (found == nullptr) return None;

  return static_cast<usize>(static_cast<const char *>(found) - data);
}

fn StringView::substring(usize start) const noexcept -> StringView
{
  if (start >= length) return StringView{data + length, 0};
  return StringView{data + start, length - start};
}

fn StringView::substring_of_length(usize start, usize count) const noexcept
    -> StringView
{
  if (start >= length) return StringView{data + length, 0};
  usize remaining = length - start;

  return StringView{data + start, count < remaining ? count : remaining};
}

fn StringView::starts_with(StringView prefix) const noexcept -> bool
{
  if (prefix.length > length) return false;
  return prefix.length == 0 ||
         std::memcmp(data, prefix.data, prefix.length) == 0;
}

} // namespace wr
