#include "Utils.hpp"

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

} // namespace wr
