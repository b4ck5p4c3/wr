#pragma once

#include "Allocator.hpp"
#include "App.hpp"
#include "Common.hpp"
#include "ErrorOr.hpp"
#include "Sqlite.hpp"
#include "Store.hpp"

#include <pthread.h>

namespace wr {

class HttpClient;

/* The liveness sweep. It runs on its own thread with its own store connection
   and a borrowed client, so a blocking probe never stalls the server loop. An
   up site is probed every five minutes, a down site every minute, and the
   reachability and the last probe time are recorded. */
class Liveness
{
public:
  Liveness(Allocator allocator, const config &cfg, HttpClient &client)
      : m_allocator(allocator), m_config(cfg), m_database(allocator),
        m_store(allocator, m_database), m_client(client)
  {}

  Liveness(const Liveness &) = delete;
  Liveness &operator=(const Liveness &) = delete;

  fn start() -> ErrorOr<Ok>;
  fn stop() -> void;

private:
  static constexpr i64 UP_INTERVAL_SECONDS = 300;
  static constexpr i64 DOWN_INTERVAL_SECONDS = 60;

  static fn thread_main(opaque *self) -> opaque *;
  fn run() -> void;
  fn sweep() -> void;

  Allocator m_allocator;
  const config &m_config;
  Sqlite m_database;
  Store m_store;
  HttpClient &m_client;
  pthread_t m_thread{};
  bool m_is_running{false};
  /* The worker polls the flag and the server sets it, so it is read and written
     through the atomic builtins. */
  bool m_should_stop{false};
};

} // namespace wr
