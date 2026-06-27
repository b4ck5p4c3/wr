#pragma once

#include "Common.hpp"
#include "Containers.hpp"
#include "ErrorOr.hpp"

#define FLAG_LIST T__FLAG_LIST

#define HELP_SYNOPSIS T__FLAG_HELP_SYNOPSIS

#define HELP_SYNOPSIS_DECL(...)                                                \
  static wr::ArrayList<wr::StringView> HELP_SYNOPSIS { __VA_ARGS__ }

#define HELP_DESCRIPTION T__FLAG_HELP_DESCRIPTION

#define HELP_DESCRIPTION_DECL(text)                                            \
  static wr::StringView HELP_DESCRIPTION { text }

#define FLAG_LIST_DECL()                                                       \
  static wr::ArrayList<wr::Flag *> FLAG_LIST { wr::heap_allocator() }

#define T__FLAG_SELECT(_1, _2, _3, _4, _5, _6, name, ...) name
#define FLAG(...) T__FLAG_SELECT(__VA_ARGS__, T__FLAG6, T__FLAG5)(__VA_ARGS__)
#define T__FLAG5(var_name, kind, short_name, long_name, description)           \
  T__FLAG6(var_name, kind, short_name, long_name, NoSection, description)
#define T__FLAG6(var_name, kind, short_name, long_name, section, description)  \
  static wr::Flag##kind concat_literal(FLAG_, var_name){                       \
      short_name, long_name, wr::flag_section::section, description};          \
  static uchar concat_literal(t__flag_dummy_, __LINE__) =                      \
      (FLAG_LIST.push(&concat_literal(FLAG_, var_name)), 0)

namespace wr {

enum class flag_section : u8
{
  NoSection,
  Server,
  Debug,
};

extern const usize HELP_WRAP_WIDTH;
extern const usize HELP_INDENT;

class Flag
{
public:
  enum class Kind : u8
  {
    Bool,
    String,
    ManyStrings,
  };

  pure fn kind() const noexcept -> Kind;
  pure fn position() const noexcept -> usize;
  fn set_position(u32 n) -> void;
  pure fn short_name() const noexcept -> char;
  pure fn long_name() const noexcept -> StringView;
  pure fn section() const noexcept -> flag_section;
  pure fn description() const noexcept -> StringView;

  virtual fn reset() -> void = 0;

protected:
  Flag(Kind type, char short_name, StringView long_name, flag_section section,
       StringView description);
  Flag(Allocator allocator, Kind type, char short_name, StringView long_name,
       flag_section section, StringView description);

  Kind m_kind;
  usize m_position{0}; /* 0 if it wasn't specified. */
  char m_short_name;
  flag_section m_section;
  String m_long_name;
  String m_description;
};

class FlagBool : public Flag
{
public:
  FlagBool(char short_name, StringView long_name, flag_section section,
           StringView description);
  FlagBool(Allocator allocator, char short_name, StringView long_name,
           flag_section section, StringView description);

  fn enable() -> void;
  pure fn is_enabled() const noexcept -> bool;
  pure fn count() const noexcept -> usize;

  fn reset() -> void override;

private:
  usize m_count{0};
};

class FlagString : public Flag
{
public:
  FlagString(char short_name, StringView long_name, flag_section section,
             StringView description);
  FlagString(Allocator allocator, char short_name, StringView long_name,
             flag_section section, StringView description);

  fn set(StringView v) -> void;
  pure fn is_set() const noexcept -> bool;
  pure fn value() const noexcept -> StringView;

  fn reset() -> void override;

private:
  bool m_is_set{false};
  String m_value{};
};

class FlagManyStrings : public Flag
{
public:
  FlagManyStrings(char short_name, StringView long_name, flag_section section,
                  StringView description);
  FlagManyStrings(Allocator allocator, char short_name, StringView long_name,
                  flag_section section, StringView description);

  fn append(StringView v) -> void;
  pure fn count() const noexcept -> usize;
  pure fn is_empty() const noexcept -> bool;

  pure fn get(usize i) const noexcept -> StringView;

  fn next() -> StringView;
  pure fn at_end() const noexcept -> bool;

  fn reset() -> void override;

private:
  ArrayList<String> m_values{};
  usize m_value_position{0};
};

fn parse_flags_vec(const ArrayList<Flag *> &flags,
                   const ArrayList<String> &args, usize base_position = 0,
                   const Flag *operand_value_flag = nullptr)
    -> ErrorOr<ArrayList<String>>;
fn parse_flags(const ArrayList<Flag *> &flags, int argc,
               const char *const *argv, usize base_position = 0,
               const Flag *operand_value_flag = nullptr)
    -> ErrorOr<ArrayList<String>>;

fn join_command_line(int argc, const char *const *argv) -> String;

fn reset_flags(const ArrayList<Flag *> &flags) -> void;

fn show_version() -> void;

fn make_synopsis(StringView program_name, const ArrayList<StringView> &lines)
    -> String;
fn make_flag_help(const ArrayList<Flag *> &flags) -> String;

fn show_message(StringView err) -> void;

fn print(StringView text) -> void;
fn print_error(StringView text) -> void;
fn flush() -> void;

} // namespace wr
