#pragma once

#include "Allocator.hpp"
#include "App.hpp"
#include "Common.hpp"
#include "Curl.hpp"
#include "ErrorOr.hpp"
#include "Store.hpp"

#include <pthread.h>

namespace wr {

/* The liveness sweep. It runs on its own thread with its own store connection
   and its own curl client, so a blocking probe never stalls the server loop. An
   up site is probed every five minutes, a down site every minute, and the
   reachability and the last probe time are recorded. */
class Liveness
{
public:
  Liveness(Allocator allocator, const config &cfg)
      : m_allocator(allocator), m_config(cfg), m_store(allocator),
        m_client(allocator, redirecting_options())
  {}

  Liveness(const Liveness &) = delete;
  Liveness &operator=(const Liveness &) = delete;

  fn start() -> ErrorOr<Ok>;
  fn stop() -> void;

private:
  static constexpr i64 UP_INTERVAL_SECONDS = 300;
  static constexpr i64 DOWN_INTERVAL_SECONDS = 60;

  static fn redirecting_options() -> CurlClient::Options
  {
    CurlClient::Options options;
    options.timeout_ms = 10000;
    options.should_follow_redirects = true;
    return options;
  }

  static fn thread_main(opaque *self) -> opaque *;
  fn run() -> void;
  fn sweep() -> void;

  Allocator m_allocator;
  const config &m_config;
  Store m_store;
  CurlClient m_client;
  pthread_t m_thread{};
  bool m_is_running{false};
  volatile bool m_should_stop{false};
};

} // namespace wr
