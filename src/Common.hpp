#pragma once

#if !defined(__linux__) && !defined(__COSMOPOLITAN__)
#error "Temple OS is not supported. Install Linux!"
#endif

#if !defined WR_ENVCXXFLAGS
#warning WR_ENVCXXFLAGS should be defined. Please use provided makefile \
         for compilation!
#define WR_ENVCXXFLAGS "<unknown>"
#endif

#if !defined WR_COMPILER_COMMAND
#warning WR_COMPILER_COMMAND should be defined. Please use provided makefile \
         for compilation!
#define WR_COMPILER_COMMAND "<unknown>"
#endif

#if !defined WR_COMMIT_HASH
#warning WR_COMMIT_HASH should be defined. Please use provided makefile for \
         compilation!
#define WR_COMMIT_HASH "<unknown>"
#endif

#if !defined WR_BUILD_MODE
#warning WR_BUILD_MODE should be defined. Please use provided makefile for \
         compilation!
#define WR_BUILD_MODE "<unset>"
#endif

#if !defined WR_OS_INFO
#warning WR_OS_INFO should be defined. Please use provided makefile for \
         compilation!
#define WR_OS_INFO "<unset>"
#endif

#if !defined WR_LIBC
#warning WR_LIBC should be defined. Please use provided makefile for \
         compilation!
#define WR_LIBC "<unknown libc>"
#endif

#define WR_BUILD_DATE (__DATE__ " at " __TIME__)

#define WR_COMPILER WR_COMPILER_COMMAND " (" __VERSION__ ", " WR_LIBC ")"

#define WR_VER_MAJOR 0
#define WR_VER_MINOR 0
#define WR_VER_PATCH 1

#define WR_API_VERSION 1

#define WR_VER_EXTRA "dev"

#define WR_STRINGIFY_INNER(x) #x
#define WR_STRINGIFY(x)       WR_STRINGIFY_INNER(x)
#define WR_VERSION_STRING                                                      \
  WR_STRINGIFY(WR_VER_MAJOR)                                                   \
  "." WR_STRINGIFY(WR_VER_MINOR) "." WR_STRINGIFY(WR_VER_PATCH) "-" WR_VER_EXTRA

#define WR_SHORT_LICENSE                                                       \
  "Licensed under the 3-Clause BSD License.\n"                                 \
  "There is NO WARRANTY, to the extent permitted by law."

/* clang-format off */
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <clocale>
#include <cstring>

#include <initializer_list>
#include <new>
/* clang-format on */

using opaque = void;

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

using uchar = u8;
using ichar = i8;

using usize = size_t;
using uintptr = uintptr_t;

#if defined __GNUC__ || defined __clang__
#define T__HAS_GCC_EXTENSIONS 1
#define t__used               __attribute__((used))
#define t__pure               __attribute__((pure))
#define t__forceinline        inline __attribute__((always_inline))
#define t__unreachable()      __builtin_unreachable()
#define t__debugtrap()        __builtin_trap()
#else /* __GNUC__ || __clang__ */
#error Oh no! Segmentation fault. Please download a better compiler that \
       supports GNU extensions!
#define T__HAS_GCC_EXTENSIONS 0
#define t__used               /* None */
#define t__pure               /* None */
#define t__forceinline        /* None */
#define t__unreachable()      abort()
#define t__debugtrap()        abort()
#endif
#define t__concat_literal(x, y) x##y
#define concat_literal(x, y)    t__concat_literal(x, y)

template <typename T>
class t__exit_scope
{
public:
  t__exit_scope(T lambda) : m_lambda(lambda) {}
  ~t__exit_scope() { m_lambda(); }
  t__exit_scope(const t__exit_scope &);

private:
  T m_lambda;
  t__exit_scope &operator=(const t__exit_scope &);
};

class t__exit_scope_help
{
public:
  template <typename T>
  t__exit_scope<T> operator+(T t)
  {
    return t;
  }
};

#define defer                                                                  \
  const auto &concat_literal(defer__, __LINE__) =                              \
      t__exit_scope_help() + [&]() -> void

#define ENUM(e) static_cast<int>(e)

#define sub_sat(a, b) ((a) > (b) ? (a) - (b) : 0)

#define unused(x) ((void) (x))

#define countof(arr) (sizeof(arr) / sizeof(*(arr)))
#define steal        ::wr::move
#define mustuse      [[nodiscard]]

#define fn   auto
#define let  auto
#define loop for (;;)

#define donteliminate t__used
#define forceinline   t__forceinline

#if T__HAS_GCC_EXTENSIONS
#define pure t__pure
#define cold [[gnu::cold]]
#define hot  [[gnu::hot]]
#if defined __clang__
#define flatten [[gnu::flatten]]
#else
#define flatten /* nothing. GNU is too harsh with inlining. */
#endif          /* __clang__ */
#define noinline [[gnu::noinline]]
#else
#define pure
#define cold
#define hot
#define flatten
#define noinline
#endif /* T__HAS_GCC_EXTENSIONS */

namespace wr {

template <class T>
struct remove_reference
{
  using type = T;
};
template <class T>
struct remove_reference<T &>
{
  using type = T;
};
template <class T>
struct remove_reference<T &&>
{
  using type = T;
};
template <class T>
using remove_reference_t = typename remove_reference<T>::type;

template <class T>
forceinline constexpr fn move(T &&value) noexcept -> remove_reference_t<T> &&
{
  return static_cast<remove_reference_t<T> &&>(value);
}

template <class T>
forceinline constexpr fn forward(remove_reference_t<T> &value) noexcept -> T &&
{
  return static_cast<T &&>(value);
}

template <class T>
forceinline constexpr fn forward(remove_reference_t<T> &&value) noexcept -> T &&
{
  return static_cast<T &&>(value);
}

} // namespace wr
