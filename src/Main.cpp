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

FLAG(Help, Bool, 'h', "help", "Show this help and exit.");
FLAG(Version, Bool, 'v', "version", "Show the version and exit.");
FLAG(Listen, String, 'l', "listen", Server,
     "Listen on URL (default http://0.0.0.0:8000).");
FLAG(Database, String, 'd', "database", Server,
     "Path to the sqlite database (default wr.db).");
FLAG(WebRoot, String, 'w', "web-root", Server,
     "Directory of the built frontend (default web/dist).");
FLAG(PublicUrl, String, 'u', "public-url", Server,
     "Public base URL used for the OAuth redirect.");
FLAG(LogFile, String, 'L', "log-file", Debug,
     "Append the log to FILE instead of standard error.");

namespace {

fn flag_or(const FlagString &flag, const char *fallback) -> StringView
{
  return flag.is_set() ? flag.value() : StringView{fallback};
}

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

  if (FLAG_Help.is_enabled()) {
    print(make_synopsis("wr", HELP_SYNOPSIS));
    print("\n");
    print(HELP_DESCRIPTION);
    print("\n\n");
    print(make_flag_help(FLAG_LIST));
    print("\n");
    flush();
    return 0;
  }

  if (FLAG_Version.is_enabled()) {
    show_version();
    return 0;
  }

  if (FLAG_LogFile.is_set()) {
    let const log_path = String{FLAG_LogFile.value()};
    let log_result = set_log_file(log_path.c_str());
    if (log_result.is_error()) {
      show_message(log_result.error().message().view());
      return 1;
    }
  }

  let const allocator = heap_allocator();

  config cfg;
  cfg.listen_url =
      String{allocator, flag_or(FLAG_Listen, "http://0.0.0.0:8000")};
  cfg.database_path = String{allocator, flag_or(FLAG_Database, "wr.db")};
  cfg.web_root = String{allocator, flag_or(FLAG_WebRoot, "web/dist")};
  cfg.public_base_url =
      String{allocator, flag_or(FLAG_PublicUrl, "http://localhost:8000")};
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

  Liveness liveness{allocator, cfg};
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
