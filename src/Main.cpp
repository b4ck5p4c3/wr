#include "Cli.hpp"
#include "Common.hpp"
#include "ErrorOr.hpp"
#include "Trace.hpp"

using namespace wr;

FLAG_LIST_DECL();
HELP_DESCRIPTION_DECL("wr, a webring server backend.");
HELP_SYNOPSIS_DECL("[options]");

FLAG(Help, Bool, 'h', "help", "Show this help and exit.");
FLAG(Version, Bool, 'v', "version", "Show the version and exit.");
FLAG(LogFile, String, 'L', "log-file", Debug,
     "Append the log to FILE instead of standard error.");

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
    print("\n");
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

  LOG(Info, "wr is starting up");

  print("Web ring or something.\n");
  flush();

  return 0;
}
