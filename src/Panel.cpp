#include "App.hpp"
#include "Http.hpp"
#include "Json.hpp"
#include "StaticStringMap.hpp"
#include "Trace.hpp"

namespace wr {

namespace {

/* A slug is a short lowercase path segment, so it keys the navigation without
   colliding with a reserved route or escaping a path. */
fn is_valid_slug(StringView slug) -> bool
{
  if (slug.is_empty() || slug.count() > 64) {
    return false;
  }
  for (usize i = 0; i < slug.count(); i++) {
    let const c = slug[i];
    let const is_allowed =
        (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    if (!is_allowed) return false;
  }
  static constexpr StaticStringMap<bool, 8> RESERVED_SLUGS{
      {{"sites", true},
       {"auth", true},
       {"api", true},
       {"about", true},
       {"panel", true},
       {"admin", true},
       {"assets", true},
       {"index", true}}
  };
  return RESERVED_SLUGS.find(slug) == nullptr;
}

/* A site url is rendered as a link and served as a navigation redirect, so only
   an http or https scheme is accepted, which keeps a javascript or data url out
   of the ring. */
fn is_valid_site_url(StringView url) -> bool
{
  return url.starts_with("http://") || url.starts_with("https://");
}

/* The audit trail prefers the readable display name, and the opaque identity is
   the fallback when the account carries no name. */
fn actor_label(const account &who) -> StringView
{
  return who.display_name.view().is_empty() ? who.identity.view()
                                            : who.display_name.view();
}

/* Behind a reverse proxy the socket peer is the proxy, so a forwarded header
   holds the real client address. The first hop of x-forwarded-for is taken,
   then x-real-ip, and the socket peer is the fallback. */
fn client_address(HttpServerEvent &event) -> StringView
{
  let const &headers = event.request_headers();
  if (let const forwarded = headers.get("x-forwarded-for");
      forwarded.has_value())
  {
    let const value = forwarded.value();
    let const comma = value.find_character(',');
    return comma.has_value() ? value.substring_of_length(0, comma.value())
                             : value;
  }
  if (let const real_ip = headers.get("x-real-ip"); real_ip.has_value())
    return real_ip.value();
  return event.client_ip();
}

fn write_panel_site(JsonWriter &writer, const site &row,
                    const ArrayList<i64> &uptime) -> void
{
  writer.object_begin();
  writer.field("slug", row.slug.view());
  writer.field("name", row.name.view());
  writer.field("url", row.url.view());
  writer.field("description", row.description.view());
  writer.key("is_reachable");
  writer.boolean(row.is_reachable);
  writer.key("last_seen_at");
  writer.number(row.last_seen_at);
  writer.key("uptime");
  writer.array_begin();
  for (usize i = 0; i < uptime.count(); i++)
    writer.number(uptime[i]);
  writer.array_end();
  writer.field("owner", row.owner.view());
  writer.object_end();
}

struct site_input
{
  StringView slug;
  StringView name;
  StringView url;
  StringView description;
};

/* Read and validate the add or edit fields out of a parsed body. The returned
   views alias the document, which the caller keeps alive. A non-null return is
   the 400 message to reply with. */
fn validate_site_input(const Json &document, site_input &out) -> const char *
{
  let const slug = document["slug"].to<StringView>();
  let const name = document["name"].to<StringView>();
  let const url = document["url"].to<StringView>();
  let const description = document["description"].to<StringView>();

  if (!slug.has_value() || !name.has_value() || !url.has_value())
    return "A slug, a name, and a url are required";
  if (!is_valid_slug(slug.value()))
    return "The slug may use only a-z, 0-9, and a dash";
  if (!is_valid_site_url(url.value()))
    return "The url must start with http:// or https://";

  out.description =
      description.has_value() ? description.value() : StringView{};
  if (out.description.count() > 280)
    return "The description must be 280 characters or fewer";

  out.slug = slug.value();
  out.name = name.value();
  out.url = url.value();
  return nullptr;
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

  /* Admins see all sites in the panel; users see only their own. The public
     landing page uses list_active_sites regardless of role, so admin-owned
     sites appear there once the liveness sweep marks them reachable. */
  let const sites_or = me.is_admin
                           ? m_store.list_all_sites()
                           : m_store.list_sites_for_owner(me.identity.view());
  if (sites_or.is_error()) {
    reply_message(event, 500, sites_or.error().message().view());
    return;
  }

  let const pending_or = m_store.list_pending_for_owner(me.identity.view());
  if (pending_or.is_error()) {
    reply_message(event, 500, pending_or.error().message().view());
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
  let const now = now_seconds();
  let const &sites = sites_or.value();
  for (usize i = 0; i < sites.count(); i++) {
    let const history_or =
        m_store.get_liveness_history(sites[i].slug.view(), now);
    ArrayList<i64> uptime{m_allocator};
    if (!history_or.is_error()) uptime = history_or.value().clone();
    write_panel_site(writer, sites[i], uptime);
  }
  writer.array_end();

  writer.key("pending");
  writer.array_begin();
  let const &pending = pending_or.value();
  for (usize i = 0; i < pending.count(); i++) {
    writer.object_begin();
    writer.key("id");
    writer.number(pending[i].id);
    writer.field("kind", pending[i].kind.view());
    writer.field("target_slug", pending[i].target_slug.view());
    writer.field("payload", pending[i].payload.view());
    writer.object_end();
  }
  writer.array_end();
  writer.object_end();
  reply_json(event, 200, writer.view());
}

fn App::handle_user_add(HttpServerEvent &event, const account &who) -> void
{
  let const document = Json::from(m_allocator, event.body());
  site_input input{};
  if (let const error = validate_site_input(document, input); error != nullptr)
  {
    reply_message(event, 400, error);
    return;
  }

  /* A submission for a slug already live in the ring is refused, so an add
     cannot be used to reassign someone else's site on approval. */
  let const existing = m_store.find_site(input.slug);
  if (existing.is_error()) {
    reply_message(event, 500, existing.error().message().view());
    return;
  }
  if (existing.value().has_value()) {
    reply_message(event, 409, "That slug is already taken");
    return;
  }

  JsonWriter payload{m_allocator};
  payload.object_begin();
  payload.field("name", input.name);
  payload.field("url", input.url);
  payload.field("description", input.description);
  payload.object_end();

  let const recorded = m_store.add_pending(
      "add", who.identity.view(), input.slug, payload.view(), now_seconds());
  if (recorded.is_error()) {
    reply_message(event, 500, recorded.error().message().view());
    return;
  }

  if (m_store
          .record_audit(actor_label(who), client_address(event), "submit add",
                        input.slug, input.name, now_seconds())
          .is_error())
    LOG(Info, "audit record dropped for submit add %.*s",
        static_cast<int>(input.slug.count()), input.slug.data);

  reply_message(event, 200, "Your site was submitted for review");
}

fn App::handle_user_rename(HttpServerEvent &event, const account &who) -> void
{
  let const document = Json::from(m_allocator, event.body());
  let const slug = document["slug"].to<StringView>();
  let const name = document["name"].to<StringView>();
  if (!slug.has_value() || !name.has_value()) {
    reply_message(event, 400, "A slug and a name are required");
    return;
  }

  let const owned = m_store.find_site(slug.value());
  if (owned.is_error() || !owned.value().has_value() ||
      owned.value().value().owner != who.identity)
  {
    reply_message(event, 403, "That site is not yours");
    return;
  }

  let const recorded = m_store.add_pending(
      "rename", who.identity.view(), slug.value(), name.value(), now_seconds());
  if (recorded.is_error()) {
    reply_message(event, 500, recorded.error().message().view());
    return;
  }

  if (m_store
          .record_audit(actor_label(who), client_address(event),
                        "submit rename", slug.value(), name.value(),
                        now_seconds())
          .is_error())
    LOG(Info, "audit record dropped for submit rename %.*s",
        static_cast<int>(slug.value().count()), slug.value().data);

  reply_message(event, 200, "Your rename was submitted for review");
}

fn App::handle_user_react(HttpServerEvent &event, const account &who) -> void
{
  let const document = Json::from(m_allocator, event.body());
  let const slug = document["slug"].to<StringView>();
  let const emoji = document["emoji"].to<StringView>();
  if (!slug.has_value() || !emoji.has_value()) {
    reply_message(event, 400, "A slug and an emoji are required");
    return;
  }

  /* Only the published set of reactions is accepted, so the table cannot be
     filled with an arbitrary key. */
  static constexpr StaticStringMap<bool, 6> ALLOWED_EMOJIS{
      {{"poop", true},
       {"like", true},
       {"eyes", true},
       {"fire", true},
       {"star", true},
       {"skull", true}}
  };
  if (ALLOWED_EMOJIS.find(emoji.value()) == nullptr) {
    reply_message(event, 400, "That reaction is not allowed");
    return;
  }

  let const target = m_store.find_site(slug.value());
  if (target.is_error()) {
    reply_message(event, 500, target.error().message().view());
    return;
  }
  if (!target.value().has_value()) {
    reply_message(event, 404, "No such site");
    return;
  }

  let const toggled =
      m_store.toggle_reaction(slug.value(), emoji.value(), who.identity.view());
  if (toggled.is_error()) {
    reply_message(event, 500, toggled.error().message().view());
    return;
  }

  if (m_store
          .record_audit(actor_label(who), client_address(event),
                        toggled.value() ? "react add" : "react remove",
                        slug.value(), emoji.value(), now_seconds())
          .is_error())
    LOG(Info, "audit record dropped for react on %.*s",
        static_cast<int>(slug.value().count()), slug.value().data);

  reply_message(event, 200,
                toggled.value() ? "Reaction added" : "Reaction removed");
}

fn App::handle_admin_add(HttpServerEvent &event) -> void
{
  let const who = require_admin(event);
  if (!who.has_value()) return;

  let const document = Json::from(m_allocator, event.body());
  site_input input{};
  if (let const error = validate_site_input(document, input); error != nullptr)
  {
    reply_message(event, 400, error);
    return;
  }

  let const existing = m_store.find_site(input.slug);
  if (existing.is_error()) {
    reply_message(event, 500, existing.error().message().view());
    return;
  }
  if (existing.value().has_value()) {
    reply_message(event, 409, "That slug is already taken");
    return;
  }

  /* An admin add bypasses the pending-action workflow and writes the site
     directly to the store, with the admin's identity as the owner. */
  site row{};
  row.slug = String{m_allocator, input.slug};
  row.name = String{m_allocator, input.name};
  row.url = String{m_allocator, input.url};
  row.description = String{m_allocator, input.description};
  row.owner = String{m_allocator, who.value().identity.view()};
  row.created_at = now_seconds();

  let const stored = m_store.upsert_site(row);
  if (stored.is_error()) {
    reply_message(event, 500, stored.error().message().view());
    return;
  }

  if (m_store
          .record_audit(actor_label(who.value()), client_address(event),
                        "add site", input.slug, input.name, now_seconds())
          .is_error())
    LOG(Info, "audit record dropped for add %.*s",
        static_cast<int>(input.slug.count()), input.slug.data);

  reply_message(event, 200, "Site added");
}

fn App::handle_admin_delete(HttpServerEvent &event) -> void
{
  let const who = require_admin(event);
  if (!who.has_value()) return;

  let const document = Json::from(m_allocator, event.body());
  let const slug = document["slug"].to<StringView>();
  if (!slug.has_value()) {
    reply_message(event, 400, "A slug is required");
    return;
  }

  let const found = m_store.find_site(slug.value());
  if (found.is_error() || !found.value().has_value()) {
    reply_message(event, 404, "No such site");
    return;
  }

  let const removed = m_store.delete_site(slug.value());
  if (removed.is_error()) {
    reply_message(event, 500, removed.error().message().view());
    return;
  }

  if (m_store
          .record_audit(actor_label(who.value()), client_address(event),
                        "remove site", slug.value(),
                        found.value().value().name.view(), now_seconds())
          .is_error())
    LOG(Info, "audit record dropped for remove %.*s",
        static_cast<int>(slug.value().count()), slug.value().data);

  reply_message(event, 200, "Site removed");
}

fn App::handle_admin_edit(HttpServerEvent &event) -> void
{
  let const who = require_admin(event);
  if (!who.has_value()) return;

  let const document = Json::from(m_allocator, event.body());
  site_input input{};
  if (let const error = validate_site_input(document, input); error != nullptr)
  {
    reply_message(event, 400, error);
    return;
  }

  /* The existing row is loaded so the owner and the creation time are
     preserved, since the upsert would otherwise overwrite the owner. */
  let const existing = m_store.find_site(input.slug);
  if (existing.is_error()) {
    reply_message(event, 500, existing.error().message().view());
    return;
  }
  if (!existing.value().has_value()) {
    reply_message(event, 404, "No such site");
    return;
  }
  let const &current = existing.value().value();

  site row{};
  row.slug = String{m_allocator, input.slug};
  row.name = String{m_allocator, input.name};
  row.url = String{m_allocator, input.url};
  row.description = String{m_allocator, input.description};
  row.owner = String{m_allocator, current.owner.view()};
  row.created_at = current.created_at;

  let const stored = m_store.upsert_site(row);
  if (stored.is_error()) {
    reply_message(event, 500, stored.error().message().view());
    return;
  }

  /* The edit may point the site at a new url, so it is queued for an immediate
     recheck instead of waiting out its reachability interval. */
  if (m_store.schedule_recheck(input.slug).is_error())
    LOG(Info, "recheck schedule dropped for %.*s",
        static_cast<int>(input.slug.count()), input.slug.data);

  if (m_store
          .record_audit(actor_label(who.value()), client_address(event),
                        "edit site", input.slug, input.name, now_seconds())
          .is_error())
    LOG(Info, "audit record dropped for edit %.*s",
        static_cast<int>(input.slug.count()), input.slug.data);

  reply_message(event, 200, "Saved");
}

fn App::handle_admin_pending(HttpServerEvent &event) -> void
{
  if (!require_admin(event).has_value()) return;

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

    /* The owner identity is opaque, so the submitter display name is attached
       for the admin to reach the person behind a request. */
    let const submitter = m_store.find_account(actions[i].owner.view());
    let const has_name = !submitter.is_error() && submitter.value().has_value();
    writer.field("owner_display_name",
                 has_name ? submitter.value().value().display_name.view() : "");

    writer.field("target_slug", actions[i].target_slug.view());
    writer.field("payload", actions[i].payload.view());
    writer.object_end();
  }
  writer.array_end();
  reply_json(event, 200, writer.view());
}

fn App::handle_admin_logs(HttpServerEvent &event) -> void
{
  if (!require_admin(event).has_value()) return;

  let const lines = log_ring_snapshot(m_allocator);

  JsonWriter writer{m_allocator};
  writer.array_begin();
  for (usize i = 0; i < lines.count(); i++) {
    let const text = lines[i].view();
    let const trimmed = (text.length > 0 && text.data[text.length - 1] == '\n')
                            ? StringView{text.data, text.length - 1}
                            : text;
    writer.string(trimmed);
  }
  writer.array_end();
  reply_json(event, 200, writer.view());
}

fn App::handle_admin_audit(HttpServerEvent &event) -> void
{
  if (!require_admin(event).has_value()) return;

  static constexpr i64 AUDIT_TAIL_COUNT = 100;
  let const entries_or = m_store.list_audit(AUDIT_TAIL_COUNT);
  if (entries_or.is_error()) {
    reply_message(event, 500, entries_or.error().message().view());
    return;
  }

  JsonWriter writer{m_allocator};
  writer.array_begin();
  let const &entries = entries_or.value();
  for (usize i = 0; i < entries.count(); i++) {
    writer.object_begin();
    writer.key("id");
    writer.number(entries[i].id);
    writer.field("actor", entries[i].actor.view());
    writer.field("actor_ip", entries[i].actor_ip.view());
    writer.field("action", entries[i].action.view());
    writer.field("target", entries[i].target.view());
    writer.field("detail", entries[i].detail.view());
    writer.key("created_at");
    writer.number(entries[i].created_at);
    writer.object_end();
  }
  writer.array_end();
  reply_json(event, 200, writer.view());
}

fn App::handle_admin_resolve(HttpServerEvent &event, bool should_approve)
    -> void
{
  let const who = require_admin(event);
  if (!who.has_value()) return;

  let const request = Json::from(m_allocator, event.body());
  let const id_or = request["id"].to<i64>();
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
    enum class pending_kind : u8
    {
      add,
      rename,
    };
    static constexpr StaticStringMap<pending_kind, 2> PENDING_KINDS{
        {{"add", pending_kind::add}, {"rename", pending_kind::rename}}
    };

    let const kind = PENDING_KINDS.find(action.kind.view());
    if (kind != nullptr) {
      switch (*kind) {
      case pending_kind::add: {
        /* A slug taken by another owner since the submission is not reassigned
           by an approval. */
        let const taken = m_store.find_site(action.target_slug.view());
        if (taken.is_error()) {
          reply_message(event, 500, taken.error().message().view());
          return;
        }
        if (taken.value().has_value() &&
            taken.value().value().owner != action.owner)
        {
          reply_message(event, 409, "That slug is already taken");
          return;
        }

        let const payload = Json::from(m_allocator, action.payload.view());
        site row{};
        row.slug = String{m_allocator, action.target_slug.view()};
        row.name =
            String{m_allocator, payload["name"].to<StringView>().value_or({})};
        row.url =
            String{m_allocator, payload["url"].to<StringView>().value_or({})};
        row.description = String{
            m_allocator, payload["description"].to<StringView>().value_or({})};
        row.owner = String{m_allocator, action.owner.view()};
        row.created_at = now_seconds();
        let const stored = m_store.upsert_site(row);
        if (stored.is_error()) {
          reply_message(event, 500, stored.error().message().view());
          return;
        }
        break;
      }
      case pending_kind::rename: {
        let const renamed = m_store.rename_site(action.target_slug.view(),
                                                action.payload.view());
        if (renamed.is_error()) {
          reply_message(event, 500, renamed.error().message().view());
          return;
        }
        break;
      }
      }
    }
  }

  let const resolved =
      m_store.set_pending_status(id, should_approve ? "approved" : "rejected");
  if (resolved.is_error()) {
    reply_message(event, 500, resolved.error().message().view());
    return;
  }

  String action_label{m_allocator};
  action_label.append(should_approve ? "approve " : "reject ");
  action_label.append(action.kind.view());
  if (m_store
          .record_audit(actor_label(who.value()), client_address(event),
                        action_label.view(), action.target_slug.view(),
                        action.owner.view(), now_seconds())
          .is_error())
    LOG(Info, "audit record dropped for pending %lld",
        static_cast<long long>(id));

  reply_message(event, 200, should_approve ? "Approved" : "Rejected");
}

} // namespace wr
