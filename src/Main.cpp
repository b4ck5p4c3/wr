#include "App.hpp"
#include "Cli.hpp"
#include "Common.hpp"
#include "Curl.hpp"
#include "Embedded.hpp"
#include "ErrorOr.hpp"
#include "Liveness.hpp"
#include "Mongoose.hpp"
#include "Path.hpp"
#include "Sqlite.hpp"
#include "Store.hpp"
#include "Trace.hpp"

#include <signal.h>

using namespace wr;

FLAG_LIST_DECL();
HELP_DESCRIPTION_DECL("wr, a webring server backend.");
HELP_SYNOPSIS_DECL("[options]");

FLAG(HELP, Bool, '\0', "help", "Show this help and exit.");
FLAG(VERSION, Bool, '\0', "version", "Show the version and exit.");
FLAG(LISTEMBEDDED, Bool, '\0', "list-embedded-assets",
     "List the embedded frontend assets with their sizes and exit.");
FLAG(LISTEN, String, 'l', "listen-address", Server,
     "URL to listen on, like http://0.0.0.0:8000 (required). Or set "
     "WR_LISTEN_ADDRESS.");
FLAG(DATABASE, String, 'd', "database-url", Server,
     "Path or URL to the database (required). Or set WR_DATABASE_URL.");
FLAG(DBBACKEND, String, 'D', "database-backend", Server,
     "Database backend. One of: sqlite (default sqlite). Or set "
     "WR_DATABASE_BACKEND.");
FLAG(PUBLICURL, String, 'u', "web-root-url", Server,
     "Public base URL for the OAuth redirect, defaults to the listen URL. Or "
     "set WR_WEB_ROOT_URL.");
FLAG(LOGFILE, String, 'L', "log-file", Debug,
     "Append the log to FILE instead of standard error.");
FLAG(VERBOSE, Bool, 'v', "verbose", Debug,
     "Raise the log verbosity, repeat for more. A single -v adds the "
     "per-request access lines and -vv traces everything.");
FLAG(DEV, Bool, '\0', "enable-dangerous-developer-environment", Debug,
     "Run with a login bypass for an admin and a user. Dangerous, never use in "
     "production.");
FLAG(METRICS, Bool, '\0', "enable-metrics", Server,
     "Record the per-site click and hop metrics shown in the admin panel.");
FLAG(TRUSTFWD, Bool, '\0', "trust-forwarded-headers", Server,
     "Trust the x-forwarded-for and x-real-ip headers for the client address. "
     "Set this only behind a reverse proxy that overwrites them.");

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

/* The running server, reached by the shutdown handler. As docker PID 1 the
   process is delivered no default disposition for a termination signal, so the
   handler is what lets the container stop. */
HttpServer *SHUTDOWN_SERVER = nullptr;

extern "C" fn request_shutdown(int signal_number) -> void
{
  (void) signal_number;
  if (SHUTDOWN_SERVER != nullptr) SHUTDOWN_SERVER->request_stop();
}

fn install_shutdown_handlers(HttpServer &server) -> void
{
  SHUTDOWN_SERVER = &server;

  struct sigaction action{};
  action.sa_handler = request_shutdown;
  sigemptyset(&action.sa_mask);
  /* SA_RESTART is left off, so the blocking poll is interrupted and the loop
     returns at once. */
  action.sa_flags = 0;

  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGTERM, &action, nullptr);
  sigaction(SIGHUP, &action, nullptr);
}

fn clear_shutdown_handlers() -> void
{
  SHUTDOWN_SERVER = nullptr;

  struct sigaction action{};
  action.sa_handler = SIG_DFL;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;

  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGTERM, &action, nullptr);
  sigaction(SIGHUP, &action, nullptr);
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
    print("\n(c) toiletbril <https://github.com/toiletbril>\n");
    print("    and b4cksp4ce contributors <https://github.com/b4ck5p4c3/wr>\n");
    flush();
    return 0;
  }
  if (FLAG_LISTEMBEDDED.is_enabled()) {
    usize total_size = 0;
    for (usize i = 0; i < EMBEDDED_ASSET_COUNT; i++) {
      let const &asset = EMBEDDED_ASSETS[i];
      char size_text[24];
      std::snprintf(size_text, sizeof(size_text), "%8.1f kB  ",
                    static_cast<double>(asset.size) / 1000.0);
      print(size_text);
      print(asset.path);
      print("\n");
      total_size += asset.size;
    }
    char summary[64];
    std::snprintf(summary, sizeof(summary), "\n%.1f kB total, %zu assets\n",
                  static_cast<double>(total_size) / 1000.0,
                  static_cast<usize>(EMBEDDED_ASSET_COUNT));
    print(summary);
    flush();
    return 0;
  }

  if (FLAG_VERBOSE.count() >= 3)
    LOGGER_VERBOSITY = verbosity::Everything;
  else if (FLAG_VERBOSE.count() == 1)
    LOGGER_VERBOSITY = verbosity::Debug;

  if (FLAG_LOGFILE.is_set()) {
    let const log_path = String{FLAG_LOGFILE.value()};
    let log_result = set_log_file(Path{log_path});
    if (log_result.is_error()) fail(log_result.error().message());
  }

  let const allocator = heap_allocator();

  std::srand(static_cast<u64>(getpid()));

  /* A server option falls back to WR_ plus the long flag uppercased, so the
     server runs from the environment with no arguments. The CLI value wins. */
  let const do_string_env = [](FlagString &flag, const char *name) {
    if (flag.is_set()) return;
    let const value = std::getenv(name);
    if (value != nullptr && value[0] != '\0') {
      flag.set(StringView{value});
    }
  };

  do_string_env(FLAG_LISTEN, "WR_LISTEN_ADDRESS");
  do_string_env(FLAG_DATABASE, "WR_DATABASE_URL");
  do_string_env(FLAG_DBBACKEND, "WR_DATABASE_BACKEND");
  do_string_env(FLAG_PUBLICURL, "WR_WEB_ROOT_URL");

  if (!FLAG_LISTEN.is_set() || !FLAG_DATABASE.is_set()) {
    String message{allocator};
    message.append("error: These options are required:\n");
    let const do_note = [&](const FlagString &flag, const char *line) {
      if (flag.is_set()) return;
      message.append("  ");
      message.append(line);
      message.append("\n");
    };
    do_note(FLAG_LISTEN, "--listen-address <url>");
    do_note(FLAG_DATABASE, "--database-url <path>");
    message.append("\nSee --help for info.");
    fail(message.view());
  }

  if (FLAG_DBBACKEND.is_set() && FLAG_DBBACKEND.value() != "sqlite") {
    fail("error: Only the sqlite database backend is implemented.");
  }

  config cfg;
  cfg.listen_url = String{allocator, FLAG_LISTEN.value()};
  cfg.database_path = String{allocator, FLAG_DATABASE.value()};
  /* The OAuth redirect is built from the public base, so it falls back to the
     bind url when it is not given. */
  cfg.public_base_url =
      String{allocator, FLAG_PUBLICURL.is_set() ? FLAG_PUBLICURL.value()
                                                : FLAG_LISTEN.value()};
  cfg.github_client_id = String{allocator, env_or("WR_GITHUB_CLIENT_ID", "")};
  cfg.github_client_secret =
      String{allocator, env_or("WR_GITHUB_CLIENT_SECRET", "")};
  cfg.telegram_bot_token =
      String{allocator, env_or("WR_TELEGRAM_BOT_TOKEN", "")};
  cfg.session_key = String{allocator, env_or("WR_SESSION_KEY", "")};
  cfg.is_dev_mode = FLAG_DEV.is_enabled();
  cfg.is_metrics_enabled = FLAG_METRICS.is_enabled();
  cfg.is_forwarded_trusted = FLAG_TRUSTFWD.is_enabled();

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
    message.append(
        "\nOr pass --enable-dangerous-developer-environment to run with the "
        "login bypass.");
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

  install_shutdown_handlers(server);
  LOG(Info, "shutdown handlers installed for SIGINT, SIGTERM, and SIGHUP");

  let run_result = server.run(); /* blocks until a shutdown signal */

  clear_shutdown_handlers();
  LOG(Info, "server loop stopped, shutting down");
  liveness.stop();

  if (run_result.is_error()) fail(run_result.error().message().view());

  return 0;
}
