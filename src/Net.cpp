#include "Net.hpp"

#include "Common.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace wr {

namespace {

fn ipv4_is_private(u32 host_order) -> bool
{
  let const b0 = (host_order >> 24) & 0xff;
  let const b1 = (host_order >> 16) & 0xff;
  if (b0 == 0 || b0 == 10 || b0 == 127) {
    return true;
  }
  if (b0 == 169 && b1 == 254) {
    return true;
  }
  if (b0 == 172 && b1 >= 16 && b1 <= 31) {
    return true;
  }
  if (b0 == 192 && b1 == 168) {
    return true;
  }
  if (b0 == 100 && b1 >= 64 && b1 <= 127) {
    return true;
  }
  return false;
}

/* The host of an http or https url, written NUL terminated into the buffer. The
   scheme is required and the userinfo and the port are stripped. */
fn extract_http_host(StringView url, char *out, usize capacity) -> bool
{
  usize scheme_end = url.count();
  for (usize i = 0; i + 2 < url.count(); i++) {
    if (url[i] == ':' && url[i + 1] == '/' && url[i + 2] == '/') {
      scheme_end = i;
      break;
    }
  }
  if (scheme_end == url.count()) return false;
  let const scheme = url.substring_of_length(0, scheme_end);
  if (scheme != "http" && scheme != "https") {
    return false;
  }

  usize authority_end = scheme_end + 3;
  while (authority_end < url.count() && url[authority_end] != '/' &&
         url[authority_end] != '?' && url[authority_end] != '#')
    authority_end++;
  let authority =
      url.substring_of_length(scheme_end + 3, authority_end - scheme_end - 3);

  usize host_begin = 0;
  for (usize i = 0; i < authority.count(); i++)
    if (authority[i] == '@') host_begin = i + 1;
  let const host_port = authority.substring(host_begin);

  StringView host;
  if (host_port.count() > 0 && host_port[0] == '[') {
    usize close = 1;
    while (close < host_port.count() && host_port[close] != ']')
      close++;
    host = host_port.substring_of_length(1, close - 1);
  } else {
    usize colon = 0;
    while (colon < host_port.count() && host_port[colon] != ':')
      colon++;
    host = host_port.substring_of_length(0, colon);
  }

  if (host.is_empty() || host.count() >= capacity) return false;
  for (usize i = 0; i < host.count(); i++)
    out[i] = host[i];
  out[host.count()] = '\0';
  return true;
}

} // namespace

fn address_is_private(const sockaddr *address) -> bool
{
  if (address->sa_family == AF_INET) {
    let const v4 = reinterpret_cast<const sockaddr_in *>(address);
    return ipv4_is_private(ntohl(v4->sin_addr.s_addr));
  }
  if (address->sa_family == AF_INET6) {
    let const bytes =
        reinterpret_cast<const sockaddr_in6 *>(address)->sin6_addr.s6_addr;
    bool is_loopback = bytes[15] == 1;
    for (int i = 0; i < 15; i++)
      if (bytes[i] != 0) is_loopback = false;
    if (is_loopback) return true;
    if ((bytes[0] & 0xfe) == 0xfc) return true;
    if (bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80) {
      return true;
    }

    bool is_v4_mapped = bytes[10] == 0xff && bytes[11] == 0xff;
    for (int i = 0; i < 10; i++)
      if (bytes[i] != 0) is_v4_mapped = false;
    if (is_v4_mapped) {
      let const v4 = (static_cast<u32>(bytes[12]) << 24) |
                     (static_cast<u32>(bytes[13]) << 16) |
                     (static_cast<u32>(bytes[14]) << 8) |
                     static_cast<u32>(bytes[15]);
      return ipv4_is_private(v4);
    }
    return false;
  }
  return true;
}

fn classify_host(StringView url) -> host_reachability
{
  char host[256];
  if (!extract_http_host(url, host, sizeof(host)))
    return host_reachability::unresolved;

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *results = nullptr;
  if (getaddrinfo(host, nullptr, &hints, &results) != 0)
    return host_reachability::unresolved;
  defer { freeaddrinfo(results); };

  if (results == nullptr) return host_reachability::unresolved;

  for (const addrinfo *it = results; it != nullptr; it = it->ai_next) {
    if (address_is_private(it->ai_addr))
      return host_reachability::private_address;
  }
  return host_reachability::public_address;
}

} // namespace wr
