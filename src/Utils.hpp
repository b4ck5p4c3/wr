#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "ErrorOr.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace wr {

/* The current unix time in seconds. */
mustuse fn now_seconds() -> i64;

/* Decode a percent-encoded form value, turning a plus into a space. */
mustuse fn percent_decode(Allocator allocator, StringView text) -> String;

/* Append the lowercase hex of the bytes to the string. */
fn append_hex(String &out, const unsigned char *bytes, usize length) -> void;

/* Compare two byte strings without an early exit, so a check does not leak the
   matching prefix length through its timing. */
mustuse fn constant_time_equal(StringView left, StringView right) -> bool;

/* Fill the buffer with kernel entropy through getrandom. A short read is
   retried, and a failure returns an error so a predictable value is never
   issued. */
mustuse fn random_bytes(opaque *buffer, usize count) -> ErrorOr<Ok>;

/* A random opaque token, sixteen bytes of entropy hex encoded. An entropy read
   that fails returns an error, so a predictable token is never issued. */
mustuse fn random_token(Allocator allocator) -> ErrorOr<String>;

} // namespace wr
