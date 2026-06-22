#pragma once

#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"

namespace wr {

class Ok
{};

inline constexpr Ok Success{};

template <class T>
class mustuse ErrorOr
{
public:
  ErrorOr(T value) : m_is_error(false) { new (&m_storage) T(steal(value)); }
  ErrorOr(Error error) : m_is_error(true)
  {
    new (&m_storage) Error(steal(error));
  }

  ErrorOr(const ErrorOr &other) : m_is_error(other.m_is_error)
  {
    if (m_is_error)
      new (&m_storage) Error(other.error_reference());
    else
      new (&m_storage) T(other.value_reference());
  }
  ErrorOr(ErrorOr &&other) noexcept : m_is_error(other.m_is_error)
  {
    if (m_is_error)
      new (&m_storage) Error(steal(other.error_reference()));
    else
      new (&m_storage) T(steal(other.value_reference()));
  }

  mustuse fn clone() const -> ErrorOr { return ErrorOr{*this}; }

  fn operator=(const ErrorOr &other)->ErrorOr &
  {
    if (this != &other) {
      destroy();
      m_is_error = other.m_is_error;
      if (m_is_error)
        new (&m_storage) Error(other.error_reference());
      else
        new (&m_storage) T(other.value_reference());
    }
    return *this;
  }
  fn operator=(ErrorOr &&other) noexcept -> ErrorOr &
  {
    if (this != &other) {
      destroy();
      m_is_error = other.m_is_error;
      if (m_is_error)
        new (&m_storage) Error(steal(other.error_reference()));
      else
        new (&m_storage) T(steal(other.value_reference()));
    }
    return *this;
  }

  ~ErrorOr() { destroy(); }

  hot mustuse pure fn is_error() const noexcept -> bool { return m_is_error; }

  hot mustuse pure fn value() noexcept -> T &
  {
    ASSERT(!m_is_error);
    return value_reference();
  }
  hot mustuse pure fn value() const noexcept -> const T &
  {
    ASSERT(!m_is_error);
    return value_reference();
  }

  mustuse pure fn error() noexcept -> Error &
  {
    ASSERT(m_is_error);
    return error_reference();
  }
  mustuse pure fn error() const noexcept -> const Error &
  {
    ASSERT(m_is_error);
    return error_reference();
  }

  hot mustuse fn take() -> T
  {
    ASSERT(!m_is_error);
    return steal(value_reference());
  }

private:
  fn destroy() noexcept -> void
  {
    if (m_is_error)
      error_reference().~Error();
    else
      value_reference().~T();
  }

  hot flatten fn value_reference() noexcept -> T &
  {
    return *reinterpret_cast<T *>(&m_storage);
  }
  hot flatten fn value_reference() const noexcept -> const T &
  {
    return *reinterpret_cast<const T *>(&m_storage);
  }
  fn error_reference() noexcept -> Error &
  {
    return *reinterpret_cast<Error *>(&m_storage);
  }
  fn error_reference() const noexcept -> const Error &
  {
    return *reinterpret_cast<const Error *>(&m_storage);
  }

  bool m_is_error;
  alignas(T) alignas(
      Error) unsigned char m_storage[sizeof(T) > sizeof(Error) ? sizeof(T)
                                                               : sizeof(Error)];
};

} // namespace wr

#define TRY(expr)                                                              \
  ({                                                                           \
    auto t__result = (expr);                                                   \
    if (t__result.is_error()) [[unlikely]]                                     \
      return t__result.error();                                                \
    t__result.take();                                                          \
  })
