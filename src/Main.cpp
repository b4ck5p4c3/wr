#include "App.hpp"
#include "Cli.hpp"
#include "Common.hpp"
#include "Curl.hpp"
#include "ErrorOr.hpp"
#include "Liveness.hpp"
#include "Mongoose.hpp"
#include "Path.hpp"
#include "Sqlite.hpp"
#include "Store.hpp"
#include "Trace.hpp"

using namespace wr;

FLAG_LIST_DECL();
HELP_DESCRIPTION_DECL("wr, a webring server backend.");
HELP_SYNOPSIS_DECL("[options]");

FLAG(HELP, Bool, '\0', "help", "Show this help and exit.");
FLAG(VERSION, Bool, '\0', "version", "Show the version and exit.");
FLAG(LISTEN, String, 'l', "listen", Server,
     "URL to listen on, like http://0.0.0.0:8000 (required).");
FLAG(DATABASE, String, 'd', "database", Server,
     "Path to the sqlite database (required).");
FLAG(PUBLICURL, String, 'u', "public-url", Server,
     "Public base URL for the OAuth redirect (required).");
FLAG(LOGFILE, String, 'L', "log-file", Debug,
     "Append the log to FILE instead of standard error.");
FLAG(VERBOSE, Bool, 'v', "verbose", Debug,
     "Raise the log verbosity, repeat for more. A single -v adds the "
     "per-request access lines and -vv traces everything.");
FLAG(DEV, Bool, 'D', "dev", Debug,
     "Run in dev mode with a login bypass for an admin and a user.");

namespace {

fn env_or(const char *name, const char *fallback) -> const char *
{
  let const value = std::getenv(name);
  return value != nullptr ? value : fallback;
}

[[noreturn]] fn fail(StringView message) -> void
{
  show_message(message);
  exit(1);
}

} // namespace

fn main(int argc, char **argv) -> int
{
  let parse_result = parse_flags(FLAG_LIST, argc, argv);
  if (parse_result.is_error()) {
    show_message(parse_result.error().message());
    return 1;
  }

  if (FLAG_HELP.is_enabled()) {
    print(make_synopsis("wr", HELP_SYNOPSIS));
    print("\n");
    print("  ");
    print(HELP_DESCRIPTION);
    print("\n\n");
    print(make_flag_help(FLAG_LIST));
    print("\n");
    flush();
    return 0;
  }
  if (FLAG_VERSION.is_enabled()) {
    show_version();
    return 0;
  }

  if (FLAG_VERBOSE.count() >= 2)
    LOGGER_VERBOSITY = verbosity::All;
  else if (FLAG_VERBOSE.count() == 1)
    LOGGER_VERBOSITY = verbosity::Debug;

  if (FLAG_LOGFILE.is_set()) {
    let const log_path = String{FLAG_LOGFILE.value()};
    let log_result = set_log_file(Path{log_path});
    if (log_result.is_error()) fail(log_result.error().message());
  }

  let const allocator = heap_allocator();

  std::srand(static_cast<u64>(getpid()));

  if (!FLAG_LISTEN.is_set() || !FLAG_DATABASE.is_set() ||
      !FLAG_PUBLICURL.is_set())
  {
    String message{allocator};
    message.append("error: These options are required:\n");
    let const do_note = [&](const FlagString &flag, const char *line) {
      if (flag.is_set()) return;
      message.append("  ");
      message.append(line);
      message.append("\n");
    };
    do_note(FLAG_LISTEN, "--listen <url>");
    do_note(FLAG_DATABASE, "--database <path>");
    do_note(FLAG_PUBLICURL, "--public-url <url>");
    message.append("\nSee --help for info.");
    fail(message.view());
  }

  config cfg;
  cfg.listen_url = String{allocator, FLAG_LISTEN.value()};
  cfg.database_path = String{allocator, FLAG_DATABASE.value()};
  cfg.public_base_url = String{allocator, FLAG_PUBLICURL.value()};
  cfg.github_client_id = String{allocator, env_or("WR_GITHUB_CLIENT_ID", "")};
  cfg.github_client_secret =
      String{allocator, env_or("WR_GITHUB_CLIENT_SECRET", "")};
  cfg.telegram_bot_token =
      String{allocator, env_or("WR_TELEGRAM_BOT_TOKEN", "")};
  cfg.session_key = String{allocator, env_or("WR_SESSION_KEY", "")};
  cfg.is_dev_mode = FLAG_DEV.is_enabled();

  LOG(Info, "wr is starting up");
  if (cfg.is_dev_mode) LOG(Info, "dev mode is on, the login bypass is enabled");

  /* Without a provider a non-dev server can authenticate nobody. */
  let const has_github = !cfg.github_client_id.view().is_empty() &&
                         !cfg.github_client_secret.view().is_empty();
  let const has_telegram = !cfg.telegram_bot_token.view().is_empty();
  if (!cfg.is_dev_mode && !has_github && !has_telegram) {
    String message{allocator};
    message.append("error: No login provider is configured. Set one of:\n");
    message.append("  WR_GITHUB_CLIENT_ID and WR_GITHUB_CLIENT_SECRET\n");
    message.append("  WR_TELEGRAM_BOT_TOKEN\n");
    message.append("\nOr pass --dev to run with the login bypass.");
    fail(message.view());
  }

  /* A session cookie is signed with the session key, so a non-dev server with
     an empty key cannot keep a login secret. */
  if (!cfg.is_dev_mode && cfg.session_key.view().is_empty()) {
    show_message("error: WR_SESSION_KEY must be set outside of dev mode.");
    return 1;
  }

  Sqlite database{allocator};
  let database_result = database.open(cfg.database_path.view());
  if (database_result.is_error())
    fail(database_result.error().message().view());

  Store store{allocator, database};
  let migrate_result = store.migrate();
  if (migrate_result.is_error()) fail(migrate_result.error().message().view());

  /* The OAuth exchange runs on a request thread, so the connect phase is
     bounded to keep a stalled provider from pinning a worker. */
  CurlClient::Options client_options;
  client_options.connect_timeout_ms = 10000;
  CurlClient client{allocator, client_options};
  App app{allocator, store, client, cfg};

  MongooseServer server{allocator};
  let listen_result = server.listen(cfg.listen_url.view(), App::on_event, &app);
  if (listen_result.is_error()) fail(listen_result.error().message().view());

  /* The sweep gets its own client with a short timeout, used only on its own
     thread. */
  CurlClient::Options probe_options;
  probe_options.timeout_ms = 10000;
  probe_options.connect_timeout_ms = 5000;
  probe_options.should_reject_private_addresses = true;
  CurlClient probe_client{allocator, probe_options};
  Liveness liveness{allocator, cfg, probe_client};

  let liveness_ret = liveness.start();
  if (liveness_ret.is_error()) fail(liveness_ret.error().message());

  let run_result = server.run(); /* blocks */

  liveness.stop();

  if (run_result.is_error()) fail(run_result.error().message().view());

  return 0;
}
