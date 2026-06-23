#include "App.hpp"
#include "Cli.hpp"
#include "Common.hpp"
#include "Curl.hpp"
#include "ErrorOr.hpp"
#include "Liveness.hpp"
#include "Mongoose.hpp"
#include "Store.hpp"
#include "Trace.hpp"

using namespace wr;

FLAG_LIST_DECL();
HELP_DESCRIPTION_DECL("wr, a webring server backend.");
HELP_SYNOPSIS_DECL("[options]");

FLAG(HELP, Bool, 'h', "help", "Show this help and exit.");
FLAG(VERSION, Bool, 'v', "version", "Show the version and exit.");
FLAG(LISTEN, String, 'l', "listen", Server,
     "Listen on URL (default http://0.0.0.0:8000).");
FLAG(DATABASE, String, 'd', "database", Server,
     "Path to the sqlite database (default wr.db).");
FLAG(WEBROOT, String, 'w', "web-root", Server,
     "Directory of the built frontend (default web/dist).");
FLAG(PUBLICURL, String, 'u', "public-url", Server,
     "Public base URL used for the OAuth redirect.");
FLAG(LOGFILE, String, 'L', "log-file", Debug,
     "Append the log to FILE instead of standard error.");

namespace {

fn env_or(const char *name, const char *fallback) -> const char *
{
  let const value = std::getenv(name);
  return value != nullptr ? value : fallback;
}

} // namespace

fn main(int argc, char **argv) -> int
{
  let parse_result = parse_flags(FLAG_LIST, argc, argv);
  if (parse_result.is_error()) {
    show_message(parse_result.error().message().view());
    return 1;
  }

  if (FLAG_HELP.is_enabled()) {
    print(make_synopsis("wr", HELP_SYNOPSIS));
    print("\n");
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

  if (FLAG_LOGFILE.is_set()) {
    let const log_path = String{FLAG_LOGFILE.value()};
    let log_result = set_log_file(log_path.c_str());
    if (log_result.is_error()) {
      show_message(log_result.error().message().view());
      return 1;
    }
  }

  let const allocator = heap_allocator();

  /* The server options carry no default, so a missing one fails early with the
     names of every option still required. */
  if (!FLAG_LISTEN.is_set() || !FLAG_DATABASE.is_set() ||
      !FLAG_WEBROOT.is_set() || !FLAG_PUBLICURL.is_set())
  {
    String missing{allocator};
    let const note = [&](const FlagString &flag, const char *name) {
      if (flag.is_set()) return;
      if (!missing.is_empty()) missing.append(", ");
      missing.append(name);
    };
    note(FLAG_LISTEN, "--listen <url>");
    note(FLAG_DATABASE, "--database <path>");
    note(FLAG_WEBROOT, "--web-root <dir>");
    note(FLAG_PUBLICURL, "--public-url <url>");

    String message{allocator};
    message.append("These options are required, ");
    message.append(missing.view());
    message.append(". See --help for info.");
    show_message(message.view());
    return 1;
  }

  config cfg;
  cfg.listen_url = String{allocator, FLAG_LISTEN.value()};
  cfg.database_path = String{allocator, FLAG_DATABASE.value()};
  cfg.web_root = String{allocator, FLAG_WEBROOT.value()};
  cfg.public_base_url = String{allocator, FLAG_PUBLICURL.value()};
  cfg.github_client_id = String{allocator, env_or("WR_GITHUB_CLIENT_ID", "")};
  cfg.github_client_secret =
      String{allocator, env_or("WR_GITHUB_CLIENT_SECRET", "")};
  cfg.telegram_bot_token =
      String{allocator, env_or("WR_TELEGRAM_BOT_TOKEN", "")};
  cfg.session_key = String{allocator, env_or("WR_SESSION_KEY", "")};

  LOG(Info, "wr is starting up");

  Store store{allocator};
  let store_result = store.open(cfg.database_path.view());
  if (store_result.is_error()) {
    show_message(store_result.error().message().view());
    return 1;
  }

  CurlClient client{allocator};
  App app{allocator, store, client, cfg};

  MongooseServer server{allocator};
  let listen_result = server.listen(cfg.listen_url.view(), App::on_event, &app);
  if (listen_result.is_error()) {
    show_message(listen_result.error().message().view());
    return 1;
  }

  /* The sweep gets its own client with a short timeout, used only on its own
     thread. */
  CurlClient::Options probe_options;
  probe_options.timeout_ms = 10000;
  CurlClient probe_client{allocator, probe_options};
  Liveness liveness{allocator, cfg, probe_client};
  if (liveness.start().is_error())
    LOG(Info, "the liveness sweep did not start, the server runs without it");

  let run_result = server.run();
  liveness.stop();
  if (run_result.is_error()) {
    show_message(run_result.error().message().view());
    return 1;
  }
  return 0;
}
