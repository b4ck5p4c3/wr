#pragma once

#include "Common.hpp"
#include "StringView.hpp"

struct sockaddr;

namespace wr {

/* The outcome of the ssrf pre-check for an outbound probe url. */
enum class host_reachability : u8
{
  public_address,
  private_address,
  unresolved,
};

/* The ssrf guard for an outbound probe. A host that does not parse or does not
   resolve is unresolved, a host that resolves to any loopback, link-local, or
   private address is private_address, and a host that is http or https and
   resolves entirely to public addresses is public_address. This is a cheap
   early check, the connect-time check below is the authoritative one. */
mustuse fn classify_host(StringView url) -> host_reachability;

/* True when the resolved address is a loopback, a link-local, or a private
   address. The curl client calls this on the address it is about to connect to,
   so a DNS rebinding between the early check and the connect cannot steer a
   probe at an internal address. */
mustuse fn address_is_private(const sockaddr *address) -> bool;

} // namespace wr
