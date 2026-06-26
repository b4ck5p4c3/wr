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

/* Parse a base-ten signed integer. The fallback is returned for empty or
   malformed text. */
mustuse fn parse_i64(StringView text, i64 fallback) noexcept -> i64;

/* Whether any whole word in the text matches the profanity filter. Each run of
   letters is lowercased and looked up in the filter table. */
mustuse fn contains_swear(StringView text) noexcept -> bool;

/* A control character in a logged value breaks the line or forges another. Each
   control byte is replaced with a space. */
mustuse fn to_single_line(Allocator allocator, StringView text) -> String;

} // namespace wr
