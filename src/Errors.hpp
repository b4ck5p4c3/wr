#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace wr {

class ErrorBase
{
public:
  ErrorBase();
  ErrorBase(StringView message);
  ErrorBase(Allocator allocator, StringView message);
  virtual ~ErrorBase();

  fn message() const -> String;

  virtual fn to_string() const -> String;

protected:
  String m_message;
};

class Error : public ErrorBase
{
public:
  Error();
  Error(StringView message);
  Error(Allocator allocator, StringView message);
};

class Warning : public Error
{
public:
  Warning(StringView message);
  Warning(Allocator allocator, StringView message);
};

} /* namespace wr */
