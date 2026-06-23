#pragma once

#include "Common.hpp"
#include "StringView.hpp"

namespace wr {

/* The ssrf guard for an outbound probe. It is true only when the url is http or
   https and its host resolves entirely to public addresses, so a probe never
   reaches a loopback, a link-local, or a private address. */
mustuse fn host_is_public(StringView url) -> bool;

} // namespace wr
