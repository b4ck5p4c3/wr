#include "Utils.hpp"

#include <cstdio>
#include <ctime>

namespace wr {

fn now_seconds() -> i64 { return static_cast<i64>(std::time(nullptr)); }

fn percent_decode(Allocator allocator, StringView text) -> String
{
  String out{allocator};
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

fn random_token(Allocator allocator) -> ErrorOr<String>
{
  unsigned char bytes[16] = {};
  std::FILE *source = std::fopen("/dev/urandom", "rb");
  if (source == nullptr) return Error{"Unable to open the entropy source"};
  defer { std::fclose(source); };

  let const read_count = std::fread(bytes, 1, sizeof(bytes), source);
  if (read_count != sizeof(bytes))
    return Error{"The entropy source returned too few bytes"};

  String token{allocator};
  append_hex(token, bytes, sizeof(bytes));
  return token;
}

} // namespace wr
