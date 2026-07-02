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

static const StringView GITHUB_API_USER_AGENT = "wr-webring";

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

  refresh_org_membership(now);
}

fn Liveness::refresh_org_membership(i64 now) -> void
{
  if (m_config.is_dev_mode || m_config.github_org.view().is_empty()) {
    return;
  }

  let const handles_or =
      m_store.list_org_handles_due(now - ORG_REFRESH_SECONDS);
  if (handles_or.is_error()) {
    LOG(Debug, "org membership handles could not be read");
    return;
  }

  let const &handles = handles_or.value();
  if (handles.is_empty()) return;

  let const has_token = !m_config.github_org_token.view().is_empty();

  String authorization{m_allocator};
  if (has_token) {
    authorization.append("token ");
    authorization.append(m_config.github_org_token.view());
  }

  usize attempted_count = 0;
  for (usize i = 0; i < handles.count(); i++) {
    if (__atomic_load_n(&m_should_stop, __ATOMIC_SEQ_CST)) return;

    if (attempted_count >= ORG_REFRESH_MAX_PER_SWEEP) {
      LOG(Info, "org membership refresh capped at %zu handles this sweep",
          ORG_REFRESH_MAX_PER_SWEEP);
      break;
    }

    let const &handle = handles[i];

    String url{m_allocator};
    url.append("https://api.github.com/orgs/");
    url.append(m_config.github_org.view());
    url.append(has_token ? "/members/" : "/public_members/");
    url.append(handle.view());

    HttpRequestBuilder builder{m_allocator};
    builder.set_method(HttpMethod::Get);
    builder.set_url(url.view());
    builder.add_auxiliary_headers(GITHUB_API_USER_AGENT, "application/json");
    if (has_token) builder.add_header("Authorization", authorization.view());

    attempted_count++;
    let const response = m_client.send(builder.build());
    if (response.is_error()) continue;

    let const is_member = response.value().status() == 204;
    if (m_store.set_org_membership(handle.view(), is_member, now).is_error())
      LOG(Info, "org membership write dropped for %s", handle.c_str());
  }
}

} // namespace wr
