#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "Containers.hpp"
#include "ErrorOr.hpp"
#include "Maybe.hpp"
#include "Sql.hpp"

namespace wr {

struct site
{
  String slug;
  String name;
  String url;
  String description;
  bool is_reachable{true};
  i64 last_seen_at{0};
  String owner;
  i64 created_at{0};
};

/* A tally of one emoji on one site, read back for the public listing. */
struct reaction_count
{
  String emoji;
  i64 count{0};
};

/* The traffic tally for one site. The click count is the outbound follows from
   the ring, and the hop count is the next, prev, and random traversals that
   landed on it. */
struct site_metric
{
  String slug;
  i64 click_count{0};
  i64 hop_count{0};
};

/* A panel account, keyed by a provider identity such as "github:1234". The
   username is the linkable handle, the github login or the telegram username.
 */
struct account
{
  String identity;
  String display_name;
  String username;
  bool is_admin{false};
};

struct session
{
  String token;
  String identity;
  i64 expires_at{0};
};

/* A user request awaiting admin review. The kind is an add or a rename, and the
   payload holds the requested fields. */
struct pending_action
{
  i64 id{0};
  String kind;
  String owner;
  String target_slug;
  String payload;
  i64 created_at{0};
  String status;
};

/* One footer comment by a site owner. The author identity keys the writer, the
   author name is the readable label, and the body may mention a site by its
   slug. */
struct comment
{
  i64 id{0};
  String author_identity;
  String author_name;
  String body;
  i64 created_at{0};
  bool is_approved{false};
};

/* One admin action kept for the audit trail. The actor is the admin label, the
   action names the operation, the target is the affected slug or id, and the
   detail carries a short human note. */
struct audit_entry
{
  i64 id{0};
  String actor;
  String actor_ip;
  String action;
  String target;
  String detail;
  i64 created_at{0};
};

/* The store. It runs the migration and maps the rows over a borrowed
   SqlDatabase backend. Every fallible call returns an ErrorOr. */
class Store
{
public:
  Store(Allocator allocator, SqlDatabase &database)
      : m_allocator(allocator), m_database(database)
  {}

  Store(const Store &) = delete;
  Store &operator=(const Store &) = delete;

  fn migrate() -> ErrorOr<Ok>;

  mustuse fn list_active_sites() const -> ErrorOr<ArrayList<site>>;
  mustuse fn list_all_sites() const -> ErrorOr<ArrayList<site>>;
  mustuse fn list_sites_for_owner(StringView owner) const
      -> ErrorOr<ArrayList<site>>;
  mustuse fn find_site(StringView slug) const -> ErrorOr<Maybe<site>>;
  fn upsert_site(const site &row) -> ErrorOr<Ok>;
  fn rename_site(StringView slug, StringView name) -> ErrorOr<Ok>;
  fn delete_site(StringView slug) -> ErrorOr<Ok>;
  fn set_site_reachability(StringView slug, bool is_reachable, i64 last_seen_at)
      -> ErrorOr<Ok>;
  fn schedule_recheck(StringView slug) -> ErrorOr<Ok>;

  fn record_liveness(StringView slug, bool is_reachable, i64 now)
      -> ErrorOr<Ok>;
  fn rotate_liveness(i64 now) -> ErrorOr<Ok>;
  mustuse fn get_liveness_history(StringView slug, i64 now) const
      -> ErrorOr<ArrayList<i64>>;

  mustuse fn toggle_reaction(StringView slug, StringView emoji,
                             StringView identity) -> ErrorOr<bool>;
  mustuse fn get_reactions(StringView slug) const
      -> ErrorOr<ArrayList<reaction_count>>;
  mustuse fn get_user_reactions(StringView slug, StringView identity) const
      -> ErrorOr<ArrayList<String>>;

  fn record_click(StringView slug) -> ErrorOr<Ok>;
  fn record_hop(StringView slug) -> ErrorOr<Ok>;
  mustuse fn get_site_metrics() const -> ErrorOr<ArrayList<site_metric>>;

  mustuse fn find_account(StringView identity) const -> ErrorOr<Maybe<account>>;
  fn upsert_account(StringView identity, StringView display_name,
                    StringView username, bool is_admin) -> ErrorOr<Ok>;

  fn create_session(StringView token, StringView identity, i64 expires_at)
      -> ErrorOr<Ok>;
  mustuse fn find_session(StringView token) const -> ErrorOr<Maybe<session>>;
  fn delete_session(StringView token) -> ErrorOr<Ok>;

  mustuse fn list_pending() const -> ErrorOr<ArrayList<pending_action>>;
  mustuse fn list_pending_for_owner(StringView owner) const
      -> ErrorOr<ArrayList<pending_action>>;
  fn add_pending(StringView kind, StringView owner, StringView target_slug,
                 StringView payload, i64 created_at) -> ErrorOr<Ok>;
  mustuse fn find_pending(i64 id) const -> ErrorOr<Maybe<pending_action>>;
  fn set_pending_status(i64 id, StringView status) -> ErrorOr<Ok>;

  fn record_audit(StringView actor, StringView actor_ip, StringView action,
                  StringView target, StringView detail, i64 created_at)
      -> ErrorOr<Ok>;
  mustuse fn list_audit(i64 limit_count) const
      -> ErrorOr<ArrayList<audit_entry>>;

  fn add_comment(StringView author_identity, StringView author_name,
                 StringView body, bool is_approved, i64 created_at)
      -> ErrorOr<Ok>;
  mustuse fn list_comments(i64 limit_count, i64 offset_count) const
      -> ErrorOr<ArrayList<comment>>;
  mustuse fn list_pending_comments(i64 limit_count) const
      -> ErrorOr<ArrayList<comment>>;
  mustuse fn find_comment(i64 id) const -> ErrorOr<Maybe<comment>>;
  fn approve_comment(i64 id) -> ErrorOr<Ok>;
  fn delete_comment(i64 id) -> ErrorOr<Ok>;

  fn clear_statement_cache() noexcept -> void;

  mustuse pure fn allocator() const noexcept -> Allocator
  {
    return m_allocator;
  }

private:
  mustuse fn query_sites(const char *filter_sql, Maybe<StringView> owner) const
      -> ErrorOr<ArrayList<site>>;

  Allocator m_allocator;
  SqlDatabase &m_database;
};

} // namespace wr
