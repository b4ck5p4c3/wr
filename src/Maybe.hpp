#pragma once

#include "Common.hpp"
#include "Debug.hpp"

namespace wr {

class Nothing
{};

inline constexpr Nothing None{};

template <class T>
class mustuse Maybe
{
public:
  Maybe() noexcept : m_has_value(false) {}
  Maybe(Nothing) noexcept : m_has_value(false) {}
  Maybe(T value) : m_has_value(true) { new (&m_storage) T(steal(value)); }

  Maybe(const Maybe &other) : m_has_value(other.m_has_value)
  {
    if (m_has_value) new (&m_storage) T(other.reference());
  }
  Maybe(Maybe &&other) noexcept : m_has_value(other.m_has_value)
  {
    if (m_has_value) new (&m_storage) T(steal(other.reference()));
  }

  mustuse fn clone() const -> Maybe { return Maybe{*this}; }

  fn operator=(const Maybe &other)->Maybe &
  {
    if (this != &other) {
      reset();
      m_has_value = other.m_has_value;
      if (m_has_value) new (&m_storage) T(other.reference());
    }
    return *this;
  }
  fn operator=(Maybe &&other) noexcept -> Maybe &
  {
    if (this != &other) {
      reset();
      m_has_value = other.m_has_value;
      if (m_has_value) new (&m_storage) T(steal(other.reference()));
    }
    return *this;
  }

  ~Maybe() { reset(); }

  hot mustuse pure fn has_value() const noexcept -> bool { return m_has_value; }
  hot mustuse pure explicit operator bool() const noexcept
  {
    return m_has_value;
  }

  hot mustuse pure fn value() noexcept -> T &
  {
    ASSERT(m_has_value);
    return reference();
  }
  hot mustuse pure fn value() const noexcept -> const T &
  {
    ASSERT(m_has_value);
    return reference();
  }
  hot flatten mustuse pure fn operator*() noexcept -> T & { return value(); }
  hot flatten mustuse pure fn operator*() const noexcept -> const T &
  {
    return value();
  }
  hot flatten mustuse pure fn operator->() noexcept -> T *
  {
    return &reference();
  }
  hot flatten mustuse pure fn operator->() const noexcept -> const T *
  {
    return &reference();
  }

  mustuse fn take() -> T
  {
    ASSERT(m_has_value);
    let taken_value = steal(reference());
    reset();
    return taken_value;
  }

  mustuse fn value_or(T fallback) const -> T
  {
    return m_has_value ? reference() : steal(fallback);
  }

  mustuse fn operator==(const T &other) const->bool
  {
    return m_has_value && reference() == other;
  }
  mustuse fn operator!=(const T &other) const->bool
  {
    return !(*this == other);
  }

  fn reset() noexcept -> void
  {
    if (m_has_value) {
      reference().~T();
      m_has_value = false;
    }
  }

private:
  fn reference() noexcept -> T & { return *reinterpret_cast<T *>(&m_storage); }
  fn reference() const noexcept -> const T &
  {
    return *reinterpret_cast<const T *>(&m_storage);
  }

  bool m_has_value;
  alignas(T) unsigned char m_storage[sizeof(T)];
};

#define UNWRAP(maybe_expr)                                                     \
  ({                                                                           \
    auto t__result = (maybe_expr);                                             \
    if (!t__result) return ::wr::None;                                         \
    t__result.take();                                                          \
  })

} // namespace wr
