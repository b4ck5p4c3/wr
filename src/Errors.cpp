#include "Errors.hpp"

namespace wr {

ErrorBase::ErrorBase() {}

ErrorBase::ErrorBase(StringView message) : m_message(message) {}

ErrorBase::ErrorBase(Allocator allocator, StringView message)
    : m_message(allocator, message)
{}

ErrorBase::~ErrorBase() {}

fn ErrorBase::message() const -> String { return m_message; }

fn ErrorBase::to_string() const -> String { return m_message; }

Error::Error() {}

Error::Error(StringView message) : ErrorBase(message) {}

Error::Error(Allocator allocator, StringView message)
    : ErrorBase(allocator, message)
{}

Warning::Warning(StringView message) : Error(message) {}

Warning::Warning(Allocator allocator, StringView message)
    : Error(allocator, message)
{}

} // namespace wr
