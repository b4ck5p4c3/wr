#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Maybe.hpp"
#include "StringView.hpp"

namespace wr {

class String
{
public:
  static constexpr usize INLINE_CAPACITY = 24;

  String() : m_allocator(heap_allocator()) { reset_to_inline(); }
  explicit String(Allocator allocator) : m_allocator(allocator)
  {
    reset_to_inline();
  }
  String(Allocator allocator, StringView initial);
  String(const char *cstr);
  String(StringView initial);

  String(const String &other);
  String(String &&other) noexcept;

  fn operator=(const String &other)->String &;
  fn operator=(String &&other) noexcept -> String &;

  mustuse fn clone() const -> String { return String{*this}; }

  ~String()
  {
    if (m_data != m_inline) free_storage();
  }

  mustuse pure fn allocator() const noexcept -> Allocator
  {
    return m_allocator;
  }

  hot mustuse pure fn count() const noexcept -> usize { return m_length; }
  hot mustuse pure fn is_empty() const noexcept -> bool
  {
    return m_length == 0;
  }
  hot mustuse pure fn operator[](usize i) const noexcept -> char
  {
    return m_data[i];
  }
  hot mustuse pure fn view() const noexcept -> StringView
  {
    return StringView{m_data, m_length};
  }
  operator StringView() const noexcept { return StringView{m_data, m_length}; }
  hot mustuse pure fn c_str() const noexcept -> const char *
  {
    return m_data != nullptr ? m_data : "";
  }

  fn clear() noexcept -> void;

  hot fn push(char c) -> void;
  hot fn append(StringView other) -> void;

  cold fn reserve(usize needed) -> void;

  mustuse pure fn data() const noexcept -> const char * { return c_str(); }
  mustuse pure fn length() const noexcept -> usize { return m_length; }
  hot mustuse pure fn back() const noexcept -> char
  {
    ASSERT(m_length > 0, "back() on an empty string");
    return m_data[m_length - 1];
  }

  fn pop_back() noexcept -> void;

  hot flatten fn append(char c) -> void { push(c); }
  fn operator+=(StringView other)->String &;
  fn operator+=(char c)->String &;

  hot flatten mustuse pure fn find_character(char wanted) const noexcept
      -> Maybe<usize>
  {
    return view().find_character(wanted);
  }
  flatten mustuse pure fn substring(usize start) const noexcept -> StringView
  {
    return view().substring(start);
  }
  flatten mustuse pure fn substring_of_length(usize start,
                                              usize count) const noexcept
      -> StringView
  {
    return view().substring_of_length(start, count);
  }
  flatten mustuse pure fn starts_with(StringView prefix) const noexcept -> bool
  {
    return view().starts_with(prefix);
  }

  hot flatten mustuse pure fn operator==(StringView other) const noexcept
      -> bool
  {
    return view() == other;
  }
  hot flatten mustuse pure fn operator!=(StringView other) const noexcept
      -> bool
  {
    return !(view() == other);
  }

private:
  cold fn free_storage() noexcept -> void;

  mustuse pure fn is_inline() const noexcept -> bool
  {
    return m_data == m_inline;
  }

  fn reset_to_inline() noexcept -> void
  {
    m_data = m_inline;
    m_inline[0] = '\0';
    m_length = 0;
    m_capacity = INLINE_CAPACITY;
  }

  Allocator m_allocator;
  char *m_data{nullptr};
  usize m_length{0};
  usize m_capacity{0};
  char m_inline[INLINE_CAPACITY];
};

fn operator+(StringView left, StringView right)->String;

} // namespace wr
