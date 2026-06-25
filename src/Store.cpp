#include "Store.hpp"

#include "Trace.hpp"

namespace wr {

namespace {

fn read_site(const SqlStatement &statement) -> site
{
  site row{};
  row.slug = statement.text(0);
  row.name = statement.text(1);
  row.url = statement.text(2);
  row.favicon = statement.text(3);
  row.is_reachable = statement.integer(4) != 0;
  row.last_seen_at = statement.integer(5);
  row.owner = statement.text(6);
  row.created_at = statement.integer(7);
  return row;
}

fn read_pending_action(const SqlStatement &statement) -> pending_action
{
  pending_action row{};
  row.id = statement.integer(0);
  row.kind = statement.text(1);
  row.owner = statement.text(2);
  row.target_slug = statement.text(3);
  row.payload = statement.text(4);
  row.created_at = statement.integer(5);
  row.status = statement.text(6);
  return row;
}

const StringView SITE_COLUMNS =
    "slug, name, url, favicon, is_reachable, last_seen_at, owner, created_at";

} // namespace

fn Store::migrate() -> ErrorOr<Ok>
{
  let const schema = "CREATE TABLE IF NOT EXISTS sites ("
                     "  slug TEXT PRIMARY KEY,"
                     "  name TEXT NOT NULL,"
                     "  url TEXT NOT NULL,"
                     "  favicon TEXT NOT NULL DEFAULT '',"
                     "  is_reachable INTEGER NOT NULL DEFAULT 1,"
                     "  last_seen_at INTEGER NOT NULL DEFAULT 0,"
                     "  owner TEXT NOT NULL DEFAULT '',"
                     "  created_at INTEGER NOT NULL DEFAULT 0,"
                     "  is_deleted INTEGER NOT NULL DEFAULT 0);"
                     "CREATE TABLE IF NOT EXISTS accounts ("
                     "  identity TEXT PRIMARY KEY,"
                     "  display_name TEXT NOT NULL DEFAULT '',"
                     "  is_admin INTEGER NOT NULL DEFAULT 0);"
                     "CREATE TABLE IF NOT EXISTS sessions ("
                     "  token TEXT PRIMARY KEY,"
                     "  identity TEXT NOT NULL,"
                     "  expires_at INTEGER NOT NULL);"
                     "CREATE TABLE IF NOT EXISTS pending_actions ("
                     "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                     "  kind TEXT NOT NULL,"
                     "  owner TEXT NOT NULL,"
                     "  target_slug TEXT NOT NULL DEFAULT '',"
                     "  payload TEXT NOT NULL DEFAULT '',"
                     "  created_at INTEGER NOT NULL,"
                     "  status TEXT NOT NULL DEFAULT 'pending');";

  TRY(m_database.execute(schema));

  /* The hot read paths filter the owner, the reachability, and the pending
     status, so each gets a covering index. The slug and the identity are
     already indexed as primary keys. */
  let const indexes =
      "CREATE INDEX IF NOT EXISTS index_sites_owner ON sites(owner);"
      "CREATE INDEX IF NOT EXISTS index_sites_reachable ON sites(is_reachable);"
      "CREATE INDEX IF NOT EXISTS index_pending_owner "
      "ON pending_actions(owner);"
      "CREATE INDEX IF NOT EXISTS index_pending_status "
      "ON pending_actions(status);"
      "CREATE INDEX IF NOT EXISTS index_sessions_expires "
      "ON sessions(expires_at);";

  TRY(m_database.execute(indexes));

  LOG(Debug, "migration ran, the tables and the indexes are present");
  return Success;
}

fn Store::query_sites(const char *filter_sql, Maybe<StringView> owner) const
    -> ErrorOr<ArrayList<site>>
{
  ArrayList<site> sites{m_allocator};
  String sql{m_allocator};
  sql.append("SELECT ");
  sql.append(SITE_COLUMNS);
  sql.append(" FROM sites ");
  sql.append(filter_sql);

  let statement = TRY(m_database.prepare(sql.view()));
  if (owner.has_value()) statement.bind(1, owner.value());

  while (TRY(statement.step()))
    sites.push(read_site(statement));
  return sites;
}

fn Store::list_active_sites() const -> ErrorOr<ArrayList<site>>
{
  return query_sites(
      "WHERE is_reachable = 1 AND is_deleted = 0 ORDER BY created_at, slug;",
      None);
}

fn Store::list_all_sites() const -> ErrorOr<ArrayList<site>>
{
  return query_sites("WHERE is_deleted = 0 ORDER BY created_at, slug;", None);
}

fn Store::list_sites_for_owner(StringView owner) const
    -> ErrorOr<ArrayList<site>>
{
  return query_sites(
      "WHERE owner = ? AND is_deleted = 0 ORDER BY created_at, slug;", owner);
}

fn Store::find_site(StringView slug) const -> ErrorOr<Maybe<site>>
{
  String sql{m_allocator};
  sql.append("SELECT ");
  sql.append(SITE_COLUMNS);
  sql.append(" FROM sites WHERE slug = ? AND is_deleted = 0;");

  let statement = TRY(m_database.prepare(sql.view()));
  statement.bind(1, slug);
  if (TRY(statement.step())) return Maybe<site>{read_site(statement)};
  return Maybe<site>{None};
}

fn Store::upsert_site(const site &row) -> ErrorOr<Ok>
{
  let const sql =
      "INSERT INTO sites "
      "(slug, name, url, favicon, is_reachable, last_seen_at, owner, "
      "created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(slug) DO UPDATE SET name = excluded.name, "
      "url = excluded.url, favicon = excluded.favicon, owner = excluded.owner, "
      "is_deleted = 0;";

  let statement = TRY(m_database.prepare(sql));
  statement.bind(1, row.slug.view());
  statement.bind(2, row.name.view());
  statement.bind(3, row.url.view());
  statement.bind(4, row.favicon.view());
  statement.bind(5, static_cast<i64>(row.is_reachable));
  statement.bind(6, row.last_seen_at);
  statement.bind(7, row.owner.view());
  statement.bind(8, row.created_at);
  unused(TRY(statement.step()));

  LOG(Info, "site upserted, slug=%s owner=%s", row.slug.c_str(),
      row.owner.c_str());
  return Success;
}

fn Store::rename_site(StringView slug, StringView name) -> ErrorOr<Ok>
{
  let statement = TRY(m_database.prepare("UPDATE sites SET name = ? "
                                         "WHERE slug = ?;"));
  statement.bind(1, name);
  statement.bind(2, slug);
  unused(TRY(statement.step()));

  LOG(Info, "site renamed, slug=%.*s name=%.*s", static_cast<int>(slug.count()),
      slug.data, static_cast<int>(name.count()), name.data);
  return Success;
}

fn Store::delete_site(StringView slug) -> ErrorOr<Ok>
{
  /* A removal marks the row deleted, so the history and the navigation links
     that pointed at the site survive an admin mistake. */
  let statement = TRY(
      m_database.prepare("UPDATE sites SET is_deleted = 1 WHERE slug = ?;"));
  statement.bind(1, slug);
  unused(TRY(statement.step()));

  LOG(Info, "site marked deleted, slug=%.*s", static_cast<int>(slug.count()),
      slug.data);
  return Success;
}

fn Store::set_site_reachability(StringView slug, bool is_reachable,
                                i64 last_seen_at) -> ErrorOr<Ok>
{
  let statement = TRY(m_database.prepare(
      "UPDATE sites SET is_reachable = ?, last_seen_at = ? WHERE slug = ?;"));
  statement.bind(1, static_cast<i64>(is_reachable));
  statement.bind(2, last_seen_at);
  statement.bind(3, slug);
  unused(TRY(statement.step()));

  LOG(Debug, "site reachability set, slug=%.*s is_reachable=%d",
      static_cast<int>(slug.count()), slug.data, is_reachable ? 1 : 0);
  return Success;
}

fn Store::find_account(StringView identity) const -> ErrorOr<Maybe<account>>
{
  let statement = TRY(m_database.prepare(
      "SELECT identity, display_name, is_admin FROM accounts "
      "WHERE identity = ?;"));
  statement.bind(1, identity);
  if (TRY(statement.step())) {
    account row{};
    row.identity = statement.text(0);
    row.display_name = statement.text(1);
    row.is_admin = statement.integer(2) != 0;
    return Maybe<account>{steal(row)};
  }
  return Maybe<account>{None};
}

fn Store::upsert_account(StringView identity, StringView display_name,
                         bool is_admin) -> ErrorOr<Ok>
{
  let statement =
      TRY(m_database.prepare("INSERT INTO accounts (identity, display_name, "
                             "is_admin) VALUES (?, ?, ?) "
                             "ON CONFLICT(identity) DO UPDATE SET "
                             "display_name = excluded.display_name;"));
  statement.bind(1, identity);
  statement.bind(2, display_name);
  statement.bind(3, static_cast<i64>(is_admin));
  unused(TRY(statement.step()));

  LOG(Info, "account upserted, identity=%.*s is_admin=%d",
      static_cast<int>(identity.count()), identity.data, is_admin ? 1 : 0);
  return Success;
}

fn Store::create_session(StringView token, StringView identity, i64 expires_at)
    -> ErrorOr<Ok>
{
  let statement = TRY(m_database.prepare(
      "INSERT INTO sessions (token, identity, expires_at) VALUES (?, ?, ?);"));
  statement.bind(1, token);
  statement.bind(2, identity);
  statement.bind(3, expires_at);
  unused(TRY(statement.step()));

  LOG(Info, "session opened for identity=%.*s",
      static_cast<int>(identity.count()), identity.data);
  return Success;
}

fn Store::find_session(StringView token) const -> ErrorOr<Maybe<session>>
{
  let statement = TRY(m_database.prepare(
      "SELECT token, identity, expires_at FROM sessions WHERE token = ?;"));
  statement.bind(1, token);
  if (TRY(statement.step())) {
    session row{};
    row.token = statement.text(0);
    row.identity = statement.text(1);
    row.expires_at = statement.integer(2);
    return Maybe<session>{steal(row)};
  }
  return Maybe<session>{None};
}

fn Store::delete_session(StringView token) -> ErrorOr<Ok>
{
  let statement =
      TRY(m_database.prepare("DELETE FROM sessions WHERE token = ?;"));
  statement.bind(1, token);
  unused(TRY(statement.step()));

  LOG(Info, "session closed");
  return Success;
}

fn Store::list_pending() const -> ErrorOr<ArrayList<pending_action>>
{
  ArrayList<pending_action> actions{m_allocator};
  let statement = TRY(m_database.prepare(
      "SELECT id, kind, owner, target_slug, payload, created_at, status "
      "FROM pending_actions WHERE status = 'pending' ORDER BY created_at;"));

  while (TRY(statement.step()))
    actions.push(read_pending_action(statement));
  return actions;
}

fn Store::list_pending_for_owner(StringView owner) const
    -> ErrorOr<ArrayList<pending_action>>
{
  ArrayList<pending_action> actions{m_allocator};
  let statement = TRY(m_database.prepare(
      "SELECT id, kind, owner, target_slug, payload, created_at, status "
      "FROM pending_actions WHERE owner = ? AND status = 'pending' "
      "ORDER BY created_at;"));
  statement.bind(1, owner);

  while (TRY(statement.step()))
    actions.push(read_pending_action(statement));
  return actions;
}

fn Store::add_pending(StringView kind, StringView owner, StringView target_slug,
                      StringView payload, i64 created_at) -> ErrorOr<Ok>
{
  let statement = TRY(m_database.prepare(
      "INSERT INTO pending_actions (kind, owner, target_slug, payload, "
      "created_at, status) VALUES (?, ?, ?, ?, ?, 'pending');"));
  statement.bind(1, kind);
  statement.bind(2, owner);
  statement.bind(3, target_slug);
  statement.bind(4, payload);
  statement.bind(5, created_at);
  unused(TRY(statement.step()));

  LOG(Info, "pending action recorded, kind=%.*s owner=%.*s target=%.*s",
      static_cast<int>(kind.count()), kind.data,
      static_cast<int>(owner.count()), owner.data,
      static_cast<int>(target_slug.count()), target_slug.data);
  return Success;
}

fn Store::find_pending(i64 id) const -> ErrorOr<Maybe<pending_action>>
{
  let statement = TRY(m_database.prepare(
      "SELECT id, kind, owner, target_slug, payload, created_at, status "
      "FROM pending_actions WHERE id = ?;"));
  statement.bind(1, id);
  if (TRY(statement.step()))
    return Maybe<pending_action>{read_pending_action(statement)};
  return Maybe<pending_action>{None};
}

fn Store::set_pending_status(i64 id, StringView status) -> ErrorOr<Ok>
{
  let statement = TRY(m_database.prepare(
      "UPDATE pending_actions SET status = ? WHERE id = ?;"));
  statement.bind(1, status);
  statement.bind(2, id);
  unused(TRY(statement.step()));

  LOG(Info, "pending action %lld set to %.*s", static_cast<long long>(id),
      static_cast<int>(status.count()), status.data);
  return Success;
}

} // namespace wr
