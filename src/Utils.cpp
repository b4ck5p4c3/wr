#include "Utils.hpp"

#include "StaticStringMap.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/random.h>

namespace wr {

fn now_seconds() -> i64 { return static_cast<i64>(std::time(nullptr)); }

fn percent_decode(Allocator allocator, StringView text) -> String
{
  String out{allocator};
  out.reserve(text.count());
  for (usize i = 0; i < text.count(); i++) {
    let const c = text[i];
    if (c == '+') {
      out.push(' ');
    } else if (c == '%' && i + 2 < text.count()) {
      let const do_hex = [](char digit) -> int {
        if (digit >= '0' && digit <= '9') return digit - '0';
        if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
        if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
        return -1;
      };
      let const high = do_hex(text[i + 1]);
      let const low = do_hex(text[i + 2]);
      if (high >= 0 && low >= 0) {
        out.push(static_cast<char>((high << 4) | low));
        i += 2;
      } else {
        out.push(c);
      }
    } else {
      out.push(c);
    }
  }
  return out;
}

fn append_hex(String &out, const unsigned char *bytes, usize length) -> void
{
  let const digits = "0123456789abcdef";
  for (usize i = 0; i < length; i++) {
    out.push(digits[bytes[i] >> 4]);
    out.push(digits[bytes[i] & 0x0f]);
  }
}

fn constant_time_equal(StringView left, StringView right) -> bool
{
  if (left.count() != right.count()) return false;
  unsigned char difference = 0;
  for (usize i = 0; i < left.count(); i++)
    difference |= static_cast<unsigned char>(left[i] ^ right[i]);
  return difference == 0;
}

fn random_bytes(opaque *buffer, usize count) -> ErrorOr<Ok>
{
  let bytes = static_cast<unsigned char *>(buffer);
  usize filled_count = 0;
  while (filled_count < count) {
    let const got = ::getrandom(bytes + filled_count, count - filled_count, 0);
    if (got < 0) {
      if (errno == EINTR) continue;
      return Error{"The entropy source is unavailable"};
    }
    filled_count += static_cast<usize>(got);
  }
  return Success;
}

fn random_token(Allocator allocator) -> ErrorOr<String>
{
  unsigned char bytes[16] = {};
  TRY(random_bytes(bytes, sizeof(bytes)));

  String token{allocator};
  append_hex(token, bytes, sizeof(bytes));
  return token;
}

fn parse_i64(StringView text, i64 fallback) noexcept -> i64
{
  usize i = 0;
  bool is_negative = false;
  if (i < text.count() && (text[i] == '-' || text[i] == '+')) {
    is_negative = text[i] == '-';
    i++;
  }

  if (i >= text.count()) return fallback;

  constexpr i64 I64_MAX = 9223372036854775807;
  i64 value = 0;
  for (; i < text.count(); i++) {
    if (text[i] < '0' || text[i] > '9') return fallback;

    let const digit = static_cast<i64>(text[i] - '0');
    if (value > (I64_MAX - digit) / 10) return fallback;
    value = (value * 10) + digit;
  }

  return is_negative ? -value : value;
}

/* The words are written as byte escapes, so the source carries no literal slur.
   The mild fuck, sex, bitch, and shit are left off, so they pass the filter. */
static constexpr StaticStringMap<bool, 9> SWEAR_WORDS{
    {{"\x61\x73\x73\x68\x6f\x6c\x65", true},
     {"\x62\x61\x73\x74\x61\x72\x64", true},
     {"\x63\x75\x6e\x74", true},
     {"\x64\x69\x63\x6b", true},
     {"\x70\x69\x73\x73", true},
     {"\x73\x6c\x75\x74", true},
     {"\x77\x68\x6f\x72\x65", true},
     {"\x6e\x69\x67\x67\x65\x72", true},
     {"\x66\x61\x67\x67\x6f\x74", true}}
};

fn contains_swear(StringView text) noexcept -> bool
{
  char token[32];
  usize token_length = 0;
  bool is_overflowed = false;

  for (usize i = 0; i <= text.count(); i++) {
    let const c = i < text.count() ? text[i] : '\0';
    let const is_letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');

    if (is_letter) {
      if (token_length < sizeof(token)) {
        let const lowered = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
        token[token_length++] = static_cast<char>(lowered);
      } else {
        is_overflowed = true;
      }
      continue;
    }

    if (token_length > 0 && !is_overflowed) {
      if (SWEAR_WORDS.find(StringView{token, token_length}) != nullptr)
        return true;
    }
    token_length = 0;
    is_overflowed = false;
  }

  return false;
}

fn to_single_line(Allocator allocator, StringView text) -> String
{
  String out{allocator};
  out.reserve(text.count());
  for (usize i = 0; i < text.count(); i++) {
    let const byte = static_cast<unsigned char>(text[i]);
    out.push(byte < 0x20 || byte == 0x7f ? ' ' : text[i]);
  }
  return out;
}

} // namespace wr
