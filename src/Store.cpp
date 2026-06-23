#include "Store.hpp"

#include "Trace.hpp"
#include "sqlite3.h"

namespace wr {

namespace {

fn make_db_error(Allocator allocator, sqlite3 *db, StringView context) -> Error
{
  String message{allocator};
  message.append(context);
  message.append(", ");
  message.append(sqlite3_errmsg(db));
  return Error{message.view()};
}

fn bind_text(sqlite3_stmt *statement, int index, StringView text) -> void
{
  sqlite3_bind_text(statement, index, text.data, static_cast<int>(text.count()),
                    SQLITE_TRANSIENT);
}

fn column_string(Allocator allocator, sqlite3_stmt *statement, int column)
    -> String
{
  let const text = sqlite3_column_text(statement, column);
  if (text == nullptr) return String{allocator};
  let const length =
      static_cast<usize>(sqlite3_column_bytes(statement, column));
  return String{
      allocator, StringView{reinterpret_cast<const char *>(text), length}
  };
}

fn read_site(Allocator allocator, sqlite3_stmt *statement) -> site
{
  site row{};
  row.slug = column_string(allocator, statement, 0);
  row.name = column_string(allocator, statement, 1);
  row.url = column_string(allocator, statement, 2);
  row.favicon = column_string(allocator, statement, 3);
  row.is_reachable = sqlite3_column_int(statement, 4) != 0;
  row.last_seen_at = sqlite3_column_int64(statement, 5);
  row.owner = column_string(allocator, statement, 6);
  row.created_at = sqlite3_column_int64(statement, 7);
  return row;
}

fn read_pending_action(Allocator allocator, sqlite3_stmt *statement)
    -> pending_action
{
  pending_action row{};
  row.id = sqlite3_column_int64(statement, 0);
  row.kind = column_string(allocator, statement, 1);
  row.owner = column_string(allocator, statement, 2);
  row.target_slug = column_string(allocator, statement, 3);
  row.payload = column_string(allocator, statement, 4);
  row.created_at = sqlite3_column_int64(statement, 5);
  row.status = column_string(allocator, statement, 6);
  return row;
}

constexpr const char *SITE_COLUMNS =
    "slug, name, url, favicon, is_reachable, last_seen_at, owner, created_at";

} // namespace

Store::~Store()
{
  if (m_db != nullptr) sqlite3_close(m_db);
}

fn Store::open(StringView path) -> ErrorOr<Ok>
{
  let const path_string = String{m_allocator, path};
  let const flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
  if (sqlite3_open_v2(path_string.c_str(), &m_db, flags, nullptr) != SQLITE_OK)
    return make_db_error(m_allocator, m_db, "Unable to open the database");

  sqlite3_busy_timeout(m_db, 5000);
  TRY(migrate());

  LOG(Info, "store opened at %s", path_string.c_str());
  return Success;
}

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
                     "  created_at INTEGER NOT NULL DEFAULT 0);"
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

  if (sqlite3_exec(m_db, schema, nullptr, nullptr, nullptr) != SQLITE_OK)
    return make_db_error(m_allocator, m_db, "Unable to run the migration");
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

  sqlite3_stmt *statement = nullptr;
  if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &statement, nullptr) !=
      SQLITE_OK)
    return make_db_error(m_allocator, m_db, "Unable to list the sites");
  defer { sqlite3_finalize(statement); };

  if (owner.has_value()) bind_text(statement, 1, owner.value());

  int step_result = SQLITE_DONE;
  while ((step_result = sqlite3_step(statement)) == SQLITE_ROW)
    sites.push(read_site(m_allocator, statement));
  if (step_result != SQLITE_DONE)
    return make_db_error(m_allocator, m_db, "Unable to read the sites");
  return sites;
}

fn Store::list_active_sites() const -> ErrorOr<ArrayList<site>>
{
  return query_sites("WHERE is_reachable = 1 ORDER BY created_at, slug;", None);
}

fn Store::list_all_sites() const -> ErrorOr<ArrayList<site>>
{
  return query_sites("ORDER BY created_at, slug;", None);
}

fn Store::list_sites_for_owner(StringView owner) const
    -> ErrorOr<ArrayList<site>>
{
  return query_sites("WHERE owner = ? ORDER BY created_at, slug;", owner);
}

fn Store::find_site(StringView slug) const -> ErrorOr<Maybe<site>>
{
  String sql{m_allocator};
  sql.append("SELECT ");
  sql.append(SITE_COLUMNS);
  sql.append(" FROM sites WHERE slug = ?;");

  sqlite3_stmt *statement = nullptr;
  if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &statement, nullptr) !=
      SQLITE_OK)
    return make_db_error(m_allocator, m_db, "Unable to find the site");
  defer { sqlite3_finalize(statement); };

  bind_text(statement, 1, slug);
  if (sqlite3_step(statement) == SQLITE_ROW)
    return Maybe<site>{read_site(m_allocator, statement)};
  return Maybe<site>{None};
}

fn Store::upsert_site(const site &row) -> ErrorOr<Ok>
{
  let const sql =
      "INSERT INTO sites "
      "(slug, name, url, favicon, is_reachable, last_seen_at, owner, "
      "created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(slug) DO UPDATE SET name = excluded.name, "
      "url = excluded.url, favicon = excluded.favicon, owner = excluded.owner;";

  sqlite3_stmt *statement = nullptr;
  if (sqlite3_prepare_v2(m_db, sql, -1, &statement, nullptr) != SQLITE_OK)
    return make_db_error(m_allocator, m_db, "Unable to store the site");
  defer { sqlite3_finalize(statement); };

  bind_text(statement, 1, row.slug.view());
  bind_text(statement, 2, row.name.view());
  bind_text(statement, 3, row.url.view());
  bind_text(statement, 4, row.favicon.view());
  sqlite3_bind_int(statement, 5, row.is_reachable ? 1 : 0);
  sqlite3_bind_int64(statement, 6, row.last_seen_at);
  bind_text(statement, 7, row.owner.view());
  sqlite3_bind_int64(statement, 8, row.created_at);

  if (sqlite3_step(statement) != SQLITE_DONE)
    return make_db_error(m_allocator, m_db, "Unable to store the site");
  return Success;
}

fn Store::rename_site(StringView slug, StringView name) -> ErrorOr<Ok>
{
  let const sql = "UPDATE sites SET name = ? WHERE slug = ?;";
  sqlite3_stmt *statement = nullptr;
  if (sqlite3_prepare_v2(m_db, sql, -1, &statement, nullptr) != SQLITE_OK)
    return make_db_error(m_allocator, m_db, "Unable to rename the site");
  defer { sqlite3_finalize(statement); };

  bind_text(statement, 1, name);
  bind_text(statement, 2, slug);
  if (sqlite3_step(statement) != SQLITE_DONE)
    return make_db_error(m_allocator, m_db, "Unable to rename the site");
  return Success;
}

fn Store::set_site_reachability(StringView slug, bool is_reachable,
                                i64 last_seen_at) -> ErrorOr<Ok>
{
  let const sql =
      "UPDATE sites SET is_reachable = ?, last_seen_at = ? WHERE slug = ?;";
  sqlite3_stmt *statement = nullptr;
  if (sqlite3_prepare_v2(m_db, sql, -1, &statement, nullptr) != SQLITE_OK)
    return make_db_error(m_allocator, m_db, "Unable to update reachability");
  defer { sqlite3_finalize(statement); };

  sqlite3_bind_int(statement, 1, is_reachable ? 1 : 0);
  sqlite3_bind_int64(statement, 2, last_seen_at);
  bind_text(statement, 3, slug);
  if (sqlite3_step(statement) != SQLITE_DONE)
    return make_db_error(m_allocator, m_db, "Unable to update reachability");
  return Success;
}

fn Store::find_account(StringView identity) const -> ErrorOr<Maybe<account>>
{
  let const sql = "SELECT identity, display_name, is_admin FROM accounts "
                  "WHERE identity = ?;";
  sqlite3_stmt *statement = nullptr;
  if (sqlite3_prepare_v2(m_db, sql, -1, &statement, nullptr) != SQLITE_OK)
    return make_db_error(m_allocator, m_db, "Unable to find the account");
  defer { sqlite3_finalize(statement); };

  bind_text(statement, 1, identity);
  if (sqlite3_step(statement) == SQLITE_ROW) {
    account row{};
    row.identity = column_string(m_allocator, statement, 0);
    row.display_name = column_string(m_allocator, statement, 1);
    row.is_admin = sqlite3_column_int(statement, 2) != 0;
    return Maybe<account>{steal(row)};
  }
  return Maybe<account>{None};
}

fn Store::upsert_account(StringView identity, StringView display_name,
                         bool is_admin) -> ErrorOr<Ok>
{
  let const sql = "INSERT INTO accounts (identity, display_name, is_admin) "
                  "VALUES (?, ?, ?) ON CONFLICT(identity) DO UPDATE SET "
                  "display_name = excluded.display_name;";
  sqlite3_stmt *statement = nullptr;
  if (sqlite3_prepare_v2(m_db, sql, -1, &statement, nullptr) != SQLITE_OK)
    return make_db_error(m_allocator, m_db, "Unable to store the account");
  defer { sqlite3_finalize(statement); };

  bind_text(statement, 1, identity);
  bind_text(statement, 2, display_name);
  sqlite3_bind_int(statement, 3, is_admin ? 1 : 0);
  if (sqlite3_step(statement) != SQLITE_DONE)
    return make_db_error(m_allocator, m_db, "Unable to store the account");
  return Success;
}

fn Store::create_session(StringView token, StringView identity, i64 expires_at)
    -> ErrorOr<Ok>
{
  let const sql =
      "INSERT INTO sessions (token, identity, expires_at) VALUES (?, ?, ?);";
  sqlite3_stmt *statement = nullptr;
  if (sqlite3_prepare_v2(m_db, sql, -1, &statement, nullptr) != SQLITE_OK)
    return make_db_error(m_allocator, m_db, "Unable to open the session");
  defer { sqlite3_finalize(statement); };

  bind_text(statement, 1, token);
  bind_text(statement, 2, identity);
  sqlite3_bind_int64(statement, 3, expires_at);
  if (sqlite3_step(statement) != SQLITE_DONE)
    return make_db_error(m_allocator, m_db, "Unable to open the session");
  return Success;
}

fn Store::find_session(StringView token) const -> ErrorOr<Maybe<session>>
{
  let const sql =
      "SELECT token, identity, expires_at FROM sessions WHERE token = ?;";
  sqlite3_stmt *statement = nullptr;
  if (sqlite3_prepare_v2(m_db, sql, -1, &statement, nullptr) != SQLITE_OK)
    return make_db_error(m_allocator, m_db, "Unable to find the session");
  defer { sqlite3_finalize(statement); };

  bind_text(statement, 1, token);
  if (sqlite3_step(statement) == SQLITE_ROW) {
    session row{};
    row.token = column_string(m_allocator, statement, 0);
    row.identity = column_string(m_allocator, statement, 1);
    row.expires_at = sqlite3_column_int64(statement, 2);
    return Maybe<session>{steal(row)};
  }
  return Maybe<session>{None};
}

fn Store::delete_session(StringView token) -> ErrorOr<Ok>
{
  let const sql = "DELETE FROM sessions WHERE token = ?;";
  sqlite3_stmt *statement = nullptr;
  if (sqlite3_prepare_v2(m_db, sql, -1, &statement, nullptr) != SQLITE_OK)
    return make_db_error(m_allocator, m_db, "Unable to close the session");
  defer { sqlite3_finalize(statement); };

  bind_text(statement, 1, token);
  if (sqlite3_step(statement) != SQLITE_DONE)
    return make_db_error(m_allocator, m_db, "Unable to close the session");
  return Success;
}

fn Store::list_pending() const -> ErrorOr<ArrayList<pending_action>>
{
  ArrayList<pending_action> actions{m_allocator};
  let const sql =
      "SELECT id, kind, owner, target_slug, payload, created_at, status "
      "FROM pending_actions WHERE status = 'pending' ORDER BY created_at;";
  sqlite3_stmt *statement = nullptr;
  if (sqlite3_prepare_v2(m_db, sql, -1, &statement, nullptr) != SQLITE_OK)
    return make_db_error(m_allocator, m_db,
                         "Unable to list the pending actions");
  defer { sqlite3_finalize(statement); };

  int step_result = SQLITE_DONE;
  while ((step_result = sqlite3_step(statement)) == SQLITE_ROW)
    actions.push(read_pending_action(m_allocator, statement));
  if (step_result != SQLITE_DONE)
    return make_db_error(m_allocator, m_db,
                         "Unable to read the pending actions");
  return actions;
}

fn Store::add_pending(StringView kind, StringView owner, StringView target_slug,
                      StringView payload, i64 created_at) -> ErrorOr<Ok>
{
  let const sql =
      "INSERT INTO pending_actions (kind, owner, target_slug, payload, "
      "created_at, status) VALUES (?, ?, ?, ?, ?, 'pending');";
  sqlite3_stmt *statement = nullptr;
  if (sqlite3_prepare_v2(m_db, sql, -1, &statement, nullptr) != SQLITE_OK)
    return make_db_error(m_allocator, m_db, "Unable to record the action");
  defer { sqlite3_finalize(statement); };

  bind_text(statement, 1, kind);
  bind_text(statement, 2, owner);
  bind_text(statement, 3, target_slug);
  bind_text(statement, 4, payload);
  sqlite3_bind_int64(statement, 5, created_at);
  if (sqlite3_step(statement) != SQLITE_DONE)
    return make_db_error(m_allocator, m_db, "Unable to record the action");
  return Success;
}

fn Store::find_pending(i64 id) const -> ErrorOr<Maybe<pending_action>>
{
  let const sql =
      "SELECT id, kind, owner, target_slug, payload, created_at, status "
      "FROM pending_actions WHERE id = ?;";
  sqlite3_stmt *statement = nullptr;
  if (sqlite3_prepare_v2(m_db, sql, -1, &statement, nullptr) != SQLITE_OK)
    return make_db_error(m_allocator, m_db, "Unable to find the action");
  defer { sqlite3_finalize(statement); };

  sqlite3_bind_int64(statement, 1, id);
  if (sqlite3_step(statement) == SQLITE_ROW)
    return Maybe<pending_action>{read_pending_action(m_allocator, statement)};
  return Maybe<pending_action>{None};
}

fn Store::set_pending_status(i64 id, StringView status) -> ErrorOr<Ok>
{
  let const sql = "UPDATE pending_actions SET status = ? WHERE id = ?;";
  sqlite3_stmt *statement = nullptr;
  if (sqlite3_prepare_v2(m_db, sql, -1, &statement, nullptr) != SQLITE_OK)
    return make_db_error(m_allocator, m_db, "Unable to update the action");
  defer { sqlite3_finalize(statement); };

  bind_text(statement, 1, status);
  sqlite3_bind_int64(statement, 2, id);
  if (sqlite3_step(statement) != SQLITE_DONE)
    return make_db_error(m_allocator, m_db, "Unable to update the action");
  return Success;
}

} // namespace wr
