#pragma once

#include "Common.hpp"

struct sockaddr;

namespace wr {

/* True when the resolved address is a loopback, a link-local, or a private
   address. The curl client calls this on the address it is about to connect to.
   A probe cannot be steered at an internal address. */
mustuse fn address_is_private(const sockaddr *address) -> bool;

} // namespace wr
