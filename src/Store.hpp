#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "Containers.hpp"
#include "ErrorOr.hpp"
#include "Maybe.hpp"
#include "Sql.hpp"

namespace wr {

enum class identity_source : u8
{
  github = 0,
  telegram = 1,
  dev = 2,
};

struct identity
{
  identity_source source{identity_source::github};
  String name;

  mustuse fn operator==(const identity &other) const->bool
  {
    return source == other.source && name.view() == other.name.view();
  }
};

fn source_oauth(identity_source source) -> StringView;

struct site
{
  String slug;
  String name;
  String url;
  String description;
  bool is_reachable{true};
  i64 last_seen_at{0};
  identity owner;
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

/* A panel account, keyed by its identity. The handle is the only label, and the
   admin flag gates the admin panel. */
struct account
{
  identity who;
  bool is_admin{false};
};

struct session
{
  String token;
  identity who;
  i64 expires_at{0};
};

/* A user request awaiting admin review. The kind is an add or a rename, and the
   payload holds the requested fields. */
struct pending_action
{
  i64 id{0};
  String kind;
  identity owner;
  String target_slug;
  String payload;
  i64 created_at{0};
  String status;
};

/* One footer comment by a site owner. The author identity keys the writer and
   carries the handle label, and the body may mention a site by its slug. */
struct comment
{
  i64 id{0};
  identity author;
  String body;
  i64 created_at{0};
  bool is_approved{false};
};

/* One admin action kept for the audit trail. The actor identity carries the
   handle label, the action names the operation, the target is the affected slug
   or id, and the detail carries a short human note. */
struct audit_entry
{
  i64 id{0};
  identity actor;
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
  fn check_api_version() -> ErrorOr<Ok>;

  mustuse fn list_active_sites() const -> ErrorOr<ArrayList<site>>;
  mustuse fn list_all_sites() const -> ErrorOr<ArrayList<site>>;
  mustuse fn list_sites_for_owner(const identity &owner) const
      -> ErrorOr<ArrayList<site>>;
  mustuse fn find_site(StringView slug) const -> ErrorOr<Maybe<site>>;
  fn upsert_site(const site &row) -> ErrorOr<Ok>;
  fn update_site_details(StringView slug, StringView name, StringView url,
                         StringView description) -> ErrorOr<Ok>;
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
                             const identity &who) -> ErrorOr<bool>;
  mustuse fn get_reactions(StringView slug) const
      -> ErrorOr<ArrayList<reaction_count>>;
  mustuse fn get_user_reactions(StringView slug, const identity &who) const
      -> ErrorOr<ArrayList<String>>;
  mustuse fn get_all_reactions() const
      -> ErrorOr<StringMap<ArrayList<reaction_count>>>;
  mustuse fn get_all_user_reactions(const identity &who) const
      -> ErrorOr<StringMap<ArrayList<String>>>;

  fn record_click(StringView slug) -> ErrorOr<Ok>;
  fn record_hop(StringView slug) -> ErrorOr<Ok>;
  mustuse fn get_site_metrics() const -> ErrorOr<ArrayList<site_metric>>;
  mustuse fn get_click_count(StringView slug) const -> ErrorOr<i64>;

  mustuse fn find_account(const identity &who) const -> ErrorOr<Maybe<account>>;
  fn upsert_account(const identity &who, bool is_admin) -> ErrorOr<Ok>;

  fn create_session(StringView token, const identity &who, i64 expires_at)
      -> ErrorOr<Ok>;
  mustuse fn find_session(StringView token) const -> ErrorOr<Maybe<session>>;
  fn delete_session(StringView token) -> ErrorOr<Ok>;

  mustuse fn list_pending() const -> ErrorOr<ArrayList<pending_action>>;
  mustuse fn list_pending_for_owner(const identity &owner) const
      -> ErrorOr<ArrayList<pending_action>>;
  fn add_pending(StringView kind, const identity &owner, StringView target_slug,
                 StringView payload, i64 created_at) -> ErrorOr<Ok>;
  mustuse fn find_pending(i64 id) const -> ErrorOr<Maybe<pending_action>>;
  fn set_pending_status(i64 id, StringView status) -> ErrorOr<Ok>;

  fn record_audit(const identity &actor, StringView actor_ip, StringView action,
                  StringView target, StringView detail, i64 created_at)
      -> ErrorOr<Ok>;
  mustuse fn list_audit(i64 limit_count) const
      -> ErrorOr<ArrayList<audit_entry>>;

  fn add_comment(const identity &author, StringView body, bool is_approved,
                 i64 created_at) -> ErrorOr<Ok>;
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
  Allocator m_allocator;
  SqlDatabase &m_database;
};

} // namespace wr
