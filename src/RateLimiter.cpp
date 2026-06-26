#include "RateLimiter.hpp"

#include "ArrayList.hpp"
#include "String.hpp"
#include "Trace.hpp"

namespace wr {

namespace {

constexpr i64 RATE_BLOCK_BASE_SECONDS = 60;
constexpr i64 RATE_BLOCK_MAX_SECONDS = 3600;
constexpr i64 RATE_BAN_SECONDS = 86400;
constexpr i64 RATE_BAN_STRIKES = 5;

/* The strikes reset after an hour without a fresh block, so a brief burst does
   not hold an address near a ban forever. */
constexpr i64 RATE_DECAY_SECONDS = 3600;

/* A sweep of idle entries runs once the table crosses this many addresses, so a
   flood of one-shot addresses cannot grow it without bound. */
constexpr usize RATE_MAX_ENTRIES = 100000;

} // namespace

fn RateLimiter::sweep_expired(i64 now) -> void
{
  let allocator = m_entries.allocator();
  ArrayList<String> stale_keys{allocator};
  m_entries.for_each([&](StringView key, const entry &record) {
    if (now >= record.blocked_until &&
        now - record.window_start >= RATE_DECAY_SECONDS)
      stale_keys.push(String{allocator, key});
  });

  for (usize i = 0; i < stale_keys.count(); i++)
    m_entries.erase(stale_keys[i].view());

  LOG(Info, "rate limiter swept, dropped=%zu remaining=%zu", stale_keys.count(),
      m_entries.count());
}

fn RateLimiter::allow(StringView address, u8 bucket, rate_rule rule, i64 now)
    -> bool
{
  if (rule.max_count <= 0) return true;

  if (m_entries.count() >= RATE_MAX_ENTRIES) sweep_expired(now);

  char key[64];
  usize key_length = 0;
  key[key_length++] = static_cast<char>(bucket);
  for (usize i = 0; i < address.count() && key_length < sizeof(key); i++)
    key[key_length++] = address[i];

  let record = &m_entries.get_or_create(StringView{key, key_length}, entry{});

  if (now < record->blocked_until) return false;

  if (record->blocked_until != 0 &&
      now - record->blocked_until >= RATE_DECAY_SECONDS)
  {
    record->strike_count = 0;
    record->blocked_until = 0;
  }

  if (now - record->window_start >= rule.window_seconds) {
    record->window_start = now;
    record->request_count = 0;
  }

  record->request_count++;
  if (record->request_count <= rule.max_count) return true;

  record->strike_count++;
  i64 block_seconds = RATE_BAN_SECONDS;
  if (record->strike_count < RATE_BAN_STRIKES) {
    block_seconds = RATE_BLOCK_BASE_SECONDS << (record->strike_count - 1);
    if (block_seconds > RATE_BLOCK_MAX_SECONDS)
      block_seconds = RATE_BLOCK_MAX_SECONDS;
  }
  record->blocked_until = now + block_seconds;
  record->window_start = now;
  record->request_count = 0;

  LOG(Info, "rate limit hit, addr=%.*s bucket=%d strike=%lld block=%llds",
      static_cast<int>(address.count()), address.data, static_cast<int>(bucket),
      static_cast<long long>(record->strike_count),
      static_cast<long long>(block_seconds));
  return false;
}

} // namespace wr
