#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "ErrorOr.hpp"
#include "Maybe.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace wr {

/* The current unix time in seconds. */
mustuse fn now_seconds() -> i64;

/* The index of the first occurrence of needle in haystack, or None. */
mustuse fn find_substring(StringView haystack, StringView needle)
    -> Maybe<usize>;

/* Decode a percent-encoded form value, turning a plus into a space. */
mustuse fn percent_decode(Allocator allocator, StringView text) -> String;

/* Append the lowercase hex of the bytes to the string. */
fn append_hex(String &out, const unsigned char *bytes, usize length) -> void;

/* Compare two byte strings without an early exit, so a check does not leak the
   matching prefix length through its timing. */
mustuse fn constant_time_equal(StringView left, StringView right) -> bool;

/* A random opaque token, sixteen bytes of urandom hex encoded. An entropy read
   that fails returns an error, so a predictable token is never issued. */
mustuse fn random_token(Allocator allocator) -> ErrorOr<String>;

/* A flat JSON string or number field by key, used to read a small request body
   or an OAuth response. Nested structures are not parsed. */
mustuse fn json_string_field(Allocator allocator, StringView json,
                             StringView key) -> Maybe<String>;
mustuse fn json_number_field(Allocator allocator, StringView json,
                             StringView key) -> Maybe<String>;

} // namespace wr
