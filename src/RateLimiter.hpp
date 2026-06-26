#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "StringMap.hpp"
#include "StringView.hpp"

namespace wr {

/* A cap of max_count requests per window_seconds. A zero max_count is
   unlimited. */
struct rate_rule
{
  i64 max_count;
  i64 window_seconds;
};

/* A per-IP throttle. Each address counts against a fixed window per bucket, and
   a violation grows an exponential block that ends in a 24 hour ban once the
   strikes pile up. The strikes decay after a long quiet spell. The event loop
   is single threaded, so the table is read without a lock. */
class RateLimiter
{
public:
  RateLimiter(Allocator allocator) : m_entries(allocator) {}

  RateLimiter(const RateLimiter &) = delete;
  RateLimiter &operator=(const RateLimiter &) = delete;

  /* Whether a request from the address is allowed now. The bucket namespaces
     the counter, so each route counts the address on its own window. A blocked
     request adds a strike and pushes the block further out. */
  mustuse fn allow(StringView address, u8 bucket, rate_rule rule, i64 now)
      -> bool;

private:
  struct entry
  {
    i64 window_start{0};
    i64 request_count{0};
    i64 strike_count{0};
    i64 blocked_until{0};
  };

  /* The table is one entry per distinct address, so a flood of fresh addresses
     grows it without bound. An idle entry is dropped once it carries no live
     block and its window has long lapsed. */
  fn sweep_expired(i64 now) -> void;

  StringMap<entry> m_entries;
};

} // namespace wr
