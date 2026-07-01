#include "Liveness.hpp"

#include "Client.hpp"
#include "Http.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

#include <cstring>
#include <unistd.h>

namespace wr {

static const StringView LIVENESS_USER_AGENT =
    "Mozilla/5.0 (compatible; wr-webring liveness checker)";

fn Liveness::start() -> ErrorOr<Ok>
{
  TRY(m_database.open(m_config.database_path.view()));

  let const code = m_thread.start(&thread_main, this);
  if (code != 0) {
    String message{m_allocator};
    message.append("Unable to start the liveness thread, ");
    message.append(std::strerror(code));
    return Error{message.view()};
  }
  m_is_running = true;
  LOG(Info, "liveness sweep started");
  return Success;
}

fn Liveness::stop() -> void
{
  if (!m_is_running) return;
  __atomic_store_n(&m_should_stop, true, __ATOMIC_SEQ_CST);
  m_thread.join();
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
  LOG(Info, "liveness sweep, checking %zu sites", sites.count());
  for (usize i = 0; i < sites.count(); i++) {
    if (__atomic_load_n(&m_should_stop, __ATOMIC_SEQ_CST)) return;

    let const &row = sites[i];
    let const interval =
        row.is_reachable ? UP_INTERVAL_SECONDS : DOWN_INTERVAL_SECONDS;
    if (row.last_seen_at != 0 && now - row.last_seen_at < interval) {
      continue;
    }

    HttpRequestBuilder builder{m_allocator};
    let const request =
        builder.set_method(HttpMethod::Get)
            .set_url(row.url.view())
            .add_auxiliary_headers(LIVENESS_USER_AGENT, "text/html")
            .build();
    let const response = m_client.send(request);

    let const status = response.is_error() ? 0 : response.value().status();
    let const is_up = status >= 200 && status < 500;
    if (is_up != row.is_reachable)
      LOG(Info, "site %s is now %s", row.slug.c_str(), is_up ? "up" : "down");

    if (m_store.set_site_reachability(row.slug.view(), is_up, now).is_error())
      LOG(Info, "reachability write dropped for %s", row.slug.c_str());
    if (m_store.record_liveness(row.slug.view(), is_up, now).is_error())
      LOG(Info, "liveness record dropped for %s", row.slug.c_str());
  }

  if (m_store.rotate_liveness(now).is_error())
    LOG(Debug, "liveness bucket rotation dropped");
}

} // namespace wr
