#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "Containers.hpp"
#include "ErrorOr.hpp"
#include "Maybe.hpp"
#include "Path.hpp"

struct sqlite3;

namespace wr {

/* A member site of the ring. The slug keys the navigation, and the reachability
   and the last seen time are maintained by the liveness sweep. */
struct site
{
  String slug;
  String name;
  String url;
  String favicon;
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

/* A login session, keyed by an opaque token held in the session cookie. */
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

/* The sqlite store. It owns the connection, runs the migration on open, and is
   the only persistence layer. Every fallible call returns an ErrorOr. */
class Store
{
public:
  explicit Store(Allocator allocator) : m_allocator(allocator) {}
  ~Store();

  Store(const Store &) = delete;
  Store &operator=(const Store &) = delete;

  fn open(Path path) -> ErrorOr<Ok>;

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

  mustuse fn find_account(StringView identity) const -> ErrorOr<Maybe<account>>;
  fn upsert_account(StringView identity, StringView display_name, bool is_admin)
      -> ErrorOr<Ok>;

  fn create_session(StringView token, StringView identity, i64 expires_at)
      -> ErrorOr<Ok>;
  mustuse fn find_session(StringView token) const -> ErrorOr<Maybe<session>>;
  fn delete_session(StringView token) -> ErrorOr<Ok>;

  mustuse fn list_pending() const -> ErrorOr<ArrayList<pending_action>>;
  fn add_pending(StringView kind, StringView owner, StringView target_slug,
                 StringView payload, i64 created_at) -> ErrorOr<Ok>;
  mustuse fn find_pending(i64 id) const -> ErrorOr<Maybe<pending_action>>;
  fn set_pending_status(i64 id, StringView status) -> ErrorOr<Ok>;

  mustuse pure fn allocator() const noexcept -> Allocator
  {
    return m_allocator;
  }

private:
  fn migrate() -> ErrorOr<Ok>;

  /* List the sites the filter selects. The filter is the WHERE and ORDER tail
     appended after the column list, and the owner is bound to the single
     parameter when the filter names one. */
  mustuse fn query_sites(const char *filter_sql, Maybe<StringView> owner) const
      -> ErrorOr<ArrayList<site>>;

  Allocator m_allocator;
  sqlite3 *m_db{nullptr};
};

} // namespace wr
