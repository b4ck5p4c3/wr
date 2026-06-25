#include "Errors.hpp"

namespace wr {

ErrorBase::ErrorBase() = default;

ErrorBase::ErrorBase(StringView message) : m_message(message) {}

ErrorBase::ErrorBase(Allocator allocator, StringView message)
    : m_message(allocator, message)
{}

ErrorBase::~ErrorBase() = default;

fn ErrorBase::message() const -> String { return m_message; }

fn ErrorBase::is_critical() const noexcept -> bool
{
  return m_severity == Severity::Critical;
}

fn ErrorBase::to_string() const -> String { return m_message; }

Error::Error() = default;

Error::Error(StringView message) : ErrorBase(message) {}

Error::Error(Allocator allocator, StringView message)
    : ErrorBase(allocator, message)
{}

fn Error::as_critical() -> Error &
{
  m_severity = Severity::Critical;
  return *this;
}

Warning::Warning(StringView message) : Error(message) {}

Warning::Warning(Allocator allocator, StringView message)
    : Error(allocator, message)
{}

} // namespace wr
