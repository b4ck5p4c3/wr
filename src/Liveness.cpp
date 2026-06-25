#include "Liveness.hpp"

#include "Client.hpp"
#include "Http.hpp"
#include "Net.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

#include <unistd.h>

namespace wr {

fn Liveness::start() -> ErrorOr<Ok>
{
  TRY(m_database.open(m_config.database_path.view()));
  if (pthread_create(&m_thread, nullptr, &thread_main, this) != 0)
    return Error{"Unable to start the liveness thread"};
  m_is_running = true;
  LOG(Info, "liveness sweep started");
  return Success;
}

fn Liveness::stop() -> void
{
  if (!m_is_running) return;
  __atomic_store_n(&m_should_stop, true, __ATOMIC_SEQ_CST);
  pthread_join(m_thread, nullptr);
  m_is_running = false;
}

fn Liveness::thread_main(opaque *self) -> opaque *
{
  static_cast<Liveness *>(self)->run();
  return nullptr;
}

fn Liveness::run() -> void
{
  while (!__atomic_load_n(&m_should_stop, __ATOMIC_SEQ_CST)) {
    sweep();
    /* The wait is polled in one-second steps so a stop is honored promptly. */
    for (int i = 0;
         i < 60 && !__atomic_load_n(&m_should_stop, __ATOMIC_SEQ_CST); i++)
      sleep(1);
  }
}

fn Liveness::sweep() -> void
{
  let const sites_or = m_store.list_all_sites();
  if (sites_or.is_error()) {
    LOG(Debug, "liveness could not read the sites");
    return;
  }

  let const now = now_seconds();
  let const &sites = sites_or.value();
  for (usize i = 0; i < sites.count(); i++) {
    let const &row = sites[i];
    let const interval =
        row.is_reachable ? UP_INTERVAL_SECONDS : DOWN_INTERVAL_SECONDS;
    if (row.last_seen_at != 0 && now - row.last_seen_at < interval) {
      continue;
    }

    /* A site whose url resolves to a private address is taken out of the ring
       before any probe, so the sweep is never an ssrf vector. */
    if (!host_is_public(row.url.view())) {
      if (row.is_reachable)
        LOG(Info, "site %s resolves to a private address, taken down",
            row.slug.c_str());
      if (m_store.set_site_reachability(row.slug.view(), false, now).is_error())
        LOG(Info, "reachability write dropped for %s", row.slug.c_str());
      continue;
    }

    HttpRequestBuilder builder{m_allocator};
    let const request =
        builder.set_method(HttpMethod::Get).set_url(row.url.view()).build();
    let const response = m_client.send(request);

    let const is_up = !response.is_error() &&
                      response.value().status() >= 200 &&
                      response.value().status() < 400;
    if (is_up != row.is_reachable)
      LOG(Info, "site %s is now %s", row.slug.c_str(), is_up ? "up" : "down");

    if (m_store.set_site_reachability(row.slug.view(), is_up, now).is_error())
      LOG(Info, "reachability write dropped for %s", row.slug.c_str());
  }
}

} // namespace wr
