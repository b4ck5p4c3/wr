#include "Liveness.hpp"

#include "Client.hpp"
#include "Http.hpp"
#include "Trace.hpp"

#include <unistd.h>

namespace wr {

fn Liveness::start() -> ErrorOr<Ok>
{
  TRY(m_store.open(m_config.database_path.view()));
  if (pthread_create(&m_thread, nullptr, &thread_main, this) != 0)
    return Error{"Unable to start the liveness thread"};
  m_is_running = true;
  LOG(Info, "liveness sweep started");
  return Success;
}

fn Liveness::stop() -> void
{
  if (!m_is_running) return;
  m_should_stop = true;
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
  while (!m_should_stop) {
    sweep();
    /* The wait is broken into one-second steps, so a stop is honored promptly
       rather than after a full minute. */
    for (int i = 0; i < 60 && !m_should_stop; i++)
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
    if (row.last_seen_at != 0 && now - row.last_seen_at < interval) continue;

    HttpRequestBuilder builder{m_allocator};
    let const request =
        builder.set_method(HttpMethod::Get).set_url(row.url.view()).build();
    let const response = m_client.send(request);

    let const is_up = !response.is_error() &&
                      response.value().status() >= 200 &&
                      response.value().status() < 400;
    if (is_up != row.is_reachable)
      LOG(Info, "site %s is now %s", row.slug.c_str(), is_up ? "up" : "down");

    unused(
        m_store.set_site_reachability(row.slug.view(), is_up, now).is_error());
  }
}

} // namespace wr
