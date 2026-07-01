#include "App.hpp"

#include "Embedded.hpp"
#include "Http.hpp"
#include "StaticStringMap.hpp"
#include "Trace.hpp"

namespace wr {

fn find_embedded_asset(StringView path) -> const embedded_asset *
{
  for (usize i = 0; i < EMBEDDED_ASSET_COUNT; i++)
    if (path == EMBEDDED_ASSETS[i].path) return &EMBEDDED_ASSETS[i];

  return nullptr;
}

/* The socket peer is the un-spoofable address, so it is the default the rate
   limiter and the audit trail key on. A forwarded header is client controlled
   and is read only when the operator marks the proxy trusted. The trusted proxy
   appends the real client as the last hop, so the rightmost x-forwarded-for
   entry is taken and a client supplied prefix is ignored. */
fn client_address(HttpServerEvent &event, bool is_forwarded_trusted)
    -> StringView
{
  if (!is_forwarded_trusted) return event.client_ip();

  let const &headers = event.request_headers();
  if (let const forwarded = headers.get("x-forwarded-for");
      forwarded.has_value())
  {
    let const value = forwarded.value();
    usize last_hop_start = value.count();
    while (last_hop_start > 0 && value[last_hop_start - 1] != ',')
      last_hop_start--;

    let const last_hop = value.substring(last_hop_start);
    usize lead = 0;
    while (lead < last_hop.count() && last_hop[lead] == ' ')
      lead++;

    usize trail = last_hop.count();
    while (trail > lead && last_hop[trail - 1] == ' ')
      trail--;

    let const trimmed = last_hop.substring_of_length(lead, trail - lead);
    if (!trimmed.is_empty()) return trimmed;
  }

  if (let const real_ip = headers.get("x-real-ip"); real_ip.has_value()) {
    let const value = real_ip.value();
    usize lead = 0;
    while (lead < value.count() && value[lead] == ' ')
      lead++;

    usize trail = value.count();
    while (trail > lead && value[trail - 1] == ' ')
      trail--;

    let const trimmed = value.substring_of_length(lead, trail - lead);
    if (!trimmed.is_empty()) return trimmed;
  }

  return event.client_ip();
}

namespace {

fn content_type_for(StringView path) -> StringView
{
  struct mime_type
  {
    const char *suffix;
    const char *type;
  };
  static constexpr mime_type TABLE[] = {
      {".html",  "text/html; charset=utf-8"      },
      {".js",    "text/javascript; charset=utf-8"},
      {".css",   "text/css; charset=utf-8"       },
      {".json",  "application/json"              },
      {".svg",   "image/svg+xml"                 },
      {".png",   "image/png"                     },
      {".webp",  "image/webp"                    },
      {".ico",   "image/x-icon"                  },
      {".woff2", "font/woff2"                    },
  };
  for (let const &entry : TABLE)
    if (path.ends_with(entry.suffix)) return entry.type;
  return "application/octet-stream";
}

fn split_first_segment(StringView path, StringView &rest) -> StringView
{
  usize start = 0;
  if (path.count() > 0 && path[0] == '/') {
    start = 1;
  }
  usize end = start;
  while (end < path.count() && path[end] != '/')
    end++;
  rest = end < path.count() ? path.substring(end + 1) : StringView{};
  return path.substring_of_length(start, end - start);
}

fn contains_double_dot(StringView path) -> bool
{
  for (usize i = 0; i + 1 < path.count(); i++) {
    if (path[i] == '.' && path[i + 1] == '.') {
      return true;
    }
  }
  return false;
}

/* A textual client identifies itself by a user agent prefix, so it is served
   the JSON form of a route instead of the page a browser is given. */
fn prefers_json(HttpServerEvent &event) -> bool
{
  let const user_agent = event.request_headers().get("user-agent");
  if (!user_agent.has_value()) return false;

  static const StringView TEXTUAL_AGENT_PREFIXES[] = {
      "curl/",           "Wget/",        "HTTPie/", "python-requests/",
      "Go-http-client/", "libwww-perl/", "okhttp/"};
  for (let const prefix : TEXTUAL_AGENT_PREFIXES)
    if (user_agent.value().starts_with(prefix)) return true;

  return false;
}

} // namespace

fn find_query_param(StringView query, StringView name, Allocator allocator)
    -> Maybe<String>
{
  usize i = 0;
  while (i < query.count()) {
    usize pair_end = i;
    while (pair_end < query.count() && query[pair_end] != '&')
      pair_end++;
    let const pair = query.substring_of_length(i, pair_end - i);

    usize equals = 0;
    while (equals < pair.count() && pair[equals] != '=')
      equals++;
    let const key = pair.substring_of_length(0, equals);
    if (key == name && equals < pair.count()) {
      return Maybe<String>{
          percent_decode(allocator, pair.substring(equals + 1))};
    }

    i = pair_end + 1;
  }
  return None;
}

fn find_cookie(StringView cookie_header, StringView name) -> Maybe<StringView>
{
  usize i = 0;
  while (i < cookie_header.count()) {
    while (i < cookie_header.count() &&
           (cookie_header[i] == ' ' || cookie_header[i] == ';'))
      i++;
    usize pair_end = i;
    while (pair_end < cookie_header.count() && cookie_header[pair_end] != ';')
      pair_end++;
    let const pair = cookie_header.substring_of_length(i, pair_end - i);

    usize equals = 0;
    while (equals < pair.count() && pair[equals] != '=')
      equals++;
    if (pair.substring_of_length(0, equals) == name && equals < pair.count()) {
      return pair.substring(equals + 1);
    }

    i = pair_end + 1;
  }
  return None;
}

/* The account identity carries the provider, so the public profile link is
   built from it. The dev bypass accounts point at GitHub. */
fn write_site_json(JsonWriter &writer, const site &row,
                   const ArrayList<reaction_count> *reactions,
                   const ArrayList<String> *reacted, const i64 *click_count)
    -> void
{
  writer.object_begin();
  writer.field("slug", row.slug.view());
  writer.field("name", row.name.view());
  writer.field("url", row.url.view());
  writer.field("description", row.description.view());
  writer.key("created_at");
  writer.number(row.created_at);
  writer.field("owner_oauth", source_oauth(row.owner.source));
  writer.field("owner_tag", row.owner.name.view());
  writer.field("owner_name", row.owner.name.view());

  if (reactions != nullptr) {
    writer.key("reactions");
    writer.object_begin();
    for (usize i = 0; i < reactions->count(); i++) {
      writer.key((*reactions)[i].emoji.view());
      writer.number((*reactions)[i].count);
    }
    writer.object_end();
  }

  if (reacted != nullptr) {
    writer.key("reacted");
    writer.array_begin();
    for (usize i = 0; i < reacted->count(); i++)
      writer.string((*reacted)[i].view());
    writer.array_end();
  }

  if (click_count != nullptr) {
    writer.key("click_count");
    writer.number(*click_count);
  }

  writer.object_end();
}

enum class route : u8
{
  static_page,
  sites,
  auth_github_login,
  auth_github_callback,
  auth_telegram_callback,
  auth_dev,
  auth_logout,
  config,
  me,
  admin_pending,
  admin_approve,
  admin_reject,
  admin_site,
  admin_site_add,
  admin_site_delete,
  admin_logs,
  admin_audit,
  admin_comments,
  admin_comment_approve,
  admin_comment_delete,
  admin_stats,
  admin_cache_clear,
  sites_add,
  sites_rename,
  sites_react,
  sites_click,
  comments_list,
  comments_add,
};

/* A GET route accepts any method, while a POST or DELETE route is reached only
   through that method, so a cross-site GET cannot drive a write on a signed-in
   browser. The limit caps a single address on the route, keyed by the route id
   as the bucket, and a zero cap leaves the route unlimited. */
struct route_target
{
  route id;
  HttpMethod method;
  rate_rule limit;
};

static constexpr let ROUTES = StaticStringMap<route_target, 32>{
    {{"/", {route::static_page, HttpMethod::Get, {0, 0}}},
     {"/about", {route::static_page, HttpMethod::Get, {0, 0}}},
     {"/panel", {route::static_page, HttpMethod::Get, {0, 0}}},
     {"/admin", {route::static_page, HttpMethod::Get, {0, 0}}},
     {"/docs", {route::static_page, HttpMethod::Get, {0, 0}}},
     {"/sites", {route::sites, HttpMethod::Get, {100, 60}}},
     {"/auth/github", {route::auth_github_login, HttpMethod::Get, {20, 60}}},
     {"/auth/github/callback",
      {route::auth_github_callback, HttpMethod::Get, {5, 60}}},
     {"/auth/telegram/callback",
      {route::auth_telegram_callback, HttpMethod::Get, {10, 60}}},
     {"/auth/dev", {route::auth_dev, HttpMethod::Get, {30, 60}}},
     {"/auth/logout", {route::auth_logout, HttpMethod::Post, {10, 60}}},
     {"/api/v1/config", {route::config, HttpMethod::Get, {200, 60}}},
     {"/api/v1/me", {route::me, HttpMethod::Get, {30, 60}}},
     {"/api/v1/admin/pending",
      {route::admin_pending, HttpMethod::Get, {120, 60}}},
     {"/api/v1/admin/pending/approve",
      {route::admin_approve, HttpMethod::Post, {120, 60}}},
     {"/api/v1/admin/pending/reject",
      {route::admin_reject, HttpMethod::Delete, {120, 60}}},
     {"/api/v1/admin/site", {route::admin_site, HttpMethod::Post, {120, 60}}},
     {"/api/v1/admin/site/add",
      {route::admin_site_add, HttpMethod::Post, {120, 60}}},
     {"/api/v1/admin/site/delete",
      {route::admin_site_delete, HttpMethod::Delete, {120, 60}}},
     {"/api/v1/admin/logs", {route::admin_logs, HttpMethod::Get, {120, 60}}},
     {"/api/v1/admin/audit", {route::admin_audit, HttpMethod::Get, {120, 60}}},
     {"/api/v1/admin/comments",
      {route::admin_comments, HttpMethod::Get, {120, 60}}},
     {"/api/v1/admin/comments/approve",
      {route::admin_comment_approve, HttpMethod::Post, {120, 60}}},
     {"/api/v1/admin/comments/delete",
      {route::admin_comment_delete, HttpMethod::Delete, {120, 60}}},
     {"/api/v1/admin/stats", {route::admin_stats, HttpMethod::Get, {120, 60}}},
     {"/api/v1/admin/cache/clear",
      {route::admin_cache_clear, HttpMethod::Post, {120, 60}}},
     {"/api/v1/sites/add", {route::sites_add, HttpMethod::Post, {2, 86400}}},
     {"/api/v1/sites/rename",
      {route::sites_rename, HttpMethod::Post, {10, 86400}}},
     {"/api/v1/sites/react",
      {route::sites_react, HttpMethod::Post, {50, 3600}}},
     {"/api/v1/sites/click", {route::sites_click, HttpMethod::Post, {100, 60}}},
     {"/api/v1/comments", {route::comments_list, HttpMethod::Get, {20, 60}}},
     {"/api/v1/comments/add",
      {route::comments_add, HttpMethod::Post, {3, 3600}}}}
};

fn App::on_event(HttpServerEvent &event, opaque *user) -> void
{
  let const app = static_cast<App *>(user);
  if (app != nullptr && event.kind() == HttpServerEvent::Kind::Request) {
    app->dispatch(event);
  }
}

fn App::dispatch(HttpServerEvent &event) -> void
{
  let const path = event.uri();

  if (let const target = ROUTES.find(path); target != nullptr) {
    /* Dev mode skips the throttle, so the test suite hammers the endpoints. */
    if (!m_config.is_dev_mode &&
        !m_limiter.allow(client_address(event), static_cast<u8>(target->id),
                         target->limit, now_seconds()))
    {
      LOG(Info, "rate limited, route=%d", static_cast<int>(target->id));
      reply_message(event, 429, "Too many requests, slow down");
      return;
    }

    if (target->method != HttpMethod::Get &&
        event.method() != http_method_name(target->method))
    {
      LOG(Info, "method not allowed, uri=%.*s", static_cast<int>(path.count()),
          path.data);
      String message{event.request_allocator()};
      message.append("This endpoint requires ");
      message.append(http_method_name(target->method));
      reply_message(event, 405, message.view());
      return;
    }

    switch (target->id) {
    case route::static_page: serve_static(event); break;
    case route::sites: handle_sites(event); break;
    case route::auth_github_login: handle_login_github(event); break;
    case route::auth_github_callback: handle_github_callback(event); break;
    case route::auth_telegram_callback: handle_telegram_callback(event); break;
    case route::auth_dev: handle_dev_login(event); break;
    case route::auth_logout: handle_logout(event); break;
    case route::config: handle_config(event); break;
    case route::me: handle_me(event); break;
    case route::admin_pending: handle_admin_pending(event); break;
    case route::admin_approve: handle_admin_resolve(event, true); break;
    case route::admin_reject: handle_admin_resolve(event, false); break;
    case route::admin_site: handle_admin_edit(event); break;
    case route::admin_site_add: handle_admin_add(event); break;
    case route::admin_site_delete: handle_admin_delete(event); break;
    case route::admin_logs: handle_admin_logs(event); break;
    case route::admin_audit: handle_admin_audit(event); break;
    case route::admin_comments: handle_admin_comments(event); break;
    case route::admin_comment_approve:
      handle_admin_comment_resolve(event, true);
      break;
    case route::admin_comment_delete:
      handle_admin_comment_resolve(event, false);
      break;
    case route::admin_stats: handle_admin_stats(event); break;
    case route::admin_cache_clear: handle_admin_cache_clear(event); break;
    case route::sites_add: {
      if (let const who = require_account(event); who.has_value())
        handle_user_add(event, who.value());
      break;
    }
    case route::sites_rename: {
      if (let const who = require_account(event); who.has_value())
        handle_user_rename(event, who.value());
      break;
    }
    case route::sites_react: {
      if (let const who = require_account(event); who.has_value())
        handle_user_react(event, who.value());
      break;
    }
    case route::sites_click: handle_site_click(event); break;
    case route::comments_list: handle_comments_list(event); break;
    case route::comments_add: {
      if (let const who = require_account(event); who.has_value())
        handle_comment_post(event, who.value());
      break;
    }
    }
    return;
  }

  if (path.starts_with("/api/v1/")) {
    reply_message(event, 404, "No such endpoint");
    return;
  }

  if (path.find_character('.').has_value()) {
    serve_static(event);
    return;
  }

  StringView rest{};
  let const slug = split_first_segment(path, rest);
  if (!slug.is_empty()) {
    /* The slug hops share one bucket, above the route ids, capped well above a
       human pace. Dev mode skips the throttle. */
    static constexpr u8 SLUG_BUCKET = 255;
    if (!m_config.is_dev_mode &&
        !m_limiter.allow(client_address(event), SLUG_BUCKET, {1000, 60},
                         now_seconds()))
    {
      LOG(Info, "rate limited, slug hop=%.*s", static_cast<int>(slug.count()),
          slug.data);
      reply_message(event, 429, "Too many requests, slow down");
      return;
    }

    StringView remainder{};
    let const first = split_first_segment(rest, remainder);
    let const is_first_data = first == "data";
    let const wants_data = is_first_data || remainder == "data";
    let const step = is_first_data || first.is_empty() ? StringView{} : first;
    handle_navigation(event, slug, step, wants_data);
    return;
  }

  serve_static(event);
}

fn App::write_listing_site(JsonWriter &writer, const site &row,
                           const Maybe<account> &who) -> void
{
  let counts_or = m_store.get_reactions(row.slug.view());
  ArrayList<reaction_count> counts{m_allocator};
  if (!counts_or.is_error()) counts = steal(counts_or.value());

  ArrayList<String> reacted{m_allocator};
  if (who.has_value()) {
    let reacted_or =
        m_store.get_user_reactions(row.slug.view(), who.value().who);
    if (!reacted_or.is_error()) reacted = steal(reacted_or.value());
  }

  i64 click_count = 0;
  bool has_click_count = false;
  if (m_config.is_metrics_enabled) {
    let const clicks_or = m_store.get_click_count(row.slug.view());
    if (!clicks_or.is_error()) {
      click_count = clicks_or.value();
      has_click_count = true;
    }
  }

  write_site_json(writer, row, &counts, who.has_value() ? &reacted : nullptr,
                  has_click_count ? &click_count : nullptr);
}

fn App::handle_sites(HttpServerEvent &event) -> void
{
  let const sites_or = m_store.list_active_sites();
  if (sites_or.is_error()) {
    reply_message(event, 500, sites_or.error().message().view());
    return;
  }

  let const who = current_account(event);
  let const &sites = sites_or.value();

  let reactions_or = m_store.get_all_reactions();
  StringMap<ArrayList<reaction_count>> reactions{event.request_allocator()};
  if (!reactions_or.is_error())
    reactions = steal(reactions_or.value());
  else
    LOG(Info, "site listing reactions unavailable");

  StringMap<ArrayList<String>> user_reactions{event.request_allocator()};
  if (who.has_value()) {
    let user_or = m_store.get_all_user_reactions(who.value().who);
    if (!user_or.is_error())
      user_reactions = steal(user_or.value());
    else
      LOG(Info, "site listing user reactions unavailable");
  }

  StringMap<i64> click_counts{event.request_allocator()};
  if (m_config.is_metrics_enabled) {
    let metrics_or = m_store.get_site_metrics();
    if (!metrics_or.is_error()) {
      let const &metrics = metrics_or.value();
      for (usize i = 0; i < metrics.count(); i++)
        click_counts.set(metrics[i].slug.view(), metrics[i].click_count);
    } else {
      LOG(Info, "site listing click counts unavailable");
    }
  }

  const ArrayList<reaction_count> empty_counts{event.request_allocator()};
  const ArrayList<String> empty_reacted{event.request_allocator()};

  JsonWriter writer{event.request_allocator()};
  writer.array_begin();
  for (usize i = 0; i < sites.count(); i++) {
    let const slug = sites[i].slug.view();

    let const *counts = reactions.find(slug);
    let const *counts_or_empty = counts != nullptr ? counts : &empty_counts;

    const ArrayList<String> *reacted_or_null = nullptr;
    if (who.has_value()) {
      let const *reacted = user_reactions.find(slug);
      reacted_or_null = reacted != nullptr ? reacted : &empty_reacted;
    }

    i64 click_count = 0;
    if (let const *found = click_counts.find(slug); found != nullptr)
      click_count = *found;

    write_site_json(writer, sites[i], counts_or_empty, reacted_or_null,
                    m_config.is_metrics_enabled ? &click_count : nullptr);
  }

  writer.array_end();
  reply_json(event, 200, writer.view());
}

fn App::handle_config(HttpServerEvent &event) -> void
{
  /* The bot id is the numeric token prefix the Telegram widget needs. The
     secret stays on the server. */
  let const telegram_token = m_config.telegram_bot_token.view();
  let const colon_position = telegram_token.find_character(':');
  let const telegram_bot =
      colon_position.has_value()
          ? telegram_token.substring_of_length(0, colon_position.value())
          : StringView{};

  JsonWriter writer{event.request_allocator()};
  writer.object_begin();
  writer.key("is_dev");
  writer.boolean(m_config.is_dev_mode);
  writer.key("github");
  writer.boolean(!m_config.github_client_id.view().is_empty());
  writer.field("telegram_bot", telegram_bot);
  writer.key("metrics_enabled");
  writer.boolean(m_config.is_metrics_enabled);
  writer.field("version", StringView{WR_VERSION_STRING});
  writer.object_end();
  reply_json(event, 200, writer.view());
}

fn App::handle_navigation(HttpServerEvent &event, StringView slug,
                          StringView step, bool wants_data) -> void
{
  /* A browser navigation is served the single-page shell on any failure, so the
     styled error page is rendered. The data form and a textual client keep the
     JSON error. */
  let const wants_page = !wants_data && !prefers_json(event);
  let const sites_or = m_store.list_active_sites();
  if (sites_or.is_error()) {
    if (wants_page) {
      serve_static(event);
      return;
    }
    reply_message(event, 500, sites_or.error().message().view());
    return;
  }
  let const &sites = sites_or.value();
  if (sites.is_empty()) {
    if (wants_page) {
      serve_static(event);
      return;
    }
    reply_message(event, 404, "The ring is empty");
    return;
  }

  usize current = sites.count();
  for (usize i = 0; i < sites.count(); i++) {
    if (sites[i].slug == slug) {
      current = i;
      break;
    }
  }
  if (current == sites.count()) {
    if (wants_page) {
      serve_static(event);
      return;
    }
    reply_message(event, 404, "No such site");
    return;
  }

  enum class nav_step
  {
    next,
    previous,
    random,
  };
  static constexpr StaticStringMap<nav_step, 3> NAV_STEPS{
      {{"next", nav_step::next},
       {"prev", nav_step::previous},
       {"random", nav_step::random}}
  };

  let const count = sites.count();
  usize target = current;
  bool did_hop = false;
  if (let const stepped = NAV_STEPS.find(step); stepped != nullptr) {
    did_hop = true;
    switch (*stepped) {
    case nav_step::next: target = (current + 1) % count; break;
    case nav_step::previous: target = (current + count - 1) % count; break;
    case nav_step::random: {
      usize roll = 0;
      if (random_bytes(&roll, sizeof(roll)).is_error()) roll = 0;
      target = roll % count;
      break;
    }
    }
  }

  if (!wants_data) {
    let const target_slug = sites[target].slug.view();
    if (did_hop && m_config.is_metrics_enabled &&
        m_store.record_hop(target_slug).is_error())
    {
      LOG(Info, "hop record dropped for %.*s",
          static_cast<int>(target_slug.count()), target_slug.data);
    }

    reply_redirect(event, sites[target].url.view());
    return;
  }

  let const previous = (target + count - 1) % count;
  let const next = (target + 1) % count;
  let const who = current_account(event);
  JsonWriter writer{event.request_allocator()};
  writer.object_begin();
  writer.key("previous");
  write_listing_site(writer, sites[previous], who);
  writer.key("current");
  write_listing_site(writer, sites[target], who);
  writer.key("next");
  write_listing_site(writer, sites[next], who);
  writer.object_end();
  reply_json(event, 200, writer.view());
}

fn App::serve_static(HttpServerEvent &event) -> void
{
  let const path = event.uri();

  /* A parent traversal is refused, since a request can never name an asset
     outside the embedded table. */
  if (contains_double_dot(path)) {
    reply_message(event, 400, "Bad path");
    return;
  }

  static constexpr StaticStringMap<const char *, 5> SPA_ROUTES{
      {{"/docs", "/docs.html"},
       {"/", "/index.html"},
       {"/about", "/index.html"},
       {"/panel", "/index.html"},
       {"/admin", "/index.html"}}
  };

  StringView asset_path = path;
  if (let const *mapped = SPA_ROUTES.find(path); mapped != nullptr)
    asset_path = StringView{*mapped};

  if (let const *asset = find_embedded_asset(asset_path); asset != nullptr) {
    reply_text(
        event, 200, content_type_for(asset_path),
        StringView{reinterpret_cast<const char *>(asset->data), asset->size});
    return;
  }

  /* A textual client is given a JSON not-found rather than the page shell. */
  if (prefers_json(event)) {
    reply_message(event, 404, "Not found");
    return;
  }

  if (let const *shell = find_embedded_asset("/index.html"); shell != nullptr) {
    reply_text(
        event, 200, "text/html; charset=utf-8",
        StringView{reinterpret_cast<const char *>(shell->data), shell->size});
    return;
  }
  reply_message(event, 404, "Not found");
}

fn App::emit(HttpServerEvent &event, u16 status, HttpHeaders &headers,
             StringView body) -> void
{
  /* The reply helpers funnel through here, so a pageview and an http error are
     traced at Debug to keep the routine access lines off the default level. The
     auth redirects reply directly and are not traced here. */
  let const method = event.method();
  let const uri = event.uri();
  let const client = client_address(event);

  if (method == "GET") {
    LOG(All, "%.*s %.*s %.*s -> %u", static_cast<int>(client.count()),
        client.data, static_cast<int>(method.count()), method.data,
        static_cast<int>(uri.count()), uri.data, status);
  } else {
    LOG(Debug, "%.*s %.*s %.*s -> %u", static_cast<int>(client.count()),
        client.data, static_cast<int>(method.count()), method.data,
        static_cast<int>(uri.count()), uri.data, status);
  }

  unused(event.reply(status, headers, body, SECURITY_HEADER_BLOCK).is_error());
}

fn App::reply_json(HttpServerEvent &event, u16 status, StringView json) -> void
{
  HttpHeaders headers{event.request_allocator()};
  headers.reserve(2);
  headers.set("Content-Type", "application/json");
  headers.set("Cache-Control", "no-store");
  emit(event, status, headers, json);
}

fn App::reply_text(HttpServerEvent &event, u16 status, StringView content_type,
                   StringView body) -> void
{
  HttpHeaders headers{event.request_allocator()};
  headers.reserve(1);
  headers.set("Content-Type", content_type);
  emit(event, status, headers, body);
}

fn App::reply_redirect(HttpServerEvent &event, StringView location) -> void
{
  HttpHeaders headers{event.request_allocator()};
  headers.reserve(1);
  headers.set("Location", location);
  emit(event, 302, headers, "");
}

fn App::client_address(HttpServerEvent &event) -> StringView
{
  return wr::client_address(event, m_config.is_forwarded_trusted);
}

fn App::record_audit_or_log(HttpServerEvent &event, const identity &actor,
                            StringView action, StringView target,
                            StringView detail) -> void
{
  if (m_store
          .record_audit(actor, client_address(event), action, target, detail,
                        now_seconds())
          .is_error())
    LOG(Info, "audit record dropped, action=%.*s target=%.*s",
        static_cast<int>(action.count()), action.data,
        static_cast<int>(target.count()), target.data);
}

fn App::reply_message(HttpServerEvent &event, u16 status, StringView message)
    -> void
{
  JsonWriter writer{event.request_allocator()};
  writer.object_begin();
  writer.field("message", message);
  writer.object_end();
  reply_json(event, status, writer.view());
}

} // namespace wr
