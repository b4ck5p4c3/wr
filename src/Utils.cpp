#include "Utils.hpp"

#include <cstdio>
#include <ctime>

namespace wr {

fn now_seconds() -> i64 { return static_cast<i64>(std::time(nullptr)); }

fn find_substring(StringView haystack, StringView needle) -> Maybe<usize>
{
  if (needle.count() == 0 || needle.count() > haystack.count()) return None;
  for (usize i = 0; i + needle.count() <= haystack.count(); i++) {
    if (haystack.substring_of_length(i, needle.count()) == needle) return i;
  }
  return None;
}

fn percent_decode(Allocator allocator, StringView text) -> String
{
  String out{allocator};
  for (usize i = 0; i < text.count(); i++) {
    let const c = text[i];
    if (c == '+') {
      out.push(' ');
    } else if (c == '%' && i + 2 < text.count()) {
      let const hex = [](char digit) -> int {
        if (digit >= '0' && digit <= '9') return digit - '0';
        if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
        if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
        return -1;
      };
      let const high = hex(text[i + 1]);
      let const low = hex(text[i + 2]);
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

fn random_token(Allocator allocator) -> String
{
  String token{allocator};
  unsigned char bytes[16] = {};
  std::FILE *source = std::fopen("/dev/urandom", "rb");
  if (source != nullptr) {
    unused(std::fread(bytes, 1, sizeof(bytes), source));
    std::fclose(source);
  }
  append_hex(token, bytes, sizeof(bytes));
  return token;
}

fn json_string_field(Allocator allocator, StringView json, StringView key)
    -> Maybe<String>
{
  String needle{allocator};
  needle.push('"');
  needle.append(key);
  needle.push('"');
  let const at = find_substring(json, needle.view());
  if (!at.has_value()) return None;

  usize i = at.value() + needle.count();
  while (i < json.count() && json[i] != ':')
    i++;
  i++;
  while (i < json.count() && (json[i] == ' ' || json[i] == '\t'))
    i++;
  if (i >= json.count() || json[i] != '"') return None;
  i++;

  String value{allocator};
  while (i < json.count() && json[i] != '"') {
    if (json[i] == '\\' && i + 1 < json.count()) {
      i++;
      value.push(json[i] == 'n' ? '\n' : json[i]);
    } else {
      value.push(json[i]);
    }
    i++;
  }
  return Maybe<String>{steal(value)};
}

fn json_number_field(Allocator allocator, StringView json, StringView key)
    -> Maybe<String>
{
  String needle{allocator};
  needle.push('"');
  needle.append(key);
  needle.push('"');
  let const at = find_substring(json, needle.view());
  if (!at.has_value()) return None;

  usize i = at.value() + needle.count();
  while (i < json.count() && json[i] != ':')
    i++;
  i++;
  while (i < json.count() && (json[i] == ' ' || json[i] == '\t'))
    i++;

  String value{allocator};
  while (i < json.count() && json[i] >= '0' && json[i] <= '9') {
    value.push(json[i]);
    i++;
  }
  if (value.is_empty()) return None;
  return Maybe<String>{steal(value)};
}

} // namespace wr
