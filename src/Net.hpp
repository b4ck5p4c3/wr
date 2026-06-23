#pragma once

#include "Common.hpp"
#include "StringView.hpp"

struct sockaddr;

namespace wr {

/* The ssrf guard for an outbound probe. It is true only when the url is http or
   https and its host resolves entirely to public addresses, so a probe never
   reaches a loopback, a link-local, or a private address. This is a cheap early
   check, the connect-time check below is the authoritative one. */
mustuse fn host_is_public(StringView url) -> bool;

/* True when the resolved address is a loopback, a link-local, or a private
   address. The curl client calls this on the address it is about to connect to,
   so a DNS rebinding between the early check and the connect cannot steer a
   probe at an internal address. */
mustuse fn address_is_private(const sockaddr *address) -> bool;

} // namespace wr
