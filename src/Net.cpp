#include "Net.hpp"

#include "Common.hpp"

#include <arpa/inet.h>
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
    let is_loopback = bytes[15] == 1;
    for (usize i = 0; i < 15; i++)
      if (bytes[i] != 0) is_loopback = false;
    if (is_loopback) return true;
    if ((bytes[0] & 0xfe) == 0xfc) return true;
    if (bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80) {
      return true;
    }

    bool is_v4_mapped = bytes[10] == 0xff && bytes[11] == 0xff;
    for (usize i = 0; i < 10; i++)
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

} // namespace wr
