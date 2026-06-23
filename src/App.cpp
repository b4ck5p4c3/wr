#include "App.hpp"

#include "Http.hpp"
#include "Trace.hpp"

#include <ctime>

namespace wr {

namespace {

fn find_substring(StringView haystack, StringView needle) -> Maybe<usize>
{
  if (needle.count() == 0 || needle.count() > haystack.count()) return None;
  for (usize i = 0; i + needle.count() <= haystack.count(); i++) {
    if (haystack.substring_of_length(i, needle.count()) == needle) return i;
  }
  return None;
}

fn percent_decode(Allocator allocator, StringView text) -> String
{
  String out{allocator};
  for (usize i = 0; i < text.count(); i++) {
    let const c = text[i];
    if (c == '+') {
      out.push(' ');
    } else if (c == '%' && i + 2 < text.count()) {
      let const hex = [](char digit) -> int {
        if (digit >= '0' && digit <= '9') return digit - '0';
        if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
        if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
        return -1;
      };
      let const high = hex(text[i + 1]);
      let const low = hex(text[i + 2]);
      if (high >= 0 && low >= 0) {
        out.push(static_cast<char>((high << 4) | low));
        i += 2;
      } else {
        out.push(c);
      }
    } else {
      out.push(c);
    }
  }
  return out;
}

fn content_type_for(StringView path) -> StringView
{
  let const ends_with = [&](StringView suffix) -> bool {
    if (path.count() < suffix.count()) return false;
    return path.substring(path.count() - suffix.count()) == suffix;
  };
  if (ends_with(".html")) return "text/html; charset=utf-8";
  if (ends_with(".js")) return "text/javascript; charset=utf-8";
  if (ends_with(".css")) return "text/css; charset=utf-8";
  if (ends_with(".json")) return "application/json";
  if (ends_with(".svg")) return "image/svg+xml";
  if (ends_with(".png")) return "image/png";
  if (ends_with(".ico")) return "image/x-icon";
  if (ends_with(".woff2")) return "font/woff2";
  return "application/octet-stream";
}

fn read_file(Allocator allocator, const char *path) -> Maybe<String>
{
  std::FILE *file = std::fopen(path, "rb");
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
    if (path[i] == '.' && path[i + 1] == '.') return true;
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
    if (key == name && equals < pair.count())
      return Maybe<String>{
          percent_decode(allocator, pair.substring(equals + 1))};

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
    if (pair.substring_of_length(0, equals) == name && equals < pair.count())
      return pair.substring(equals + 1);

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

fn now_seconds() -> i64 { return static_cast<i64>(std::time(nullptr)); }

fn json_string_field(Allocator allocator, StringView json, StringView key)
    -> Maybe<String>
{
  String needle{allocator};
  needle.push('"');
  needle.append(key);
  needle.push('"');
  let const at = find_substring(json, needle.view());
  if (!at.has_value()) return None;

  usize i = at.value() + needle.count();
  while (i < json.count() && json[i] != ':')
    i++;
  i++;
  while (i < json.count() && (json[i] == ' ' || json[i] == '\t'))
    i++;
  if (i >= json.count() || json[i] != '"') return None;
  i++;

  String value{allocator};
  while (i < json.count() && json[i] != '"') {
    if (json[i] == '\\' && i + 1 < json.count()) {
      i++;
      value.push(json[i] == 'n' ? '\n' : json[i]);
    } else {
      value.push(json[i]);
    }
    i++;
  }
  return Maybe<String>{steal(value)};
}

fn json_number_field(Allocator allocator, StringView json, StringView key)
    -> Maybe<String>
{
  String needle{allocator};
  needle.push('"');
  needle.append(key);
  needle.push('"');
  let const at = find_substring(json, needle.view());
  if (!at.has_value()) return None;

  usize i = at.value() + needle.count();
  while (i < json.count() && json[i] != ':')
    i++;
  i++;
  while (i < json.count() && (json[i] == ' ' || json[i] == '\t'))
    i++;

  String value{allocator};
  while (i < json.count() && json[i] >= '0' && json[i] <= '9') {
    value.push(json[i]);
    i++;
  }
  if (value.is_empty()) return None;
  return Maybe<String>{steal(value)};
}

fn App::on_event(HttpServerEvent &event, opaque *user) -> void
{
  let const app = static_cast<App *>(user);
  if (app != nullptr && event.kind() == HttpServerEvent::Kind::Request)
    app->dispatch(event);
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
  if (path.starts_with("/auth/logout")) {
    handle_logout(event);
    return;
  }

  if (path.starts_with("/api/")) {
    if (path == "/api/me") {
      handle_me(event);
    } else if (path == "/api/admin/pending") {
      handle_admin_pending(event);
    } else if (path == "/api/admin/pending/approve") {
      handle_admin_resolve(event, true);
    } else if (path == "/api/admin/pending/reject") {
      handle_admin_resolve(event, false);
    } else if (path == "/api/admin/site") {
      handle_admin_edit(event);
    } else if (path == "/api/sites/add") {
      let const who = current_account(event);
      if (who.has_value())
        handle_user_add(event, who.value());
      else
        reply_message(event, 401, "Not signed in");
    } else if (path == "/api/sites/rename") {
      let const who = current_account(event);
      if (who.has_value())
        handle_user_rename(event, who.value());
      else
        reply_message(event, 401, "Not signed in");
    } else {
      reply_message(event, 404, "No such endpoint");
    }
    return;
  }

  /* The SPA owns the bare app routes and any asset, so they are served as
     static files before a top-level segment is read as a navigation slug. */
  if (path == "/" || path == "/about" || path == "/panel" || path == "/admin" ||
      path.starts_with("/assets/"))
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

fn App::handle_navigation(HttpServerEvent &event, StringView slug,
                          StringView step, bool wants_data) -> void
{
  let const sites_or = m_store.list_active_sites();
  if (sites_or.is_error()) {
    reply_message(event, 500, sites_or.error().message().view());
    return;
  }
  let const &sites = sites_or.value();
  if (sites.is_empty()) {
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
    reply_message(event, 404, "No such site");
    return;
  }

  let const count = sites.count();
  usize target = current;
  if (step == "next") {
    target = (current + 1) % count;
  } else if (step == "prev") {
    target = (current + count - 1) % count;
  } else if (step == "random") {
    target = static_cast<usize>(std::rand()) % count;
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
  if (path == "/" || path == "/about" || path == "/panel" || path == "/admin")
    file_path.append("/index.html");
  else
    file_path.append(path);

  let contents = read_file(m_allocator, file_path.c_str());
  if (contents.has_value()) {
    reply_text(event, 200, content_type_for(file_path.view()),
               contents.value().view());
    return;
  }

  /* The single-page app handles its own routes, so an unknown path falls back
     to the shell rather than a 404. */
  String index_path{m_allocator};
  index_path.append(m_config.web_root.view());
  index_path.append("/index.html");
  let shell = read_file(m_allocator, index_path.c_str());
  if (shell.has_value()) {
    reply_text(event, 200, "text/html; charset=utf-8", shell.value().view());
    return;
  }
  reply_message(event, 404, "Not found");
}

fn App::reply_json(HttpServerEvent &event, u16 status, StringView json) -> void
{
  HttpHeaders headers{m_allocator};
  headers.set("Content-Type", "application/json");
  unused(event.reply(status, headers, json).is_error());
}

fn App::reply_text(HttpServerEvent &event, u16 status, StringView content_type,
                   StringView body) -> void
{
  HttpHeaders headers{m_allocator};
  headers.set("Content-Type", content_type);
  unused(event.reply(status, headers, body).is_error());
}

fn App::reply_redirect(HttpServerEvent &event, StringView location) -> void
{
  HttpHeaders headers{m_allocator};
  headers.set("Location", location);
  unused(event.reply(302, headers, "").is_error());
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
