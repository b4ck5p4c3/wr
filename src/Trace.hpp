#pragma once

#include "Common.hpp"
#include "Containers.hpp"
#include "ErrorOr.hpp"
#include "Path.hpp"
#include "Pthread.hpp"

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

inline u32 LOG_THREAD_COUNTER = 0;

inline fn current_thread_id() noexcept -> u32
{
  thread_local u32 thread_id = 0;
  thread_local bool was_assigned = false;
  if (!was_assigned) {
    thread_id = __atomic_fetch_add(&LOG_THREAD_COUNTER, 1, __ATOMIC_RELAXED);
    was_assigned = true;
  }
  return thread_id;
}

inline fn set_log_file(Path path) -> ErrorOr<Ok>
{
  let const opened = std::fopen(path.c_str(), "a");
  if (opened == nullptr) {
    String message{};
    message.append("Unable to open the log file, ");
    message.append(std::strerror(errno));
    return Error{message.view()};
  }

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
  let const now = std::time(nullptr);
  std::tm broken{};
  localtime_r(&now, &broken);
  std::strftime(buffer, length, "%H:%M:%S", &broken);
  return buffer;
}

constexpr usize LOG_RING_CAPACITY = 256;

inline PthreadMutex LOG_RING_MUTEX{};
inline String LOG_RING_LINES[LOG_RING_CAPACITY]{};
inline usize LOG_RING_HEAD = 0;
inline usize LOG_RING_COUNT = 0;

// A ring of recent lines backs the admin log panel when no log file is on disk.
inline fn log_ring_push(StringView header, StringView body) noexcept -> void
{
  ScopedLock guard{LOG_RING_MUTEX};
  let &slot = LOG_RING_LINES[LOG_RING_HEAD];
  slot.clear();
  slot.append(header);
  slot.append(body);
  LOG_RING_HEAD = (LOG_RING_HEAD + 1) % LOG_RING_CAPACITY;
  if (LOG_RING_COUNT < LOG_RING_CAPACITY) LOG_RING_COUNT++;
}

inline fn log_ring_snapshot(Allocator allocator) -> ArrayList<String>
{
  ArrayList<String> lines{allocator};

  ScopedLock guard{LOG_RING_MUTEX};
  let const start =
      (LOG_RING_HEAD + LOG_RING_CAPACITY - LOG_RING_COUNT) % LOG_RING_CAPACITY;
  for (usize i = 0; i < LOG_RING_COUNT; i++) {
    let const index = (start + i) % LOG_RING_CAPACITY;
    lines.push(LOG_RING_LINES[index].clone());
  }

  return lines;
}

inline fn log_emit(verbosity level, const char *file_line, const char *func,
                   StringView body) noexcept -> void
{
  char timestamp_buffer[16];
  char header[256];
  let const header_length = std::snprintf(
      header, sizeof(header), "[%s] [%s] [t%u] %32s %32s(): ",
      log_timestamp(timestamp_buffer, sizeof(timestamp_buffer)),
      verbosity_to_string(level), current_thread_id(), file_line, func);
  let const header_view = header_length > 0 ? StringView{header} : StringView{};

  let const stream = log_output_stream();
  unused(std::fwrite(header_view.data, 1, header_view.length, stream));
  unused(std::fwrite(body.data, 1, body.length, stream));
  unused(std::fputc('\n', stream));
  unused(std::fflush(stream));

  log_ring_push(header_view, body);
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
      char t__log_body[1024];                                                  \
      unused(std::snprintf(t__log_body, sizeof(t__log_body), __VA_ARGS__));    \
      ::wr::log_emit(t__log_level, __FILE__ ":" T__LOG_STRINGIZE(__LINE__),    \
                     __func__, ::wr::StringView{t__log_body});                 \
    }                                                                          \
  } while (0)

#define LOG_VARS(level, ...)                                                   \
  do {                                                                         \
    constexpr ::wr::verbosity t__log_level = ::wr::verbosity::level;           \
    if (t__log_level <= ::wr::LOGGER_VERBOSITY) [[unlikely]] {                 \
      ::wr::String t__vars =                                                   \
          ::wr::log_detail::format_named_values(#__VA_ARGS__, __VA_ARGS__);    \
      ::wr::log_emit(t__log_level, __FILE__ ":" T__LOG_STRINGIZE(__LINE__),    \
                     __func__, t__vars.view());                                \
    }                                                                          \
  } while (0)
