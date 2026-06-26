#include "Store.hpp"

#include "Trace.hpp"

namespace wr {

namespace {

fn read_site(SqlStatement &statement) -> site
{
  site row{};
  row.slug = statement.get<String>();
  row.name = statement.get<String>();
  row.url = statement.get<String>();
  row.description = statement.get<String>();
  row.is_reachable = statement.get<i64>() != 0;
  row.last_seen_at = statement.get<i64>();
  row.owner = statement.get<String>();
  row.created_at = statement.get<i64>();
  return row;
}

fn read_pending_action(SqlStatement &statement) -> pending_action
{
  pending_action row{};
  row.id = statement.get<i64>();
  row.kind = statement.get<String>();
  row.owner = statement.get<String>();
  row.target_slug = statement.get<String>();
  row.payload = statement.get<String>();
  row.created_at = statement.get<i64>();
  row.status = statement.get<String>();
  return row;
}

fn read_audit_entry(SqlStatement &statement) -> audit_entry
{
  audit_entry row{};
  row.id = statement.get<i64>();
  row.actor = statement.get<String>();
  row.actor_ip = statement.get<String>();
  row.action = statement.get<String>();
  row.target = statement.get<String>();
  row.detail = statement.get<String>();
  row.created_at = statement.get<i64>();
  return row;
}

fn read_comment(SqlStatement &statement) -> comment
{
  comment row{};
  row.id = statement.get<i64>();
  row.author_identity = statement.get<String>();
  row.author_name = statement.get<String>();
  row.body = statement.get<String>();
  row.created_at = statement.get<i64>();
  return row;
}

const StringView SITE_COLUMNS = "slug, name, url, description, is_reachable, "
                                "last_seen_at, owner, created_at";

} // namespace

/* The schema is the sum of the ordered migrations below. A migration is run
   once, and the applied count is held in PRAGMA user_version, so a new database
   runs every step and an older one runs only the steps it is missing. A
   migration is never edited once released, a schema change is a new entry. */
static const char *const SCHEMA_MIGRATIONS[] = {
    "CREATE TABLE IF NOT EXISTS sites ("
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
    "  status TEXT NOT NULL DEFAULT 'pending');"
    "CREATE INDEX IF NOT EXISTS index_sites_owner ON sites(owner);"
    "CREATE INDEX IF NOT EXISTS index_sites_reachable ON sites(is_reachable);"
    "CREATE INDEX IF NOT EXISTS index_pending_owner ON pending_actions(owner);"
    "CREATE INDEX IF NOT EXISTS index_pending_status ON "
    "pending_actions(status);"
    "CREATE INDEX IF NOT EXISTS index_sessions_expires ON "
    "sessions(expires_at);",

    "ALTER TABLE sites ADD COLUMN is_deleted INTEGER NOT NULL DEFAULT 0;",

    ("ALTER TABLE sites ADD COLUMN description TEXT NOT NULL DEFAULT ''; "
     "ALTER TABLE sites DROP COLUMN favicon;"),

    ("CREATE TABLE IF NOT EXISTS liveness_buckets ("
     "  slug TEXT NOT NULL,"
     "  hour_bucket INTEGER NOT NULL,"
     "  up_count INTEGER NOT NULL DEFAULT 0,"
     "  probe_count INTEGER NOT NULL DEFAULT 0,"
     "  PRIMARY KEY (slug, hour_bucket));"
     "CREATE INDEX IF NOT EXISTS index_liveness_buckets_hour ON "
     "liveness_buckets(hour_bucket);"),

    ("CREATE TABLE IF NOT EXISTS reactions ("
     "  slug TEXT NOT NULL,"
     "  emoji TEXT NOT NULL,"
     "  identity TEXT NOT NULL,"
     "  PRIMARY KEY (slug, emoji, identity));"
     "CREATE INDEX IF NOT EXISTS index_reactions_slug ON reactions(slug);"),

    ("CREATE TABLE IF NOT EXISTS audit_log ("
     "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
     "  actor TEXT NOT NULL,"
     "  action TEXT NOT NULL,"
     "  target TEXT NOT NULL DEFAULT '',"
     "  detail TEXT NOT NULL DEFAULT '',"
     "  created_at INTEGER NOT NULL);"
     "CREATE INDEX IF NOT EXISTS index_audit_created ON "
     "audit_log(created_at);"),

    "ALTER TABLE audit_log ADD COLUMN actor_ip TEXT NOT NULL DEFAULT '';",

    ("CREATE TABLE IF NOT EXISTS comments ("
     "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
     "  author_identity TEXT NOT NULL,"
     "  author_name TEXT NOT NULL DEFAULT '',"
     "  body TEXT NOT NULL,"
     "  created_at INTEGER NOT NULL);"
     "CREATE INDEX IF NOT EXISTS index_comments_created ON "
     "comments(created_at);"),
};

/* A probe is bucketed by the hour, and seven days of buckets are kept. */
static constexpr i64 LIVENESS_WINDOW_HOURS = 168;

fn Store::schema_version() const -> ErrorOr<i64>
{
  let statement = TRY(m_database.prepare("PRAGMA user_version;"));
  if (TRY(statement.step())) return statement.get<i64>();
  return i64{0};
}

/* A database created before versioning carries a user_version of zero, so its
   real baseline is read from the schema it already holds. */
fn Store::detect_baseline() const -> ErrorOr<i64>
{
  let table_statement = TRY(m_database.prepare(
      "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'sites';"));
  if (!TRY(table_statement.step())) return i64{0};

  let info_statement = TRY(m_database.prepare("PRAGMA table_info(sites);"));
  while (TRY(info_statement.step())) {
    unused(info_statement.get<i64>());
    let const name = info_statement.get<String>();
    if (name.view() == "is_deleted") return i64{2};
  }
  return i64{1};
}

fn Store::set_schema_version(i64 version) -> ErrorOr<Ok>
{
  char pragma[48];
  std::snprintf(pragma, sizeof(pragma), "PRAGMA user_version = %lld;",
                static_cast<long long>(version));
  return m_database.execute(StringView{pragma});
}

fn Store::migrate() -> ErrorOr<Ok>
{
  let const stored_count = TRY(schema_version());
  let const applied_count =
      stored_count == 0 ? TRY(detect_baseline()) : stored_count;
  let const target_count = static_cast<i64>(countof(SCHEMA_MIGRATIONS));

  if (applied_count >= target_count) {
    if (applied_count != stored_count) TRY(set_schema_version(applied_count));
    LOG(Debug, "schema already at version %lld",
        static_cast<long long>(applied_count));
    return Success;
  }

  TRY(m_database.execute("BEGIN;"));
  for (i64 version = applied_count; version < target_count; version++)
    TRY(m_database.execute(SCHEMA_MIGRATIONS[version]));
  TRY(set_schema_version(target_count));
  TRY(m_database.execute("COMMIT;"));

  LOG(Info, "schema migrated from version %lld to %lld",
      static_cast<long long>(applied_count),
      static_cast<long long>(target_count));
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
  if (owner.has_value()) statement.bind(owner.value());

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
  statement.bind(slug);
  if (TRY(statement.step())) return Maybe<site>{read_site(statement)};
  return Maybe<site>{None};
}

fn Store::upsert_site(const site &row) -> ErrorOr<Ok>
{
  let const sql =
      "INSERT INTO sites "
      "(slug, name, url, description, is_reachable, last_seen_at, owner, "
      "created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(slug) DO UPDATE SET name = excluded.name, "
      "url = excluded.url, description = excluded.description, "
      "owner = excluded.owner, is_deleted = 0;";

  let statement = TRY(m_database.prepare(sql));
  statement.bind(row.slug.view());
  statement.bind(row.name.view());
  statement.bind(row.url.view());
  statement.bind(row.description.view());
  statement.bind(row.is_reachable);
  statement.bind(row.last_seen_at);
  statement.bind(row.owner.view());
  statement.bind(row.created_at);
  unused(TRY(statement.step()));

  LOG(Info, "site upserted, slug=%s owner=%s", row.slug.c_str(),
      row.owner.c_str());
  return Success;
}

fn Store::rename_site(StringView slug, StringView name) -> ErrorOr<Ok>
{
  let statement = TRY(m_database.prepare(
      "UPDATE sites SET name = ? WHERE slug = ? AND is_deleted = 0;"));
  statement.bind(name);
  statement.bind(slug);
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
  statement.bind(slug);
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
  statement.bind(is_reachable);
  statement.bind(last_seen_at);
  statement.bind(slug);
  unused(TRY(statement.step()));

  LOG(Debug, "site reachability set, slug=%.*s is_reachable=%d",
      static_cast<int>(slug.count()), slug.data, is_reachable ? 1 : 0);
  return Success;
}

fn Store::schedule_recheck(StringView slug) -> ErrorOr<Ok>
{
  /* A zero last_seen_at is always due, so the next sweep re-probes the site
     instead of waiting out its interval. */
  let statement = TRY(
      m_database.prepare("UPDATE sites SET last_seen_at = 0 WHERE slug = ?;"));
  statement.bind(slug);
  unused(TRY(statement.step()));

  LOG(Info, "site scheduled for a recheck, slug=%.*s",
      static_cast<int>(slug.count()), slug.data);
  return Success;
}

fn Store::record_liveness(StringView slug, bool is_reachable, i64 now)
    -> ErrorOr<Ok>
{
  let const hour_bucket = now / 3600;
  let const up_count = is_reachable ? 1 : 0;

  let statement = TRY(m_database.prepare(
      "INSERT INTO liveness_buckets (slug, hour_bucket, up_count, probe_count) "
      "VALUES (?, ?, ?, 1) "
      "ON CONFLICT(slug, hour_bucket) DO UPDATE SET "
      "up_count = up_count + excluded.up_count, "
      "probe_count = probe_count + 1;"));
  statement.bind(slug);
  statement.bind(hour_bucket);
  statement.bind(static_cast<i64>(up_count));
  unused(TRY(statement.step()));

  LOG(Debug, "liveness recorded, slug=%.*s hour=%lld is_reachable=%d",
      static_cast<int>(slug.count()), slug.data,
      static_cast<long long>(hour_bucket), is_reachable ? 1 : 0);
  return Success;
}

fn Store::rotate_liveness(i64 now) -> ErrorOr<Ok>
{
  let const cutoff_hour = (now / 3600) - LIVENESS_WINDOW_HOURS;

  let statement = TRY(m_database.prepare(
      "DELETE FROM liveness_buckets WHERE hour_bucket < ?;"));
  statement.bind(cutoff_hour);
  unused(TRY(statement.step()));

  LOG(Debug, "liveness buckets rotated, cutoff_hour=%lld",
      static_cast<long long>(cutoff_hour));
  return Success;
}

fn Store::get_liveness_history(StringView slug, i64 now) const
    -> ErrorOr<ArrayList<i64>>
{
  let const current_hour = now / 3600;

  ArrayList<i64> history{m_allocator};
  history.reserve(static_cast<usize>(LIVENESS_WINDOW_HOURS));
  for (i64 i = 0; i < LIVENESS_WINDOW_HOURS; i++)
    history.push(-1);

  let statement = TRY(m_database.prepare(
      "SELECT hour_bucket, up_count, probe_count FROM liveness_buckets "
      "WHERE slug = ? AND hour_bucket > ? ORDER BY hour_bucket;"));
  statement.bind(slug);
  statement.bind(current_hour - LIVENESS_WINDOW_HOURS);
  while (TRY(statement.step())) {
    let const hour_bucket = statement.get<i64>();
    let const up_count = statement.get<i64>();
    let const probe_count = statement.get<i64>();

    let const slot = hour_bucket - current_hour + LIVENESS_WINDOW_HOURS - 1;
    if (slot >= 0 && slot < LIVENESS_WINDOW_HOURS && probe_count > 0) {
      history[static_cast<usize>(slot)] = up_count * 100 / probe_count;
    }
  }

  return history;
}

fn Store::toggle_reaction(StringView slug, StringView emoji,
                          StringView identity) -> ErrorOr<bool>
{
  /* The probe statement is leased from the per-connection cache, so it is
     scoped and returned before the write statement is prepared. */
  bool is_set = false;
  {
    let probe = TRY(m_database.prepare(
        "SELECT 1 FROM reactions WHERE slug = ? AND emoji = ? AND "
        "identity = ?;"));
    probe.bind(slug);
    probe.bind(emoji);
    probe.bind(identity);
    is_set = TRY(probe.step());
  }

  if (is_set) {
    let statement = TRY(m_database.prepare(
        "DELETE FROM reactions WHERE slug = ? AND emoji = ? AND "
        "identity = ?;"));
    statement.bind(slug);
    statement.bind(emoji);
    statement.bind(identity);
    unused(TRY(statement.step()));

    LOG(Info, "reaction removed, slug=%.*s emoji=%.*s",
        static_cast<int>(slug.count()), slug.data,
        static_cast<int>(emoji.count()), emoji.data);
    return false;
  }

  let statement = TRY(m_database.prepare(
      "INSERT INTO reactions (slug, emoji, identity) VALUES (?, ?, ?);"));
  statement.bind(slug);
  statement.bind(emoji);
  statement.bind(identity);
  unused(TRY(statement.step()));

  LOG(Info, "reaction added, slug=%.*s emoji=%.*s",
      static_cast<int>(slug.count()), slug.data,
      static_cast<int>(emoji.count()), emoji.data);
  return true;
}

fn Store::get_reactions(StringView slug) const
    -> ErrorOr<ArrayList<reaction_count>>
{
  ArrayList<reaction_count> counts{m_allocator};

  let statement = TRY(m_database.prepare(
      "SELECT emoji, COUNT(*) FROM reactions WHERE slug = ? GROUP BY emoji "
      "ORDER BY emoji;"));
  statement.bind(slug);
  while (TRY(statement.step())) {
    reaction_count entry{};
    entry.emoji = statement.get<String>();
    entry.count = statement.get<i64>();
    counts.push(steal(entry));
  }

  return counts;
}

fn Store::get_user_reactions(StringView slug, StringView identity) const
    -> ErrorOr<ArrayList<String>>
{
  ArrayList<String> emojis{m_allocator};

  let statement = TRY(m_database.prepare(
      "SELECT emoji FROM reactions WHERE slug = ? AND identity = ? "
      "ORDER BY emoji;"));
  statement.bind(slug);
  statement.bind(identity);
  while (TRY(statement.step()))
    emojis.push(statement.get<String>());

  return emojis;
}

fn Store::find_account(StringView identity) const -> ErrorOr<Maybe<account>>
{
  let statement = TRY(m_database.prepare(
      "SELECT identity, display_name, is_admin FROM accounts "
      "WHERE identity = ?;"));
  statement.bind(identity);
  if (TRY(statement.step())) {
    account row{};
    row.identity = statement.get<String>();
    row.display_name = statement.get<String>();
    row.is_admin = statement.get<i64>() != 0;
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
  statement.bind(identity);
  statement.bind(display_name);
  statement.bind(is_admin);
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
  statement.bind(token);
  statement.bind(identity);
  statement.bind(expires_at);
  unused(TRY(statement.step()));

  LOG(Info, "session opened for identity=%.*s",
      static_cast<int>(identity.count()), identity.data);
  return Success;
}

fn Store::find_session(StringView token) const -> ErrorOr<Maybe<session>>
{
  let statement = TRY(m_database.prepare(
      "SELECT token, identity, expires_at FROM sessions WHERE token = ?;"));
  statement.bind(token);
  if (TRY(statement.step())) {
    session row{};
    row.token = statement.get<String>();
    row.identity = statement.get<String>();
    row.expires_at = statement.get<i64>();
    return Maybe<session>{steal(row)};
  }
  return Maybe<session>{None};
}

fn Store::delete_session(StringView token) -> ErrorOr<Ok>
{
  let statement =
      TRY(m_database.prepare("DELETE FROM sessions WHERE token = ?;"));
  statement.bind(token);
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
  statement.bind(owner);

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
  statement.bind(kind);
  statement.bind(owner);
  statement.bind(target_slug);
  statement.bind(payload);
  statement.bind(created_at);
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
  statement.bind(id);
  if (TRY(statement.step()))
    return Maybe<pending_action>{read_pending_action(statement)};
  return Maybe<pending_action>{None};
}

fn Store::set_pending_status(i64 id, StringView status) -> ErrorOr<Ok>
{
  let statement = TRY(m_database.prepare(
      "UPDATE pending_actions SET status = ? WHERE id = ?;"));
  statement.bind(status);
  statement.bind(id);
  unused(TRY(statement.step()));

  LOG(Info, "pending action %lld set to %.*s", static_cast<long long>(id),
      static_cast<int>(status.count()), status.data);
  return Success;
}

fn Store::record_audit(StringView actor, StringView actor_ip, StringView action,
                       StringView target, StringView detail, i64 created_at)
    -> ErrorOr<Ok>
{
  let statement = TRY(m_database.prepare(
      "INSERT INTO audit_log (actor, actor_ip, action, target, detail, "
      "created_at) VALUES (?, ?, ?, ?, ?, ?);"));
  statement.bind(actor);
  statement.bind(actor_ip);
  statement.bind(action);
  statement.bind(target);
  statement.bind(detail);
  statement.bind(created_at);
  unused(TRY(statement.step()));

  LOG(Info, "audit recorded, actor=%.*s ip=%.*s action=%.*s target=%.*s",
      static_cast<int>(actor.count()), actor.data,
      static_cast<int>(actor_ip.count()), actor_ip.data,
      static_cast<int>(action.count()), action.data,
      static_cast<int>(target.count()), target.data);
  return Success;
}

fn Store::list_audit(i64 limit_count) const -> ErrorOr<ArrayList<audit_entry>>
{
  ArrayList<audit_entry> entries{m_allocator};
  let statement = TRY(m_database.prepare(
      "SELECT id, actor, actor_ip, action, target, detail, created_at "
      "FROM audit_log ORDER BY id DESC LIMIT ?;"));
  statement.bind(limit_count);

  while (TRY(statement.step()))
    entries.push(read_audit_entry(statement));
  return entries;
}

fn Store::add_comment(StringView author_identity, StringView author_name,
                      StringView body, i64 created_at) -> ErrorOr<Ok>
{
  let statement = TRY(m_database.prepare(
      "INSERT INTO comments (author_identity, author_name, body, created_at) "
      "VALUES (?, ?, ?, ?);"));
  statement.bind(author_identity);
  statement.bind(author_name);
  statement.bind(body);
  statement.bind(created_at);
  unused(TRY(statement.step()));

  LOG(Info, "comment added, author=%.*s",
      static_cast<int>(author_identity.count()), author_identity.data);
  return Success;
}

fn Store::list_comments(i64 limit_count) const -> ErrorOr<ArrayList<comment>>
{
  ArrayList<comment> comments{m_allocator};
  let statement = TRY(m_database.prepare(
      "SELECT id, author_identity, author_name, body, created_at "
      "FROM comments ORDER BY id DESC LIMIT ?;"));
  statement.bind(limit_count);

  while (TRY(statement.step()))
    comments.push(read_comment(statement));
  return comments;
}

} // namespace wr
