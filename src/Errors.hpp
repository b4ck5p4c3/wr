#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace wr {

class ErrorBase
{
public:
  /* A recoverable error is reported and the server keeps running, while a
     critical error means the service cannot continue and is restarted. */
  enum class Severity : u8
  {
    Recoverable,
    Critical,
  };

  ErrorBase();
  ErrorBase(StringView message);
  ErrorBase(Allocator allocator, StringView message);
  virtual ~ErrorBase();

  fn message() const -> String;
  mustuse fn is_critical() const noexcept -> bool;

  virtual fn to_string() const -> String;

protected:
  String m_message;
  Severity m_severity = Severity::Recoverable;
};

class Error : public ErrorBase
{
public:
  Error();
  Error(StringView message);
  Error(Allocator allocator, StringView message);

  fn as_critical() -> Error &;
};

class Warning : public Error
{
public:
  Warning(StringView message);
  Warning(Allocator allocator, StringView message);
};

} /* namespace wr */
