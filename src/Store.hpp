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

/* A panel account, keyed by a provider identity such as "github:1234". */
struct account
{
  String identity;
  String display_name;
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

  mustuse fn find_account(StringView identity) const -> ErrorOr<Maybe<account>>;
  fn upsert_account(StringView identity, StringView display_name, bool is_admin)
      -> ErrorOr<Ok>;

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

  mustuse pure fn allocator() const noexcept -> Allocator
  {
    return m_allocator;
  }

private:
  mustuse fn query_sites(const char *filter_sql, Maybe<StringView> owner) const
      -> ErrorOr<ArrayList<site>>;
  mustuse fn schema_version() const -> ErrorOr<i64>;
  mustuse fn detect_baseline() const -> ErrorOr<i64>;
  fn set_schema_version(i64 version) -> ErrorOr<Ok>;

  Allocator m_allocator;
  SqlDatabase &m_database;
};

} // namespace wr
