#include "String.hpp"

namespace wr {

String::String(Allocator allocator, StringView initial) : m_allocator(allocator)
{
  reset_to_inline();
  append(initial);
}

String::String(const char *cstr) : m_allocator(heap_allocator())
{
  reset_to_inline();
  append(StringView{cstr});
}

String::String(StringView initial) : m_allocator(heap_allocator())
{
  reset_to_inline();
  append(initial);
}

cold String::String(const String &other) : m_allocator(other.m_allocator)
{
  reset_to_inline();
  append(other.view());
}

String::String(String &&other) noexcept : m_allocator(other.m_allocator)
{
  if (other.is_inline()) {
    std::memcpy(m_inline, other.m_inline, other.m_length + 1);
    m_data = m_inline;
    m_length = other.m_length;
    m_capacity = INLINE_CAPACITY;
  } else {
    m_data = other.m_data;
    m_length = other.m_length;
    m_capacity = other.m_capacity;
    other.reset_to_inline();
  }
}

fn String::operator=(const String &other) -> String &
{
  if (this != &other) {
    clear();
    append(other.view());
  }
  return *this;
}

fn String::operator=(String &&other) noexcept -> String &
{
  if (this != &other) {
    free_storage();
    m_allocator = other.m_allocator;
    if (other.is_inline()) {
      std::memcpy(m_inline, other.m_inline, other.m_length + 1);
      m_data = m_inline;
      m_length = other.m_length;
      m_capacity = INLINE_CAPACITY;
    } else {
      m_data = other.m_data;
      m_length = other.m_length;
      m_capacity = other.m_capacity;
      other.reset_to_inline();
    }
  }
  return *this;
}

fn String::clear() noexcept -> void
{
  m_length = 0;
  if (m_data != nullptr) m_data[0] = '\0';
}

hot fn String::push(char c) -> void
{
  reserve(m_length + 1);
  m_data[m_length++] = c;
  m_data[m_length] = '\0';
}

hot fn String::append(StringView other) -> void
{
  if (other.length == 0) return;
  reserve(m_length + other.length);
  std::memcpy(m_data + m_length, other.data, other.length);
  m_length += other.length;
  m_data[m_length] = '\0';
}

cold fn String::reserve(usize needed) -> void
{
  if (needed + 1 <= m_capacity) [[likely]]
    return;
  let new_capacity = m_capacity < 64 ? m_capacity * 4 : m_capacity * 2;
  while (new_capacity < needed + 1)
    new_capacity *= 2;
  let fresh = m_allocator.alloc_array<char>(new_capacity);
  let const preserved_length = m_length;
  if (preserved_length > 0) std::memcpy(fresh, m_data, preserved_length);
  fresh[preserved_length] = '\0';
  free_storage();
  m_data = fresh;
  m_length = preserved_length;
  m_capacity = new_capacity;
}

fn String::pop_back() noexcept -> void
{
  ASSERT(m_length > 0, "pop_back on empty string");
  m_length--;
  m_data[m_length] = '\0';
}

fn String::operator+=(StringView other) -> String &
{
  append(other);
  return *this;
}

fn String::operator+=(char c) -> String &
{
  push(c);
  return *this;
}

hot fn String::operator<(const String &other) const noexcept -> bool
{
  let const shared_length =
      m_length < other.m_length ? m_length : other.m_length;
  let const order = shared_length == 0
                        ? 0
                        : std::memcmp(c_str(), other.c_str(), shared_length);

  if (order != 0) return order < 0;
  return m_length < other.m_length;
}

fn String::find_substring(StringView needle, usize from) const noexcept
    -> Maybe<usize>
{
  if (needle.length == 0) return from <= m_length ? Maybe<usize>{from} : None;
  if (needle.length > m_length) return None;
  let i = from;
  while (i + needle.length <= m_length) {
    let const scan_length = m_length - needle.length - i + 1;
    let const found = std::memchr(
        m_data + i, static_cast<unsigned char>(needle.data[0]), scan_length);
    if (found == nullptr) return None;
    let const candidate =
        static_cast<usize>(static_cast<const char *>(found) - m_data);
    if (std::memcmp(m_data + candidate, needle.data, needle.length) == 0)
      return candidate;
    i = candidate + 1;
  }
  return None;
}

fn String::find_last_character(char wanted) const noexcept -> Maybe<usize>
{
  for (usize i = m_length; i > 0; i--)
    if (m_data[i - 1] == wanted) return i - 1;
  return None;
}

cold fn String::free_storage() noexcept -> void
{
  if (m_data != nullptr && m_data != m_inline) {
    m_allocator.free_array(m_data, m_capacity);
  }
  reset_to_inline();
}

fn operator+(StringView left, StringView right)->String
{
  let result = String{heap_allocator()};
  result.reserve(left.length + right.length);
  result.append(left);
  result.append(right);
  return result;
}

} // namespace wr
