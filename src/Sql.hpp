#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "ErrorOr.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace wr {

class SqlDatabase;

/* The handle is finalized on destruction, so a statement is move-only. */
class SqlStatement
{
public:
  SqlStatement(SqlDatabase &database, opaque *handle, Allocator allocator,
               bool is_cached = false)
      : m_database(&database), m_handle(handle), m_allocator(allocator),
        m_is_cached(is_cached)
  {}
  ~SqlStatement();

  SqlStatement(const SqlStatement &) = delete;
  SqlStatement &operator=(const SqlStatement &) = delete;
  SqlStatement(SqlStatement &&other) noexcept;
  SqlStatement &operator=(SqlStatement &&other) noexcept;

  fn bind(StringView text) -> void;
  fn bind(i64 value) -> void;
  fn bind(bool value) -> void;

  mustuse fn step() -> ErrorOr<bool>;

  /* The column is read in select order, so the position advances on every call
     and resets on the next step. */
  template <class T>
  mustuse fn get() -> T;

private:
  SqlDatabase *m_database;
  opaque *m_handle;
  Allocator m_allocator;
  bool m_is_cached = false;
  int m_next_bind_position = 1;
  int m_next_column_position = 0;
};

/* The abstract SQL database. A backend such as Sqlite owns the connection,
   opened from a connection string, and is the only persistence primitive. The
   connection string is the local path for sqlite and a connection link for a
   networked store. */
class SqlDatabase
{
public:
  virtual ~SqlDatabase() = default;

  SqlDatabase(const SqlDatabase &) = delete;
  SqlDatabase &operator=(const SqlDatabase &) = delete;

  virtual fn open(StringView connection_string) -> ErrorOr<Ok> = 0;
  virtual fn execute(StringView sql) -> ErrorOr<Ok> = 0;
  mustuse virtual fn prepare(StringView sql) -> ErrorOr<SqlStatement> = 0;
  virtual fn clear_statement_cache() noexcept -> void = 0;

protected:
  SqlDatabase() = default;

  friend class SqlStatement;

  virtual fn finalize(opaque *handle) noexcept -> void = 0;
  virtual fn reset_statement(opaque *handle) noexcept -> void = 0;
  virtual fn bind_text(opaque *handle, int index, StringView text) -> void = 0;
  virtual fn bind_int(opaque *handle, int index, i64 value) -> void = 0;
  mustuse virtual fn step(opaque *handle) -> ErrorOr<bool> = 0;
  mustuse virtual fn column_text(opaque *handle, int column,
                                 Allocator allocator) const -> String = 0;
  mustuse virtual fn column_int(opaque *handle, int column) const -> i64 = 0;
};

inline SqlStatement::~SqlStatement()
{
  if (m_handle == nullptr) return;

  if (m_is_cached)
    m_database->reset_statement(m_handle);
  else
    m_database->finalize(m_handle);
}

inline SqlStatement::SqlStatement(SqlStatement &&other) noexcept
    : m_database(other.m_database), m_handle(other.m_handle),
      m_allocator(other.m_allocator), m_is_cached(other.m_is_cached)
{
  other.m_handle = nullptr;
}

inline SqlStatement &SqlStatement::operator=(SqlStatement &&other) noexcept
{
  if (this != &other) {
    if (m_handle != nullptr) {
      if (m_is_cached)
        m_database->reset_statement(m_handle);
      else
        m_database->finalize(m_handle);
    }
    m_database = other.m_database;
    m_handle = other.m_handle;
    m_allocator = other.m_allocator;
    m_is_cached = other.m_is_cached;
    other.m_handle = nullptr;
  }
  return *this;
}

inline fn SqlStatement::bind(StringView text) -> void
{
  m_database->bind_text(m_handle, m_next_bind_position++, text);
}

inline fn SqlStatement::bind(i64 value) -> void
{
  m_database->bind_int(m_handle, m_next_bind_position++, value);
}

inline fn SqlStatement::bind(bool value) -> void
{
  m_database->bind_int(m_handle, m_next_bind_position++,
                       static_cast<i64>(value));
}

inline fn SqlStatement::step() -> ErrorOr<bool>
{
  m_next_column_position = 0;
  return m_database->step(m_handle);
}

template <>
inline fn SqlStatement::get<String>() -> String
{
  return m_database->column_text(m_handle, m_next_column_position++,
                                 m_allocator);
}

template <>
inline fn SqlStatement::get<i64>() -> i64
{
  return m_database->column_int(m_handle, m_next_column_position++);
}

} // namespace wr
