#include "App.hpp"

#include "Http.hpp"
#include "Path.hpp"
#include "StaticStringMap.hpp"
#include "Trace.hpp"

namespace wr {

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

fn read_file(Allocator allocator, Path path) -> Maybe<String>
{
  std::FILE *file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) return None;
  defer { std::fclose(file); };

  String contents{allocator};
  char buffer[8192];
  loop
  {
    let const read_count = std::fread(buffer, 1, sizeof(buffer), file);
    if (read_count > 0) contents.append(StringView{buffer, read_count});
    if (read_count < sizeof(buffer)) break;
  }
  return Maybe<String>{steal(contents)};
}

/* The first path segment after an optional leading slash, and the remainder. */
fn split_first_segment(StringView path, StringView &rest) -> StringView
{
  usize start = 0;
  if (path.count() > 0 && path[0] == '/') start = 1;
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

fn write_site_json(JsonWriter &writer, const site &row) -> void
{
  writer.object_begin();
  writer.field("slug", row.slug.view());
  writer.field("name", row.name.view());
  writer.field("url", row.url.view());
  writer.field("favicon", row.favicon.view());
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

  if (path.starts_with("/api/")) {
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
      sites_add,
      sites_rename,
    };
    struct api_endpoint
    {
      api_route route;
      bool is_mutation;
    };
    static constexpr StaticStringMap<api_endpoint, 10> API_ROUTES{
        {{"/api/config", {api_route::config, false}},
         {"/api/me", {api_route::me, false}},
         {"/api/admin/pending", {api_route::admin_pending, false}},
         {"/api/admin/pending/approve", {api_route::admin_approve, true}},
         {"/api/admin/pending/reject", {api_route::admin_reject, true}},
         {"/api/admin/site", {api_route::admin_site, true}},
         {"/api/admin/site/add", {api_route::admin_site_add, true}},
         {"/api/admin/site/delete", {api_route::admin_site_delete, true}},
         {"/api/sites/add", {api_route::sites_add, true}},
         {"/api/sites/rename", {api_route::sites_rename, true}}}
    };

    let const endpoint = API_ROUTES.find(path);
    if (endpoint == nullptr) {
      reply_message(event, 404, "No such endpoint");
      return;
    }

    /* The required method is data in the route table, so a mutation is reached
       only through POST and a cross-site GET cannot drive it on a signed-in
       browser. */
    if (endpoint->is_mutation && event.method() != "POST") {
      reply_message(event, 405, "This endpoint requires POST");
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
    }
    return;
  }

  /* The SPA owns the bare app routes and any asset, so they are served as
     static files before a top-level segment is read as a navigation slug. */
  if (path == "/" || path == "/about" || path == "/panel" || path == "/admin" ||
      path == "/docs" || path.starts_with("/assets/"))
  {
    serve_static(event);
    return;
  }

  /* An asset path carries a file extension, so it is served statically rather
     than read as a navigation slug. */
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

  let const &sites = sites_or.value();
  JsonWriter writer{m_allocator};
  writer.array_begin();
  for (usize i = 0; i < sites.count(); i++)
    write_site_json(writer, sites[i]);
  writer.array_end();
  reply_json(event, 200, writer.view());
}

fn App::handle_config(HttpServerEvent &event) -> void
{
  /* The bot id is the numeric prefix of the token, which is what the Telegram
     widget needs, while the secret stays on the server. */
  let const telegram_token = m_config.telegram_bot_token.view();
  usize colon = 0;
  while (colon < telegram_token.count() && telegram_token[colon] != ':')
    colon++;
  let const telegram_bot = telegram_token.substring_of_length(0, colon);

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
     styled error page is rendered. The data form keeps the JSON error for an
     API consumer. */
  let const sites_or = m_store.list_active_sites();
  if (sites_or.is_error()) {
    if (!wants_data) {
      serve_static(event);
      return;
    }
    reply_message(event, 500, sites_or.error().message().view());
    return;
  }
  let const &sites = sites_or.value();
  if (sites.is_empty()) {
    if (!wants_data) {
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
    if (!wants_data) {
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
    case nav_step::random: target = static_cast<usize>(std::rand()) % count;
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

  /* A parent traversal is refused so a request can never escape the web root.
   */
  if (contains_double_dot(path)) {
    reply_message(event, 400, "Bad path");
    return;
  }

  String file_path{m_allocator};
  file_path.append(m_config.web_root.view());
  if (path == "/docs")
    file_path.append("/docs.html");
  else if (path == "/" || path == "/about" || path == "/panel" ||
           path == "/admin")
    file_path.append("/index.html");
  else
    file_path.append(path);

  let contents = read_file(m_allocator, Path{file_path});
  if (contents.has_value()) {
    reply_text(event, 200, content_type_for(file_path.view()),
               contents.value().view());
    return;
  }

  /* The single-page app handles its own routes, so an unknown path is served
     the shell. */
  String index_path{m_allocator};
  index_path.append(m_config.web_root.view());
  index_path.append("/index.html");
  let shell = read_file(m_allocator, Path{index_path});
  if (shell.has_value()) {
    reply_text(event, 200, "text/html; charset=utf-8", shell.value().view());
    return;
  }
  reply_message(event, 404, "Not found");
}

fn App::emit(HttpServerEvent &event, u16 status, const HttpHeaders &headers,
             StringView body) -> void
{
  /* Every response funnels through here, so the access line traces each
     pageview and every http error at the default verbosity. */
  let const method = String{m_allocator, event.method()};
  let const uri = String{m_allocator, event.uri()};
  LOG(Info, "%s %s -> %u", method.c_str(), uri.c_str(), status);

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
