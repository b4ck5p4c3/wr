#include "App.hpp"
#include "Http.hpp"
#include "Json.hpp"
#include "Trace.hpp"

namespace wr {

namespace {

/* A slug is a short lowercase path segment, so it keys the navigation without
   colliding with a reserved route or escaping a path. */
fn is_valid_slug(StringView slug) -> bool
{
  if (slug.is_empty() || slug.count() > 64) return false;
  for (usize i = 0; i < slug.count(); i++) {
    let const c = slug[i];
    let const is_allowed =
        (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    if (!is_allowed) return false;
  }
  let const reserved = {"sites", "auth",  "api",    "about",
                        "panel", "admin", "assets", "index"};
  for (const char *word : reserved)
    if (slug == word) return false;
  return true;
}

/* A site url is rendered as a link and served as a navigation redirect, so only
   an http or https scheme is accepted, which keeps a javascript or data url out
   of the ring. */
fn is_valid_site_url(StringView url) -> bool
{
  return url.starts_with("http://") || url.starts_with("https://");
}

fn write_panel_site(JsonWriter &writer, const site &row) -> void
{
  writer.object_begin();
  writer.field("slug", row.slug.view());
  writer.field("name", row.name.view());
  writer.field("url", row.url.view());
  writer.field("favicon", row.favicon.view());
  writer.key("is_reachable");
  writer.boolean(row.is_reachable);
  writer.key("last_seen_at");
  writer.number(row.last_seen_at);
  writer.field("owner", row.owner.view());
  writer.object_end();
}

} // namespace

fn App::handle_me(HttpServerEvent &event) -> void
{
  let const who = current_account(event);
  if (!who.has_value()) {
    reply_message(event, 401, "Not signed in");
    return;
  }
  let const &me = who.value();

  let const sites_or = me.is_admin
                           ? m_store.list_all_sites()
                           : m_store.list_sites_for_owner(me.identity.view());
  if (sites_or.is_error()) {
    reply_message(event, 500, sites_or.error().message().view());
    return;
  }

  JsonWriter writer{m_allocator};
  writer.object_begin();
  writer.field("identity", me.identity.view());
  writer.field("display_name", me.display_name.view());
  writer.key("is_admin");
  writer.boolean(me.is_admin);
  writer.key("sites");
  writer.array_begin();
  for (usize i = 0; i < sites_or.value().count(); i++)
    write_panel_site(writer, sites_or.value()[i]);
  writer.array_end();
  writer.object_end();
  reply_json(event, 200, writer.view());
}

fn App::handle_user_add(HttpServerEvent &event, const account &who) -> void
{
  let const body = event.body();
  let const slug = json_get_string(m_allocator, body, "slug");
  let const name = json_get_string(m_allocator, body, "name");
  let const url = json_get_string(m_allocator, body, "url");
  let const favicon = json_get_string(m_allocator, body, "favicon");
  if (!slug.has_value() || !name.has_value() || !url.has_value()) {
    reply_message(event, 400, "A slug, a name, and a url are required");
    return;
  }
  if (!is_valid_slug(slug.value().view())) {
    reply_message(event, 400, "The slug may use only a-z, 0-9, and a dash");
    return;
  }
  if (!is_valid_site_url(url.value().view())) {
    reply_message(event, 400, "The url must start with http:// or https://");
    return;
  }

  JsonWriter payload{m_allocator};
  payload.object_begin();
  payload.field("name", name.value().view());
  payload.field("url", url.value().view());
  payload.field("favicon", favicon.has_value() ? favicon.value().view() : "");
  payload.object_end();

  let const recorded =
      m_store.add_pending("add", who.identity.view(), slug.value().view(),
                          payload.view(), now_seconds());
  if (recorded.is_error()) {
    reply_message(event, 500, recorded.error().message().view());
    return;
  }
  reply_message(event, 200, "Your site was submitted for review");
}

fn App::handle_user_rename(HttpServerEvent &event, const account &who) -> void
{
  let const body = event.body();
  let const slug = json_get_string(m_allocator, body, "slug");
  let const name = json_get_string(m_allocator, body, "name");
  if (!slug.has_value() || !name.has_value()) {
    reply_message(event, 400, "A slug and a name are required");
    return;
  }

  let const owned = m_store.find_site(slug.value().view());
  if (owned.is_error() || !owned.value().has_value() ||
      owned.value().value().owner != who.identity)
  {
    reply_message(event, 403, "That site is not yours");
    return;
  }

  let const recorded =
      m_store.add_pending("rename", who.identity.view(), slug.value().view(),
                          name.value().view(), now_seconds());
  if (recorded.is_error()) {
    reply_message(event, 500, recorded.error().message().view());
    return;
  }
  reply_message(event, 200, "Your rename was submitted for review");
}

fn App::handle_admin_edit(HttpServerEvent &event) -> void
{
  let const who = current_account(event);
  if (!who.has_value() || !who.value().is_admin) {
    reply_message(event, 403, "Admins only");
    return;
  }

  let const body = event.body();
  let const slug = json_get_string(m_allocator, body, "slug");
  let const name = json_get_string(m_allocator, body, "name");
  let const url = json_get_string(m_allocator, body, "url");
  let const favicon = json_get_string(m_allocator, body, "favicon");
  if (!slug.has_value() || !name.has_value() || !url.has_value()) {
    reply_message(event, 400, "A slug, a name, and a url are required");
    return;
  }
  if (!is_valid_slug(slug.value().view())) {
    reply_message(event, 400, "The slug may use only a-z, 0-9, and a dash");
    return;
  }
  if (!is_valid_site_url(url.value().view())) {
    reply_message(event, 400, "The url must start with http:// or https://");
    return;
  }

  site row{};
  row.slug = String{m_allocator, slug.value().view()};
  row.name = String{m_allocator, name.value().view()};
  row.url = String{m_allocator, url.value().view()};
  row.favicon =
      String{m_allocator, favicon.has_value() ? favicon.value().view() : ""};
  row.created_at = now_seconds();

  let const stored = m_store.upsert_site(row);
  if (stored.is_error()) {
    reply_message(event, 500, stored.error().message().view());
    return;
  }
  reply_message(event, 200, "Saved");
}

fn App::handle_admin_pending(HttpServerEvent &event) -> void
{
  let const who = current_account(event);
  if (!who.has_value() || !who.value().is_admin) {
    reply_message(event, 403, "Admins only");
    return;
  }

  let const actions_or = m_store.list_pending();
  if (actions_or.is_error()) {
    reply_message(event, 500, actions_or.error().message().view());
    return;
  }

  JsonWriter writer{m_allocator};
  writer.array_begin();
  let const &actions = actions_or.value();
  for (usize i = 0; i < actions.count(); i++) {
    writer.object_begin();
    writer.key("id");
    writer.number(actions[i].id);
    writer.field("kind", actions[i].kind.view());
    writer.field("owner", actions[i].owner.view());
    writer.field("target_slug", actions[i].target_slug.view());
    writer.field("payload", actions[i].payload.view());
    writer.object_end();
  }
  writer.array_end();
  reply_json(event, 200, writer.view());
}

fn App::handle_admin_resolve(HttpServerEvent &event, bool should_approve)
    -> void
{
  let const who = current_account(event);
  if (!who.has_value() || !who.value().is_admin) {
    reply_message(event, 403, "Admins only");
    return;
  }

  let const id_or = json_get_number(m_allocator, event.body(), "id");
  if (!id_or.has_value()) {
    reply_message(event, 400, "An id is required");
    return;
  }
  let const id = id_or.value();

  let const found = m_store.find_pending(id);
  if (found.is_error() || !found.value().has_value()) {
    reply_message(event, 404, "No such action");
    return;
  }
  let const &action = found.value().value();

  if (should_approve) {
    if (action.kind == "add") {
      site row{};
      row.slug = String{m_allocator, action.target_slug.view()};
      row.name = json_get_string(m_allocator, action.payload.view(), "name")
                     .value_or(String{m_allocator});
      row.url = json_get_string(m_allocator, action.payload.view(), "url")
                    .value_or(String{m_allocator});
      row.favicon =
          json_get_string(m_allocator, action.payload.view(), "favicon")
              .value_or(String{m_allocator});
      row.owner = String{m_allocator, action.owner.view()};
      row.created_at = now_seconds();
      let const stored = m_store.upsert_site(row);
      if (stored.is_error()) {
        reply_message(event, 500, stored.error().message().view());
        return;
      }
    } else if (action.kind == "rename") {
      let const renamed =
          m_store.rename_site(action.target_slug.view(), action.payload.view());
      if (renamed.is_error()) {
        reply_message(event, 500, renamed.error().message().view());
        return;
      }
    }
  }

  let const resolved =
      m_store.set_pending_status(id, should_approve ? "approved" : "rejected");
  if (resolved.is_error()) {
    reply_message(event, 500, resolved.error().message().view());
    return;
  }
  reply_message(event, 200, should_approve ? "Approved" : "Rejected");
}

} // namespace wr
