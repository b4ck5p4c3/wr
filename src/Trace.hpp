#pragma once

#include "Common.hpp"
#include "Containers.hpp"
#include "ErrorOr.hpp"
#include "Path.hpp"

#include <ctime>

namespace wr {

enum class verbosity : u8
{
  Nothing = 0,
  Info,
  Debug,
  All,
};

inline verbosity LOGGER_VERBOSITY = verbosity::Info;

inline std::FILE *LOGGER_OUTPUT = nullptr;

inline fn log_output_stream() noexcept -> std::FILE *
{
  return LOGGER_OUTPUT != nullptr ? LOGGER_OUTPUT : stderr;
}

inline fn set_log_file(Path path) -> ErrorOr<Ok>
{
  std::FILE *opened = std::fopen(path.c_str(), "a");
  if (opened == nullptr) return Error{"Unable to open the log file"};

  if (LOGGER_OUTPUT != nullptr) std::fclose(LOGGER_OUTPUT);

  LOGGER_OUTPUT = opened;
  return Success;
}

constexpr const char *verbosity_to_string(verbosity verbosity)
{
  switch (verbosity) {
  case verbosity::Nothing: return "OFF";
  case verbosity::Info: return "INF";
  case verbosity::Debug: return "DBG";
  case verbosity::All: return "ALL";
  }
  return "???";
}

inline fn log_timestamp(char *buffer, usize length) noexcept -> const char *
{
  std::time_t now = std::time(nullptr);
  std::tm broken{};
  localtime_r(&now, &broken);
  std::strftime(buffer, length, "%H:%M:%S", &broken);
  return buffer;
}

inline fn log_write_header(std::FILE *stream, verbosity level,
                           const char *file_line, const char *func) noexcept
    -> void
{
  char buffer[16];
  unused(std::fprintf(
      stream, "[%s] [%s] %32s %32s(): ", log_timestamp(buffer, sizeof(buffer)),
      verbosity_to_string(level), file_line, func));
}

namespace log_detail {

inline String value_to_log_string(StringView value) { return String{value}; }

inline String value_to_log_string(const char *value)
{
  return value != nullptr ? String{value} : String{"(null)"};
}

inline String value_to_log_string(bool value)
{
  return value ? String{"true"} : String{"false"};
}

inline String value_to_log_string(char value)
{
  String out{};
  out.push(value);
  return out;
}

template <class T>
  requires(__is_integral(T))
String value_to_log_string(T value)
{
  char buffer[32];
  if constexpr (__is_signed(T)) {
    std::snprintf(buffer, sizeof(buffer), "%lld",
                  static_cast<long long>(value));
  } else {
    std::snprintf(buffer, sizeof(buffer), "%llu",
                  static_cast<unsigned long long>(value));
  }
  return String{buffer};
}

template <class T>
  requires(__is_floating_point(T))
String value_to_log_string(T value)
{
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
  return String{buffer};
}

template <class T>
  requires(__is_pointer(T))
String value_to_log_string(T value)
{
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%p", static_cast<const void *>(value));
  return String{buffer};
}

template <class... Args>
String format_named_values(StringView names, Args &&...args)
{
  String out{};
  usize index = 0;
  const usize value_count = sizeof...(Args);

  auto do_append_one = [&](auto &&value) {
    StringView name = names;
    Maybe<usize> comma_position = names.find_character(',');
    if (comma_position.has_value()) {
      name = names.substring_of_length(0, comma_position.value());
      names = names.substring(comma_position.value() + 1);
    } else {
      names = StringView{};
    }
    usize start = 0;
    while (start < name.length &&
           (name.data[start] == ' ' || name.data[start] == '\t'))
      start++;
    usize stop = name.length;
    while (stop > start &&
           (name.data[stop - 1] == ' ' || name.data[stop - 1] == '\t'))
      stop--;
    name = name.substring_of_length(start, stop - start);

    out.append(name);
    out.append(StringView{" = "});
    out.append(value_to_log_string(value).view());
    if (++index < value_count) out.append(StringView{", "});
  };

  (do_append_one(::wr::forward<Args>(args)), ...);
  return out;
}

} /* namespace log_detail */

} /* namespace wr */

#define T__LOG_STRINGIZE2(x) #x
#define T__LOG_STRINGIZE(x)  T__LOG_STRINGIZE2(x)

#define LOG(level, ...)                                                        \
  do {                                                                         \
    constexpr ::wr::verbosity t__log_level = ::wr::verbosity::level;           \
    if (t__log_level <= ::wr::LOGGER_VERBOSITY) [[unlikely]] {                 \
      std::FILE *t__log_stream = ::wr::log_output_stream();                    \
      ::wr::log_write_header(t__log_stream, t__log_level,                      \
                             __FILE__ ":" T__LOG_STRINGIZE(__LINE__),          \
                             __func__);                                        \
      unused(std::fprintf(t__log_stream, __VA_ARGS__));                        \
      unused(std::fputc('\n', t__log_stream));                                 \
      unused(std::fflush(t__log_stream));                                      \
    }                                                                          \
  } while (0)

#define LOG_VARS(level, ...)                                                   \
  do {                                                                         \
    constexpr ::wr::verbosity t__log_level = ::wr::verbosity::level;           \
    if (t__log_level <= ::wr::LOGGER_VERBOSITY) [[unlikely]] {                 \
      ::wr::String t__vars =                                                   \
          ::wr::log_detail::format_named_values(#__VA_ARGS__, __VA_ARGS__);    \
      std::FILE *t__log_stream = ::wr::log_output_stream();                    \
      ::wr::log_write_header(t__log_stream, t__log_level,                      \
                             __FILE__ ":" T__LOG_STRINGIZE(__LINE__),          \
                             __func__);                                        \
      unused(std::fprintf(t__log_stream, "%s\n", t__vars.c_str()));            \
      unused(std::fflush(t__log_stream));                                      \
    }                                                                          \
  } while (0)
