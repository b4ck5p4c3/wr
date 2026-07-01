#include "Store.hpp"

#include "Trace.hpp"

namespace wr {

fn source_oauth(identity_source source) -> StringView
{
  if (source == identity_source::telegram) return "telegram";
  return "github";
}

namespace {

fn bind_identity(SqlStatement &statement, const identity &who) -> void
{
  statement.bind(static_cast<i64>(who.source));
  statement.bind(who.name.view());
}

fn read_identity(SqlStatement &statement) -> identity
{
  identity who{};
  who.source = static_cast<identity_source>(statement.get<i64>());
  who.name = statement.get<String>();
  return who;
}

fn read_site(SqlStatement &statement) -> site
{
  site row{};
  row.slug = statement.get<String>();
  row.name = statement.get<String>();
  row.url = statement.get<String>();
  row.description = statement.get<String>();
  row.is_reachable = statement.get<i64>() != 0;
  row.last_seen_at = statement.get<i64>();
  row.owner = read_identity(statement);
  row.created_at = statement.get<i64>();
  return row;
}

fn read_pending_action(SqlStatement &statement) -> pending_action
{
  pending_action row{};
  row.id = statement.get<i64>();
  row.kind = statement.get<String>();
  row.owner = read_identity(statement);
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
  row.actor = read_identity(statement);
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
  row.author = read_identity(statement);
  row.body = statement.get<String>();
  row.created_at = statement.get<i64>();
  row.is_approved = statement.get<i64>() != 0;
  return row;
}

} // namespace

/* The schema is one idempotent baseline. Every statement is a CREATE IF NOT
   EXISTS, so a boot against a database that already holds it does nothing. */
static const char *const SCHEMA =
    "CREATE TABLE IF NOT EXISTS sites ("
    "  slug TEXT PRIMARY KEY,"
    "  name TEXT NOT NULL,"
    "  url TEXT NOT NULL,"
    "  is_reachable INTEGER NOT NULL DEFAULT 1,"
    "  last_seen_at INTEGER NOT NULL DEFAULT 0,"
    "  owner_source INTEGER NOT NULL DEFAULT 0,"
    "  owner_name TEXT NOT NULL DEFAULT '',"
    "  created_at INTEGER NOT NULL DEFAULT 0,"
    "  is_deleted INTEGER NOT NULL DEFAULT 0,"
    "  description TEXT NOT NULL DEFAULT '');"
    "CREATE TABLE IF NOT EXISTS accounts ("
    "  source INTEGER NOT NULL,"
    "  name TEXT NOT NULL,"
    "  is_admin INTEGER NOT NULL DEFAULT 0,"
    "  PRIMARY KEY (source, name));"
    "CREATE TABLE IF NOT EXISTS oauth_sources ("
    "  source INTEGER PRIMARY KEY,"
    "  name TEXT NOT NULL);"
    "INSERT OR IGNORE INTO oauth_sources (source, name) VALUES "
    "  (0, 'github'), (1, 'telegram'), (2, 'dev');"
    "CREATE TABLE IF NOT EXISTS wr ("
    "  key TEXT PRIMARY KEY,"
    "  value TEXT NOT NULL);"
    "CREATE TABLE IF NOT EXISTS sessions ("
    "  token TEXT PRIMARY KEY,"
    "  source INTEGER NOT NULL,"
    "  name TEXT NOT NULL,"
    "  expires_at INTEGER NOT NULL);"
    "CREATE TABLE IF NOT EXISTS pending_actions ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  kind TEXT NOT NULL,"
    "  owner_source INTEGER NOT NULL DEFAULT 0,"
    "  owner_name TEXT NOT NULL DEFAULT '',"
    "  target_slug TEXT NOT NULL DEFAULT '',"
    "  payload TEXT NOT NULL DEFAULT '',"
    "  created_at INTEGER NOT NULL,"
    "  status TEXT NOT NULL DEFAULT 'pending');"
    "CREATE TABLE IF NOT EXISTS liveness_buckets ("
    "  slug TEXT NOT NULL,"
    "  hour_bucket INTEGER NOT NULL,"
    "  up_count INTEGER NOT NULL DEFAULT 0,"
    "  probe_count INTEGER NOT NULL DEFAULT 0,"
    "  PRIMARY KEY (slug, hour_bucket));"
    "CREATE TABLE IF NOT EXISTS reactions ("
    "  slug TEXT NOT NULL,"
    "  emoji TEXT NOT NULL,"
    "  source INTEGER NOT NULL,"
    "  name TEXT NOT NULL,"
    "  PRIMARY KEY (slug, emoji, source, name));"
    "CREATE TABLE IF NOT EXISTS audit_log ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  actor_source INTEGER NOT NULL DEFAULT 0,"
    "  actor_name TEXT NOT NULL DEFAULT '',"
    "  action TEXT NOT NULL,"
    "  target TEXT NOT NULL DEFAULT '',"
    "  detail TEXT NOT NULL DEFAULT '',"
    "  created_at INTEGER NOT NULL,"
    "  actor_ip TEXT NOT NULL DEFAULT '');"
    "CREATE TABLE IF NOT EXISTS comments ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  author_source INTEGER NOT NULL DEFAULT 0,"
    "  author_name TEXT NOT NULL DEFAULT '',"
    "  body TEXT NOT NULL,"
    "  created_at INTEGER NOT NULL,"
    "  is_approved INTEGER NOT NULL DEFAULT 0);"
    "CREATE TABLE IF NOT EXISTS site_metrics ("
    "  slug TEXT PRIMARY KEY,"
    "  click_count INTEGER NOT NULL DEFAULT 0,"
    "  hop_count INTEGER NOT NULL DEFAULT 0);"
    "CREATE INDEX IF NOT EXISTS index_sites_owner ON "
    "sites(owner_source, owner_name);"
    "CREATE INDEX IF NOT EXISTS index_sites_reachable ON sites(is_reachable);"
    "CREATE INDEX IF NOT EXISTS index_pending_owner ON "
    "pending_actions(owner_source, owner_name);"
    "CREATE INDEX IF NOT EXISTS index_pending_status ON "
    "pending_actions(status);"
    "CREATE INDEX IF NOT EXISTS index_sessions_expires ON sessions(expires_at);"
    "CREATE INDEX IF NOT EXISTS index_liveness_buckets_hour ON "
    "liveness_buckets(hour_bucket);"
    "CREATE INDEX IF NOT EXISTS index_reactions_slug ON reactions(slug);"
    "CREATE INDEX IF NOT EXISTS index_audit_created ON audit_log(created_at);"
    "CREATE INDEX IF NOT EXISTS index_comments_created ON "
    "comments(created_at);";

/* A probe is bucketed by the hour, and seven days of buckets are kept. */
static constexpr i64 LIVENESS_WINDOW_HOURS = 168;

fn Store::migrate() -> ErrorOr<Ok>
{
  TRY(m_database.execute(SCHEMA));
  LOG(Info, "schema ensured");
  return Success;
}

fn Store::check_api_version() -> ErrorOr<Ok>
{
  let const expected = StringView{WR_STRINGIFY(WR_API_VERSION)};

  Maybe<String> stored;
  {
    let read = TRY(m_database.prepare("SELECT value FROM wr WHERE key = ?;"));
    read.bind(StringView{"api_version"});
    if (TRY(read.step())) stored = read.get<String>();
  }

  if (stored.has_value()) {
    let const &current = stored.value();
    if (current.view() != expected) {
      String message{m_allocator};
      message.append("The database API version ");
      message.append(current.view());
      message.append(" does not match the server API version ");
      message.append(expected);
      message.append(", the database must be migrated");

      Error error{message.view()};
      return error.as_critical();
    }

    LOG(Info, "database api version matches, version=%.*s",
        static_cast<int>(expected.count()), expected.data);
  } else {
    let seed =
        TRY(m_database.prepare("INSERT INTO wr (key, value) VALUES (?, ?);"));
    seed.bind(StringView{"api_version"});
    seed.bind(expected);
    unused(TRY(seed.step()));

    LOG(Info, "database api version seeded, version=%.*s",
        static_cast<int>(expected.count()), expected.data);
  }

  let info = TRY(m_database.prepare(
      "INSERT OR REPLACE INTO wr (key, value) VALUES (?, ?);"));
  info.bind(StringView{"version"});
  info.bind(StringView{WR_VERSION_STRING});
  unused(TRY(info.step()));

  return Success;
}

fn Store::list_active_sites() const -> ErrorOr<ArrayList<site>>
{
  ArrayList<site> sites{m_allocator};
  let statement = TRY(m_database.prepare(
      "SELECT slug, name, url, description, is_reachable, last_seen_at, "
      "owner_source, owner_name, created_at FROM sites "
      "WHERE is_reachable = 1 AND is_deleted = 0 ORDER BY created_at, slug;"));

  while (TRY(statement.step()))
    sites.push(read_site(statement));

  return sites;
}

fn Store::list_all_sites() const -> ErrorOr<ArrayList<site>>
{
  ArrayList<site> sites{m_allocator};
  let statement = TRY(m_database.prepare(
      "SELECT slug, name, url, description, is_reachable, last_seen_at, "
      "owner_source, owner_name, created_at FROM sites "
      "WHERE is_deleted = 0 ORDER BY created_at, slug;"));

  while (TRY(statement.step()))
    sites.push(read_site(statement));

  return sites;
}

fn Store::list_sites_for_owner(const identity &owner) const
    -> ErrorOr<ArrayList<site>>
{
  ArrayList<site> sites{m_allocator};
  let statement = TRY(m_database.prepare(
      "SELECT slug, name, url, description, is_reachable, last_seen_at, "
      "owner_source, owner_name, created_at FROM sites "
      "WHERE owner_source = ? AND owner_name = ? AND is_deleted = 0 "
      "ORDER BY created_at, slug;"));
  bind_identity(statement, owner);

  while (TRY(statement.step()))
    sites.push(read_site(statement));

  return sites;
}

fn Store::find_site(StringView slug) const -> ErrorOr<Maybe<site>>
{
  let statement = TRY(m_database.prepare(
      "SELECT slug, name, url, description, is_reachable, last_seen_at, "
      "owner_source, owner_name, created_at FROM sites "
      "WHERE slug = ? AND is_deleted = 0;"));
  statement.bind(slug);

  if (TRY(statement.step())) return Maybe<site>{read_site(statement)};

  return Maybe<site>{None};
}

fn Store::upsert_site(const site &row) -> ErrorOr<Ok>
{
  let const sql =
      "INSERT INTO sites "
      "(slug, name, url, description, is_reachable, last_seen_at, "
      "owner_source, owner_name, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, "
      "?) "
      "ON CONFLICT(slug) DO UPDATE SET name = excluded.name, "
      "url = excluded.url, description = excluded.description, "
      "owner_source = excluded.owner_source, owner_name = excluded.owner_name, "
      "is_deleted = 0;";

  let statement = TRY(m_database.prepare(sql));
  statement.bind(row.slug.view());
  statement.bind(row.name.view());
  statement.bind(row.url.view());
  statement.bind(row.description.view());
  statement.bind(row.is_reachable);
  statement.bind(row.last_seen_at);
  bind_identity(statement, row.owner);
  statement.bind(row.created_at);
  unused(TRY(statement.step()));

  LOG(Info, "site upserted, slug=%s owner=%s", row.slug.c_str(),
      row.owner.name.c_str());
  return Success;
}

fn Store::update_site_details(StringView slug, StringView name, StringView url,
                              StringView description) -> ErrorOr<Ok>
{
  let statement = TRY(m_database.prepare("UPDATE sites SET name = ?, url = ?, "
                                         "description = ? WHERE slug = ? AND "
                                         "is_deleted = 0;"));
  statement.bind(name);
  statement.bind(url);
  statement.bind(description);
  statement.bind(slug);
  unused(TRY(statement.step()));

  LOG(Info, "site details updated, slug=%.*s name=%.*s",
      static_cast<int>(slug.count()), slug.data, static_cast<int>(name.count()),
      name.data);
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
                          const identity &who) -> ErrorOr<bool>
{
  /* The probe statement is leased from the per-connection cache, so it is
     scoped and returned before the write statement is prepared. */
  bool is_set = false;
  {
    let probe = TRY(m_database.prepare(
        "SELECT 1 FROM reactions WHERE slug = ? AND emoji = ? AND "
        "source = ? AND name = ?;"));
    probe.bind(slug);
    probe.bind(emoji);
    bind_identity(probe, who);
    is_set = TRY(probe.step());
  }

  if (is_set) {
    let statement = TRY(m_database.prepare(
        "DELETE FROM reactions WHERE slug = ? AND emoji = ? AND "
        "source = ? AND name = ?;"));
    statement.bind(slug);
    statement.bind(emoji);
    bind_identity(statement, who);
    unused(TRY(statement.step()));

    LOG(Info, "reaction removed, slug=%.*s emoji=%.*s",
        static_cast<int>(slug.count()), slug.data,
        static_cast<int>(emoji.count()), emoji.data);
    return false;
  }

  let statement = TRY(m_database.prepare("INSERT INTO reactions (slug, emoji, "
                                         "source, name) VALUES (?, ?, ?, ?);"));
  statement.bind(slug);
  statement.bind(emoji);
  bind_identity(statement, who);
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

fn Store::get_user_reactions(StringView slug, const identity &who) const
    -> ErrorOr<ArrayList<String>>
{
  ArrayList<String> emojis{m_allocator};

  let statement = TRY(m_database.prepare(
      "SELECT emoji FROM reactions WHERE slug = ? AND source = ? AND name = ? "
      "ORDER BY emoji;"));
  statement.bind(slug);
  bind_identity(statement, who);
  while (TRY(statement.step()))
    emojis.push(statement.get<String>());

  return emojis;
}

fn Store::get_all_reactions() const
    -> ErrorOr<StringMap<ArrayList<reaction_count>>>
{
  StringMap<ArrayList<reaction_count>> by_slug{m_allocator};

  let statement = TRY(m_database.prepare(
      "SELECT slug, emoji, COUNT(*) FROM reactions GROUP BY slug, emoji "
      "ORDER BY slug, emoji;"));
  while (TRY(statement.step())) {
    let const slug = statement.get<String>();
    reaction_count entry{};
    entry.emoji = statement.get<String>();
    entry.count = statement.get<i64>();
    by_slug.get_or_create(slug.view(), ArrayList<reaction_count>{m_allocator})
        .push(steal(entry));
  }

  return by_slug;
}

fn Store::get_all_user_reactions(const identity &who) const
    -> ErrorOr<StringMap<ArrayList<String>>>
{
  StringMap<ArrayList<String>> by_slug{m_allocator};

  let statement = TRY(m_database.prepare(
      "SELECT slug, emoji FROM reactions WHERE source = ? AND name = ? "
      "ORDER BY slug, emoji;"));
  bind_identity(statement, who);
  while (TRY(statement.step())) {
    let const slug = statement.get<String>();
    by_slug.get_or_create(slug.view(), ArrayList<String>{m_allocator})
        .push(statement.get<String>());
  }

  return by_slug;
}

fn Store::record_click(StringView slug) -> ErrorOr<Ok>
{
  let statement = TRY(m_database.prepare(
      "INSERT INTO site_metrics (slug, click_count, hop_count) "
      "VALUES (?, 1, 0) "
      "ON CONFLICT(slug) DO UPDATE SET click_count = click_count + 1;"));
  statement.bind(slug);
  unused(TRY(statement.step()));

  LOG(Info, "click recorded, slug=%.*s", static_cast<int>(slug.count()),
      slug.data);
  return Success;
}

fn Store::record_hop(StringView slug) -> ErrorOr<Ok>
{
  let statement = TRY(m_database.prepare(
      "INSERT INTO site_metrics (slug, click_count, hop_count) "
      "VALUES (?, 0, 1) "
      "ON CONFLICT(slug) DO UPDATE SET hop_count = hop_count + 1;"));
  statement.bind(slug);
  unused(TRY(statement.step()));

  LOG(Info, "hop recorded, slug=%.*s", static_cast<int>(slug.count()),
      slug.data);
  return Success;
}

fn Store::get_site_metrics() const -> ErrorOr<ArrayList<site_metric>>
{
  ArrayList<site_metric> metrics{m_allocator};

  let statement = TRY(m_database.prepare(
      "SELECT slug, click_count, hop_count FROM site_metrics "
      "ORDER BY click_count DESC, hop_count DESC, slug;"));
  while (TRY(statement.step())) {
    site_metric entry{};
    entry.slug = statement.get<String>();
    entry.click_count = statement.get<i64>();
    entry.hop_count = statement.get<i64>();
    metrics.push(steal(entry));
  }

  return metrics;
}

fn Store::get_click_count(StringView slug) const -> ErrorOr<i64>
{
  let statement = TRY(m_database.prepare(
      "SELECT click_count FROM site_metrics WHERE slug = ?;"));
  statement.bind(slug);

  if (TRY(statement.step())) return statement.get<i64>();

  return i64{0};
}

fn Store::find_account(const identity &who) const -> ErrorOr<Maybe<account>>
{
  let statement =
      TRY(m_database.prepare("SELECT source, name, is_admin FROM accounts "
                             "WHERE source = ? AND name = ?;"));
  bind_identity(statement, who);
  if (TRY(statement.step())) {
    account row{};
    row.who = read_identity(statement);
    row.is_admin = statement.get<i64>() != 0;
    return Maybe<account>{steal(row)};
  }
  return Maybe<account>{None};
}

fn Store::upsert_account(const identity &who, bool is_admin) -> ErrorOr<Ok>
{
  let statement =
      TRY(m_database.prepare("INSERT INTO accounts (source, name, is_admin) "
                             "VALUES (?, ?, ?) "
                             "ON CONFLICT(source, name) DO UPDATE SET "
                             "is_admin = excluded.is_admin;"));
  bind_identity(statement, who);
  statement.bind(is_admin);
  unused(TRY(statement.step()));

  LOG(Info, "account upserted, source=%d name=%s is_admin=%d",
      static_cast<int>(who.source), who.name.c_str(), is_admin ? 1 : 0);
  return Success;
}

fn Store::create_session(StringView token, const identity &who, i64 expires_at)
    -> ErrorOr<Ok>
{
  let statement = TRY(m_database.prepare(
      "INSERT INTO sessions (token, source, name, expires_at) "
      "VALUES (?, ?, ?, ?);"));
  statement.bind(token);
  bind_identity(statement, who);
  statement.bind(expires_at);
  unused(TRY(statement.step()));

  LOG(Info, "session opened for source=%d name=%s",
      static_cast<int>(who.source), who.name.c_str());
  return Success;
}

fn Store::find_session(StringView token) const -> ErrorOr<Maybe<session>>
{
  let statement = TRY(m_database.prepare(
      "SELECT token, source, name, expires_at FROM sessions WHERE token = ?;"));
  statement.bind(token);
  if (TRY(statement.step())) {
    session row{};
    row.token = statement.get<String>();
    row.who = read_identity(statement);
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
      "SELECT id, kind, owner_source, owner_name, target_slug, payload, "
      "created_at, status "
      "FROM pending_actions WHERE status = 'pending' ORDER BY created_at;"));

  while (TRY(statement.step()))
    actions.push(read_pending_action(statement));
  return actions;
}

fn Store::list_pending_for_owner(const identity &owner) const
    -> ErrorOr<ArrayList<pending_action>>
{
  ArrayList<pending_action> actions{m_allocator};
  let statement = TRY(m_database.prepare(
      "SELECT id, kind, owner_source, owner_name, target_slug, payload, "
      "created_at, status "
      "FROM pending_actions WHERE owner_source = ? AND owner_name = ? AND "
      "status = 'pending' ORDER BY created_at;"));
  bind_identity(statement, owner);

  while (TRY(statement.step()))
    actions.push(read_pending_action(statement));
  return actions;
}

fn Store::add_pending(StringView kind, const identity &owner,
                      StringView target_slug, StringView payload,
                      i64 created_at) -> ErrorOr<Ok>
{
  let statement = TRY(m_database.prepare(
      "INSERT INTO pending_actions (kind, owner_source, owner_name, "
      "target_slug, payload, created_at, status) "
      "VALUES (?, ?, ?, ?, ?, ?, 'pending');"));
  statement.bind(kind);
  bind_identity(statement, owner);
  statement.bind(target_slug);
  statement.bind(payload);
  statement.bind(created_at);
  unused(TRY(statement.step()));

  LOG(Info, "pending action recorded, kind=%.*s owner=%s target=%.*s",
      static_cast<int>(kind.count()), kind.data, owner.name.c_str(),
      static_cast<int>(target_slug.count()), target_slug.data);
  return Success;
}

fn Store::find_pending(i64 id) const -> ErrorOr<Maybe<pending_action>>
{
  let statement = TRY(m_database.prepare(
      "SELECT id, kind, owner_source, owner_name, target_slug, payload, "
      "created_at, status "
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

fn Store::record_audit(const identity &actor, StringView actor_ip,
                       StringView action, StringView target, StringView detail,
                       i64 created_at) -> ErrorOr<Ok>
{
  let statement = TRY(m_database.prepare(
      "INSERT INTO audit_log (actor_source, actor_name, actor_ip, action, "
      "target, detail, created_at) VALUES (?, ?, ?, ?, ?, ?, ?);"));
  bind_identity(statement, actor);
  statement.bind(actor_ip);
  statement.bind(action);
  statement.bind(target);
  statement.bind(detail);
  statement.bind(created_at);
  unused(TRY(statement.step()));

  LOG(Info, "audit recorded, actor=%s ip=%.*s action=%.*s target=%.*s",
      actor.name.c_str(), static_cast<int>(actor_ip.count()), actor_ip.data,
      static_cast<int>(action.count()), action.data,
      static_cast<int>(target.count()), target.data);
  return Success;
}

fn Store::list_audit(i64 limit_count) const -> ErrorOr<ArrayList<audit_entry>>
{
  ArrayList<audit_entry> entries{m_allocator};
  let statement = TRY(m_database.prepare(
      "SELECT id, actor_source, actor_name, actor_ip, action, target, detail, "
      "created_at FROM audit_log ORDER BY id DESC LIMIT ?;"));
  statement.bind(limit_count);

  while (TRY(statement.step()))
    entries.push(read_audit_entry(statement));

  return entries;
}

fn Store::add_comment(const identity &author, StringView body, bool is_approved,
                      i64 created_at) -> ErrorOr<Ok>
{
  let statement = TRY(m_database.prepare(
      "INSERT INTO comments "
      "(author_source, author_name, body, is_approved, created_at) "
      "VALUES (?, ?, ?, ?, ?);"));
  bind_identity(statement, author);
  statement.bind(body);
  statement.bind(is_approved);
  statement.bind(created_at);
  unused(TRY(statement.step()));

  LOG(Info, "comment added, author=%s", author.name.c_str());
  return Success;
}

fn Store::list_comments(i64 limit_count, i64 offset_count) const
    -> ErrorOr<ArrayList<comment>>
{
  ArrayList<comment> comments{m_allocator};
  let statement = TRY(m_database.prepare(
      "SELECT id, author_source, author_name, body, created_at, is_approved "
      "FROM comments WHERE is_approved = 1 ORDER BY id DESC LIMIT ? OFFSET "
      "?;"));
  statement.bind(limit_count);
  statement.bind(offset_count);

  while (TRY(statement.step()))
    comments.push(read_comment(statement));

  return comments;
}

fn Store::list_pending_comments(i64 limit_count) const
    -> ErrorOr<ArrayList<comment>>
{
  ArrayList<comment> comments{m_allocator};
  let statement = TRY(m_database.prepare(
      "SELECT id, author_source, author_name, body, created_at, is_approved "
      "FROM comments WHERE is_approved = 0 ORDER BY id DESC LIMIT ?;"));
  statement.bind(limit_count);

  while (TRY(statement.step()))
    comments.push(read_comment(statement));

  return comments;
}

fn Store::find_comment(i64 id) const -> ErrorOr<Maybe<comment>>
{
  let statement = TRY(m_database.prepare(
      "SELECT id, author_source, author_name, body, created_at, is_approved "
      "FROM comments WHERE id = ?;"));
  statement.bind(id);
  if (TRY(statement.step())) return Maybe<comment>{read_comment(statement)};
  return Maybe<comment>{None};
}

fn Store::approve_comment(i64 id) -> ErrorOr<Ok>
{
  let statement = TRY(
      m_database.prepare("UPDATE comments SET is_approved = 1 WHERE id = ?;"));
  statement.bind(id);
  unused(TRY(statement.step()));

  LOG(Info, "comment approved, id=%lld", static_cast<long long>(id));
  return Success;
}

fn Store::delete_comment(i64 id) -> ErrorOr<Ok>
{
  let statement = TRY(m_database.prepare("DELETE FROM comments WHERE id = ?;"));
  statement.bind(id);
  unused(TRY(statement.step()));

  LOG(Info, "comment deleted, id=%lld", static_cast<long long>(id));
  return Success;
}

fn Store::clear_statement_cache() noexcept -> void
{
  m_database.clear_statement_cache();
}

} // namespace wr
