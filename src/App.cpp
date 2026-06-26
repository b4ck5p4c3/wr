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

fn write_site_json(JsonWriter &writer, const site &row,
                   const ArrayList<reaction_count> *reactions,
                   const ArrayList<String> *reacted,
                   StringView owner_display_name, StringView owner_username)
    -> void
{
  writer.object_begin();
  writer.field("slug", row.slug.view());
  writer.field("name", row.name.view());
  writer.field("url", row.url.view());
  writer.field("description", row.description.view());
  writer.key("created_at");
  writer.number(row.created_at);
  writer.field("owner", row.owner.view());
  writer.field("owner_display_name", owner_display_name);
  writer.field("owner_username", owner_username);

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

  writer.object_end();
}

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

  if (path == "/sites") {
    handle_sites(event);
    return;
  }

  if (path.starts_with("/auth/github/callback")) {
    handle_github_callback(event);
    return;
  }
  if (path.starts_with("/auth/github")) {
    handle_login_github(event);
    return;
  }
  if (path.starts_with("/auth/telegram/callback")) {
    handle_telegram_callback(event);
    return;
  }
  if (path.starts_with("/auth/dev")) {
    handle_dev_login(event);
    return;
  }
  if (path.starts_with("/auth/logout")) {
    /* Logout deletes the session, so it requires POST like the other mutations
       and a cross-site GET cannot end a signed-in session. */
    if (event.method() != "POST") {
      reply_message(event, 405, "This endpoint requires POST");
      return;
    }
    handle_logout(event);
    return;
  }

  if (path.starts_with("/api/v1/")) {
    enum class api_route
    {
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
      sites_add,
      sites_rename,
      sites_react,
      comments_list,
      comments_add,
    };
    enum class request_method : u8
    {
      read,
      post,
      del,
    };
    struct api_endpoint
    {
      api_route route;
      request_method method;
    };
    static constexpr StaticStringMap<api_endpoint, 18> API_ROUTES{
        {{"/api/v1/config", {api_route::config, request_method::read}},
         {"/api/v1/me", {api_route::me, request_method::read}},
         {"/api/v1/admin/pending",
          {api_route::admin_pending, request_method::read}},
         {"/api/v1/admin/pending/approve",
          {api_route::admin_approve, request_method::post}},
         {"/api/v1/admin/pending/reject",
          {api_route::admin_reject, request_method::del}},
         {"/api/v1/admin/site", {api_route::admin_site, request_method::post}},
         {"/api/v1/admin/site/add",
          {api_route::admin_site_add, request_method::post}},
         {"/api/v1/admin/site/delete",
          {api_route::admin_site_delete, request_method::del}},
         {"/api/v1/admin/logs", {api_route::admin_logs, request_method::read}},
         {"/api/v1/admin/audit",
          {api_route::admin_audit, request_method::read}},
         {"/api/v1/admin/comments",
          {api_route::admin_comments, request_method::read}},
         {"/api/v1/admin/comments/approve",
          {api_route::admin_comment_approve, request_method::post}},
         {"/api/v1/admin/comments/delete",
          {api_route::admin_comment_delete, request_method::del}},
         {"/api/v1/sites/add", {api_route::sites_add, request_method::post}},
         {"/api/v1/sites/rename",
          {api_route::sites_rename, request_method::post}},
         {"/api/v1/sites/react",
          {api_route::sites_react, request_method::post}},
         {"/api/v1/comments", {api_route::comments_list, request_method::read}},
         {"/api/v1/comments/add",
          {api_route::comments_add, request_method::post}}}
    };

    let const endpoint = API_ROUTES.find(path);
    if (endpoint == nullptr) {
      reply_message(event, 404, "No such endpoint");
      return;
    }

    /* The required method is data in the route table. A write is reached only
       through POST or DELETE, so a cross-site GET cannot drive it on a
       signed-in browser. */
    if (endpoint->method == request_method::post && event.method() != "POST") {
      reply_message(event, 405, "This endpoint requires POST");
      return;
    }
    if (endpoint->method == request_method::del && event.method() != "DELETE") {
      reply_message(event, 405, "This endpoint requires DELETE");
      return;
    }

    switch (endpoint->route) {
    case api_route::config: handle_config(event); break;
    case api_route::me: handle_me(event); break;
    case api_route::admin_pending: handle_admin_pending(event); break;
    case api_route::admin_approve: handle_admin_resolve(event, true); break;
    case api_route::admin_reject: handle_admin_resolve(event, false); break;
    case api_route::admin_site: handle_admin_edit(event); break;
    case api_route::admin_site_add: handle_admin_add(event); break;
    case api_route::admin_site_delete: handle_admin_delete(event); break;
    case api_route::admin_logs: handle_admin_logs(event); break;
    case api_route::admin_audit: handle_admin_audit(event); break;
    case api_route::admin_comments: handle_admin_comments(event); break;
    case api_route::admin_comment_approve:
      handle_admin_comment_resolve(event, true);
      break;
    case api_route::admin_comment_delete:
      handle_admin_comment_resolve(event, false);
      break;
    case api_route::sites_add: {
      let const who = current_account(event);
      if (who.has_value())
        handle_user_add(event, who.value());
      else
        reply_message(event, 401, "Not signed in");
      break;
    }
    case api_route::sites_rename: {
      let const who = current_account(event);
      if (who.has_value())
        handle_user_rename(event, who.value());
      else
        reply_message(event, 401, "Not signed in");
      break;
    }
    case api_route::sites_react: {
      let const who = current_account(event);
      if (who.has_value())
        handle_user_react(event, who.value());
      else
        reply_message(event, 401, "Not signed in");
      break;
    }
    case api_route::comments_list: handle_comments_list(event); break;
    case api_route::comments_add: {
      let const who = current_account(event);
      if (who.has_value())
        handle_comment_post(event, who.value());
      else
        reply_message(event, 401, "Not signed in");
      break;
    }
    }
    return;
  }

  if (path == "/" || path == "/about" || path == "/panel" || path == "/admin" ||
      path == "/docs" || path.starts_with("/assets/"))
  {
    serve_static(event);
    return;
  }

  if (path.find_character('.').has_value()) {
    serve_static(event);
    return;
  }

  StringView rest{};
  let const slug = split_first_segment(path, rest);
  if (!slug.is_empty()) {
    StringView remainder{};
    let const first = split_first_segment(rest, remainder);
    let const wants_data = first == "data" || remainder == "data";
    let const step = first == "data" || first.is_empty() ? StringView{} : first;
    handle_navigation(event, slug, step, wants_data);
    return;
  }

  serve_static(event);
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
  JsonWriter writer{m_allocator};
  writer.array_begin();
  for (usize i = 0; i < sites.count(); i++) {
    let const counts_or = m_store.get_reactions(sites[i].slug.view());
    ArrayList<reaction_count> counts{m_allocator};
    if (!counts_or.is_error()) counts = counts_or.value().clone();

    ArrayList<String> reacted{m_allocator};
    if (who.has_value()) {
      let const reacted_or = m_store.get_user_reactions(
          sites[i].slug.view(), who.value().identity.view());
      if (!reacted_or.is_error()) reacted = reacted_or.value().clone();
    }

    let const account = m_store.find_account(sites[i].owner.view());
    let const has_account = !account.is_error() && account.value().has_value();
    let const owner_name = has_account
                               ? account.value().value().display_name.view()
                               : StringView{};
    let const owner_handle =
        has_account ? account.value().value().username.view() : StringView{};

    write_site_json(writer, sites[i], &counts,
                    who.has_value() ? &reacted : nullptr, owner_name,
                    owner_handle);
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
          : telegram_token;

  JsonWriter writer{m_allocator};
  writer.object_begin();
  writer.key("is_dev");
  writer.boolean(m_config.is_dev_mode);
  writer.key("github");
  writer.boolean(!m_config.github_client_id.view().is_empty());
  writer.field("telegram_bot",
               telegram_token.is_empty() ? StringView{} : telegram_bot);
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
    prev,
    random,
  };
  static constexpr StaticStringMap<nav_step, 3> NAV_STEPS{
      {{"next", nav_step::next},
       {"prev", nav_step::prev},
       {"random", nav_step::random}}
  };

  let const count = sites.count();
  usize target = current;
  if (let const stepped = NAV_STEPS.find(step); stepped != nullptr) {
    switch (*stepped) {
    case nav_step::next: target = (current + 1) % count; break;
    case nav_step::prev: target = (current + count - 1) % count; break;
    case nav_step::random: {
      usize roll = 0;
      if (random_bytes(&roll, sizeof(roll)).is_error()) roll = 0;
      target = roll % count;
      break;
    }
    }
  }

  if (!wants_data) {
    reply_redirect(event, sites[target].url.view());
    return;
  }

  let const previous = (target + count - 1) % count;
  let const next = (target + 1) % count;
  JsonWriter writer{m_allocator};
  writer.object_begin();
  writer.key("previous");
  write_site_json(writer, sites[previous]);
  writer.key("current");
  write_site_json(writer, sites[target]);
  writer.key("next");
  write_site_json(writer, sites[next]);
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

  StringView asset_path = path;
  if (path == "/docs") {
    asset_path = "/docs.html";
  } else if (path == "/" || path == "/about" || path == "/panel" ||
             path == "/admin")
  {
    asset_path = "/index.html";
  }

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

fn App::emit(HttpServerEvent &event, u16 status, const HttpHeaders &headers,
             StringView body) -> void
{
  /* The reply helpers funnel through here, so a pageview and an http error are
     traced at Debug to keep the routine access lines off the default level. The
     auth redirects reply directly and are not traced here. */
  let const method = event.method();
  let const uri = event.uri();
  let const forwarded = event.request_headers().get("x-forwarded-for");
  let const client =
      forwarded.has_value() ? forwarded.value() : event.client_ip();
  LOG(Debug, "%.*s %.*s %.*s -> %u", static_cast<int>(client.count()),
      client.data, static_cast<int>(method.count()), method.data,
      static_cast<int>(uri.count()), uri.data, status);

  unused(event.reply(status, headers, body).is_error());
}

fn App::reply_json(HttpServerEvent &event, u16 status, StringView json) -> void
{
  HttpHeaders headers{m_allocator};
  headers.set("Content-Type", "application/json");
  emit(event, status, headers, json);
}

fn App::reply_text(HttpServerEvent &event, u16 status, StringView content_type,
                   StringView body) -> void
{
  HttpHeaders headers{m_allocator};
  headers.set("Content-Type", content_type);
  emit(event, status, headers, body);
}

fn App::reply_redirect(HttpServerEvent &event, StringView location) -> void
{
  HttpHeaders headers{m_allocator};
  headers.set("Location", location);
  emit(event, 302, headers, "");
}

fn App::reply_message(HttpServerEvent &event, u16 status, StringView message)
    -> void
{
  JsonWriter writer{m_allocator};
  writer.object_begin();
  writer.field("message", message);
  writer.object_end();
  reply_json(event, status, writer.view());
}

} // namespace wr
