#include "Errors.hpp"

namespace wr {

ErrorBase::ErrorBase() {}

ErrorBase::ErrorBase(StringView message) : m_message(message) {}

ErrorBase::~ErrorBase() {}

fn ErrorBase::message() const -> String { return m_message; }

fn ErrorBase::to_string() const -> String { return m_message; }

Error::Error() {}

Error::Error(StringView message) : ErrorBase(message) {}

Warning::Warning(StringView message) : Error(message) {}

} // namespace wr
