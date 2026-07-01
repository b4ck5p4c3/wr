#pragma once

#include "Allocator.hpp"
#include "App.hpp"
#include "Common.hpp"
#include "ErrorOr.hpp"
#include "Postgres.hpp"
#include "Pthread.hpp"
#include "Sqlite.hpp"
#include "Store.hpp"

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
      : m_allocator(allocator), m_config(cfg), m_sqlite_database(allocator),
        m_postgres_database(allocator),
        m_database(select_backend(cfg, m_sqlite_database, m_postgres_database)),
        m_store(allocator, m_database), m_client(client)
  {}

  Liveness(const Liveness &) = delete;
  Liveness &operator=(const Liveness &) = delete;

  fn start() -> ErrorOr<Ok>;
  fn stop() -> void;

private:
  static fn select_backend(const config &cfg, Sqlite &sqlite_database,
                           Postgres &postgres_database) -> SqlDatabase &
  {
    if (cfg.is_postgres_backend) return postgres_database;
    return sqlite_database;
  }

  static constexpr i64 UP_INTERVAL_SECONDS = 300;
  static constexpr i64 DOWN_INTERVAL_SECONDS = 60;
  static constexpr i64 ORG_REFRESH_SECONDS = 86400;
  static constexpr usize ORG_REFRESH_MAX_PER_SWEEP = 25;

  static fn thread_main(opaque *self) -> opaque *;
  fn run() -> void;
  fn sweep() -> void;
  fn refresh_org_membership(i64 now) -> void;

  Allocator m_allocator;
  const config &m_config;
  Sqlite m_sqlite_database;
  Postgres m_postgres_database;
  SqlDatabase &m_database;
  Store m_store;
  HttpClient &m_client;
  PthreadThread m_thread{};
  bool m_is_running{false};
  /* The worker polls the flag and the server sets it, so it is read and written
     through the atomic builtins. */
  bool m_should_stop{false};
};

} // namespace wr
