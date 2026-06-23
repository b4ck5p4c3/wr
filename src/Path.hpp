#pragma once

#include "Common.hpp"
#include "String.hpp"

namespace wr {

/* A filesystem path handed to a file operation. The path is a NUL-terminated C
   string, so it is built from a String or a literal and borrowed for the call.
   The bytes are not owned, so a Path must not outlive its source. */
class Path
{
public:
  explicit Path(const char *path) noexcept : m_path(path) {}
  explicit Path(const String &path) noexcept : m_path(path.c_str()) {}
  /* A temporary String would leave the borrowed pointer dangling. */
  explicit Path(String &&) = delete;

  mustuse pure fn c_str() const noexcept -> const char * { return m_path; }

private:
  const char *m_path;
};

} // namespace wr
