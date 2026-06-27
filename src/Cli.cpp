#include "Cli.hpp"

#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Trace.hpp"

namespace wr {

constexpr usize HELP_WRAP_WIDTH = 80;
constexpr usize HELP_INDENT = 2;

Flag::Flag(Flag::Kind kind, char short_name, StringView long_name,
           flag_section section, StringView description)
    : m_kind(kind), m_short_name(short_name), m_section(section),
      m_long_name(long_name), m_description(description)
{}

Flag::Flag(Allocator allocator, Flag::Kind kind, char short_name,
           StringView long_name, flag_section section, StringView description)
    : m_kind(kind), m_short_name(short_name), m_section(section),
      m_long_name(allocator, long_name), m_description(allocator, description)
{}

pure fn Flag::kind() const noexcept -> Flag::Kind { return m_kind; }

fn Flag::set_position(u32 n) -> void { m_position = n; }

pure fn Flag::position() const noexcept -> usize { return m_position; }

pure fn Flag::short_name() const noexcept -> char { return m_short_name; }

pure fn Flag::long_name() const noexcept -> StringView { return m_long_name; }

pure fn Flag::section() const noexcept -> flag_section { return m_section; }

pure fn Flag::description() const noexcept -> StringView
{
  return m_description;
}

FlagBool::FlagBool(char short_name, StringView long_name, flag_section section,
                   StringView description)
    : Flag(Flag::Kind::Bool, short_name, long_name, section, description)
{}

FlagBool::FlagBool(Allocator allocator, char short_name, StringView long_name,
                   flag_section section, StringView description)
    : Flag(allocator, Flag::Kind::Bool, short_name, long_name, section,
           description)
{}

fn FlagBool::enable() -> void { m_count++; }

pure fn FlagBool::is_enabled() const noexcept -> bool { return m_count > 0; }

pure fn FlagBool::count() const noexcept -> usize { return m_count; }

fn FlagBool::reset() -> void
{
  m_position = 0;
  m_count = 0;
}

FlagString::FlagString(char short_name, StringView long_name,
                       flag_section section, StringView description)
    : Flag(Flag::Kind::String, short_name, long_name, section, description)
{}

FlagString::FlagString(Allocator allocator, char short_name,
                       StringView long_name, flag_section section,
                       StringView description)
    : Flag(allocator, Flag::Kind::String, short_name, long_name, section,
           description),
      m_value(allocator)
{}

fn FlagString::set(StringView v) -> void
{
  m_value = v;
  m_is_set = true;
}

pure fn FlagString::is_set() const noexcept -> bool { return m_is_set; }

pure fn FlagString::value() const noexcept -> StringView
{
  return m_value.view();
}

fn FlagString::reset() -> void
{
  m_position = 0;
  m_value.clear();
  m_is_set = false;
}

FlagManyStrings::FlagManyStrings(char short_name, StringView long_name,
                                 flag_section section, StringView description)
    : Flag(Flag::Kind::ManyStrings, short_name, long_name, section, description)
{}

FlagManyStrings::FlagManyStrings(Allocator allocator, char short_name,
                                 StringView long_name, flag_section section,
                                 StringView description)
    : Flag(allocator, Flag::Kind::ManyStrings, short_name, long_name, section,
           description),
      m_values(allocator)
{}

fn FlagManyStrings::append(StringView v) -> void { m_values.push_managed(v); }

pure fn FlagManyStrings::is_empty() const noexcept -> bool
{
  return m_values.is_empty();
}

pure fn FlagManyStrings::count() const noexcept -> usize
{
  return m_values.count();
}

pure fn FlagManyStrings::get(usize i) const noexcept -> StringView
{
  ASSERT(i < m_values.count());
  return m_values[i].view();
}

fn FlagManyStrings::next() -> StringView
{
  ASSERT(m_value_position < m_values.count());
  const String &value = m_values[m_value_position++];
  return value.view();
}

pure fn FlagManyStrings::at_end() const noexcept -> bool
{
  return m_value_position == count();
}

fn FlagManyStrings::reset() -> void
{
  m_position = 0;
  m_values.clear();
  m_value_position = 0;
}

static fn find_flag(const ArrayList<Flag *> &flags, const char *flag_start,
                    bool is_long, Flag **result_flag, const char **value_start)
    -> bool
{
  usize longest_length = 0;

  *value_start = nullptr;
  *result_flag = nullptr;

  for (usize i = 0; i < flags.count(); ++i) {
    if (!is_long) {
      if (flags[i]->short_name() != '\0' &&
          flags[i]->short_name() == *flag_start)
      {
        *result_flag = flags[i];
        *value_start = flag_start + 1;
        return true;
      }
    } else {
      if (!flags[i]->long_name().is_empty()) {
        let const flag_length = flags[i]->long_name().length;

        if (flag_length > longest_length &&
            std::strncmp(flags[i]->long_name().data, flag_start, flag_length) ==
                0 &&
            (flag_start[flag_length] == '\0' || flag_start[flag_length] == '='))
        {
          *result_flag = flags[i];
          *value_start = flag_start + flag_length;
          longest_length = flag_length;
        }
      }
    }
  }

  return longest_length > 0;
}

fn parse_flags_vec(const ArrayList<Flag *> &flags,
                   const ArrayList<String> &args, usize base_position,
                   const Flag *operand_value_flag) -> ErrorOr<ArrayList<String>>
{
  let os_argv = ArrayList<const char *>{};
  os_argv.reserve(args.count());

  for (let const &arg : args)
    os_argv.push(arg.c_str());

  return parse_flags(flags, static_cast<int>(os_argv.count()), os_argv.begin(),
                     base_position, operand_value_flag);
}

static fn flag_name(const Flag *f, bool is_long) -> String
{
  let name = String{};
  name += "-";
  if (is_long) {
    name += "-";
    name += f->long_name();
  } else {
    name.push(f->short_name());
  }
  return name;
}

fn parse_flags(const ArrayList<Flag *> &flags, int argc,
               const char *const *argv, usize base_position,
               const Flag *operand_value_flag) -> ErrorOr<ArrayList<String>>
{
  ASSERT(argc >= 0);
  unused(base_position);

  if (argc == 0) return ArrayList<String>{};

  ASSERT(argv != nullptr);

  LOG(Debug, "parsing %d command line arguments", argc);

  u32 position = 0;
  let args = ArrayList<String>{};

  Flag *prev_flag{};
  bool next_arg_is_value = false;
  bool prev_is_long = false;
  bool ignore_rest = false;

  for (int i = 0; i < argc; i++) {
    ASSERT(argv[i] != nullptr);

    if (next_arg_is_value) {
      bool next_is_known_bool_flag = false;
      if (prev_flag == operand_value_flag && operand_value_flag != nullptr &&
          argv[i][0] == '-' && argv[i][1] != '\0')
      {
        let const is_long_token = argv[i][1] == '-';
        let const token_offset = is_long_token ? &argv[i][2] : &argv[i][1];
        if (*token_offset != '\0') {
          Flag *probe_flag = nullptr;
          const char *probe_value = nullptr;
          if (find_flag(flags, token_offset, is_long_token, &probe_flag,
                        &probe_value) &&
              probe_flag->kind() == Flag::Kind::Bool)
          {
            next_is_known_bool_flag = true;
          }
        }
      }

      if (!next_is_known_bool_flag) {
        next_arg_is_value = false;

        ASSERT(prev_flag != nullptr);
        LOG(All,
            "attaching the next argument '%s' as the value of the flag '%s'",
            argv[i], flag_name(prev_flag, prev_is_long).c_str());
        if (prev_flag->kind() == Flag::Kind::String)
          static_cast<FlagString *>(prev_flag)->set(argv[i]);
        else
          static_cast<FlagManyStrings *>(prev_flag)->append(argv[i]);

        continue;
      }
    }

    if (ignore_rest || argv[i][0] != '-' || i == 0) {
      const bool is_program_name = args.is_empty();
      LOG(Debug, "taking '%s' as an operand", argv[i]);
      args.push_managed(StringView{argv[i]});
      if (!is_program_name) ignore_rest = true;
      continue;
    }

    bool is_long = false;
    const char *flag_offset{};

    if (argv[i][1] != '-') {
      flag_offset = &argv[i][1];
    } else {
      flag_offset = &argv[i][2];
      is_long = true;
    }

    if (*flag_offset == '\0') {
      if (is_long) {
        LOG(Debug, "stopping option parsing at '--'");
        ignore_rest = true;
      } else {
        args.push_managed(StringView{argv[i]});
      }

      continue;
    }

    bool repeat = true;

    Flag *flag{};
    const char *value_offset{};

    while (repeat) {
      repeat = false;

      let const found =
          find_flag(flags, flag_offset, is_long, &flag, &value_offset);

      if (found) {
        switch (flag->kind()) {
        case Flag::Kind::Bool: {
          let const bool_flag = static_cast<FlagBool *>(flag);

          bool_flag->enable();
          bool_flag->set_position(++position);
          LOG(All, "enabled the flag '%s'",
              flag_name(bool_flag, is_long).c_str());

          if (!is_long && *value_offset != '\0') {
            ++flag_offset;
            repeat = true;
            continue;
          }
        } break;

        case Flag::Kind::String:
        case Flag::Kind::ManyStrings: {
          if (*value_offset == '\0') {
            LOG(All, "the flag '%s' expects the next argument as its value",
                flag_name(flag, is_long).c_str());
            next_arg_is_value = true;
            prev_flag = flag;
            prev_is_long = is_long;
          } else {
            if (*value_offset == '=') {
              value_offset++;

              if (*value_offset != '\0') {
                if (flag->kind() == Flag::Kind::String)
                  static_cast<FlagString *>(flag)->set(value_offset);
                else
                  static_cast<FlagManyStrings *>(flag)->append(value_offset);

                flag->set_position(++position);
                LOG(All, "set the flag '%s' to '%s'",
                    flag_name(flag, is_long).c_str(), value_offset);
              } else {
                return Error{"No value provided for '" +
                             flag_name(flag, is_long) + "' flag"};
              }
            } else if (!is_long) {
              if (flag->kind() == Flag::Kind::String)
                static_cast<FlagString *>(flag)->set(value_offset);
              else
                static_cast<FlagManyStrings *>(flag)->append(value_offset);

              flag->set_position(++position);
              LOG(All, "set the flag '%s' to the attached value '%s'",
                  flag_name(flag, is_long).c_str(), value_offset);
            } else {
              return Error{"Long flags require a separator "
                           "between the flag and the "
                           "value. Try using '" +
                           flag_name(flag, is_long) + "=" + value_offset + "'"};
            }
          }
        } break;
        }
      }

      if (!found) {
        if (*flag_offset == '-') {
          return Error{"Missing space between '-' and other options"};
        } else {
          let error_message = String{};
          error_message += "Unknown flag '-";

          if (!is_long) {
            error_message.push(*flag_offset);
          } else {
            error_message += "-";

            const StringView flag_sv = flag_offset;
            let const equals_position = flag_sv.find_character('=');

            if (equals_position)
              error_message += flag_sv.substring_of_length(0, *equals_position);
            else
              error_message += flag_sv;
          }
          error_message += "'";

          LOG(Debug, "rejecting the unknown flag in '%s'", argv[i]);

          return Error{error_message};
        }
      }
    }
  }

  if (next_arg_is_value) {
    ASSERT(prev_flag != nullptr);
    return Error{"No value provided for '" +
                 flag_name(prev_flag, prev_is_long) + "' flag"};
  }

  return args;
}

fn join_command_line(int argc, const char *const *argv) -> String
{
  let s = String{};
  for (int i = 0; i < argc; i++) {
    if (i > 0) s.push(' ');
    s.append(StringView{argv[i], std::strlen(argv[i])});
  }
  return s;
}

fn reset_flags(const ArrayList<Flag *> &flags) -> void
{
  for (let const flag : flags) {
    flag->reset();
  }
}

cold fn show_version() -> void
{
  let s = String{};
  s += "wr ";
  s += WR_VERSION_STRING;
  s += '\n';
  s += "Built on ";
  s += WR_BUILD_DATE;
  s += '\n';
  s += '\n';
  s += "MODE=";
  s += WR_BUILD_MODE;
  s += '\n';
  s += "HEAD=";
  s += WR_COMMIT_HASH;
  s += '\n';
  s += "CXX=";
  s += WR_COMPILER;
  s += '\n';
  s += "ENVCXXFLAGS=";
  s += (*WR_ENVCXXFLAGS == '\0' ? "<none>" : WR_ENVCXXFLAGS);
  s += '\n';
  s += "OS=";
  s += WR_OS_INFO;
  s += '\n';
  s += '\n';
  s += WR_SHORT_LICENSE;
  s += '\n';

  print(s);
  flush();
}

cold fn make_synopsis(StringView program_name,
                      const ArrayList<StringView> &lines) -> String
{
  let s = String{};

  s += "SYNOPSIS\n";

  for (let const line : lines) {
    s += "  ";
    s += program_name;
    s += ' ';
    s += line;
    s += "\n";
  }

  return s;
}

cold fn make_flag_help(const ArrayList<Flag *> &flags) -> String
{
  let s = String{};

  static constexpr usize DESCRIPTION_COLUMN = 26;
  static constexpr usize TEXT_WIDTH = HELP_WRAP_WIDTH - DESCRIPTION_COLUMN;

  let const do_render_flag = [&](const wr::Flag *f) {
    s += "\n";

    let left = String{};
    if (f->short_name() != '\0') {
      left += "  -";
      left += f->short_name();
      if (!f->long_name().is_empty()) left += ", ";
    } else if (!f->long_name().is_empty()) {
      left += "      ";
    } else {
      left += "  ";
    }

    if (!f->long_name().is_empty()) {
      left += "--";
      left += f->long_name();
      switch (f->kind()) {
      case wr::Flag::Kind::String: left += "=<...>"; break;
      case wr::Flag::Kind::ManyStrings: left += "=<.., ..>"; break;
      case wr::Flag::Kind::Bool: break;
      }
    }

    s += left;

    if (left.length() + 2 > DESCRIPTION_COLUMN) {
      s += '\n';
      for (usize i = 0; i < DESCRIPTION_COLUMN; i++)
        s += ' ';
    } else {
      for (usize i = left.length(); i < DESCRIPTION_COLUMN; i++)
        s += ' ';
    }

    let const description = f->description();
    usize line_used = 0;
    usize word_start = 0;
    for (usize i = 0; i <= description.length; i++) {
      const bool at_end = i == description.length;
      if (!at_end && description[i] != ' ') {
        continue;
      }

      const usize word_length = i - word_start;
      if (word_length > 0) {
        if (line_used > 0 && line_used + 1 + word_length > TEXT_WIDTH) {
          s += '\n';
          for (usize j = 0; j < DESCRIPTION_COLUMN; j++)
            s += ' ';
          line_used = 0;
        }
        if (line_used > 0) {
          s += ' ';
          line_used++;
        }
        s += description.substring_of_length(word_start, word_length);
        line_used += word_length;
      }
      word_start = i + 1;
    }
  };

  static const StringView SECTION_HEADERS[] = {"OPTIONS", "SERVER OPTIONS",
                                               "DEBUG OPTIONS"};
  for (u8 section = 0; section < 3; section++) {
    bool header_printed = false;
    for (let const flag : flags) {
      if (static_cast<u8>(flag->section()) != section) continue;
      if (!header_printed) {
        if (!s.is_empty()) s += "\n\n";
        s += SECTION_HEADERS[section];
        header_printed = true;
      }
      do_render_flag(flag);
    }
  }

  return s;
}

fn print(StringView text) -> void
{
  std::fwrite(text.data, 1, text.count(), stdout);
  std::fflush(stdout);
}

fn print_error(StringView text) -> void
{
  std::fwrite(text.data, 1, text.count(), stderr);
  std::fflush(stderr);
}

fn flush() -> void { std::fflush(stdout); }

cold fn show_message(StringView err) -> void
{
  print_error("wr: ");
  print_error(err);
  print_error("\n");
}

} // namespace wr
