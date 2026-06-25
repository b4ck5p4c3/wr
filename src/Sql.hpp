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
  SqlStatement(SqlDatabase &database, opaque *handle, Allocator allocator)
      : m_database(&database), m_handle(handle), m_allocator(allocator)
  {}
  ~SqlStatement();

  SqlStatement(const SqlStatement &) = delete;
  SqlStatement &operator=(const SqlStatement &) = delete;
  SqlStatement(SqlStatement &&other) noexcept;
  SqlStatement &operator=(SqlStatement &&other) noexcept;

  fn bind(int index, StringView text) -> void;
  fn bind(int index, i64 value) -> void;

  mustuse fn step() -> ErrorOr<bool>;

  mustuse fn text(int column) const -> String;
  mustuse fn integer(int column) const -> i64;

private:
  SqlDatabase *m_database;
  opaque *m_handle;
  Allocator m_allocator;
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

protected:
  SqlDatabase() = default;

  friend class SqlStatement;

  virtual fn finalize(opaque *handle) noexcept -> void = 0;
  virtual fn bind_text(opaque *handle, int index, StringView text) -> void = 0;
  virtual fn bind_int(opaque *handle, int index, i64 value) -> void = 0;
  mustuse virtual fn step(opaque *handle) -> ErrorOr<bool> = 0;
  mustuse virtual fn column_text(opaque *handle, int column,
                                 Allocator allocator) const -> String = 0;
  mustuse virtual fn column_int(opaque *handle, int column) const -> i64 = 0;
};

inline SqlStatement::~SqlStatement()
{
  if (m_handle != nullptr) m_database->finalize(m_handle);
}

inline SqlStatement::SqlStatement(SqlStatement &&other) noexcept
    : m_database(other.m_database), m_handle(other.m_handle),
      m_allocator(other.m_allocator)
{
  other.m_handle = nullptr;
}

inline SqlStatement &SqlStatement::operator=(SqlStatement &&other) noexcept
{
  if (this != &other) {
    if (m_handle != nullptr) m_database->finalize(m_handle);
    m_database = other.m_database;
    m_handle = other.m_handle;
    m_allocator = other.m_allocator;
    other.m_handle = nullptr;
  }
  return *this;
}

inline fn SqlStatement::bind(int index, StringView text) -> void
{
  m_database->bind_text(m_handle, index, text);
}

inline fn SqlStatement::bind(int index, i64 value) -> void
{
  m_database->bind_int(m_handle, index, value);
}

inline fn SqlStatement::step() -> ErrorOr<bool>
{
  return m_database->step(m_handle);
}

inline fn SqlStatement::text(int column) const -> String
{
  return m_database->column_text(m_handle, column, m_allocator);
}

inline fn SqlStatement::integer(int column) const -> i64
{
  return m_database->column_int(m_handle, column);
}

} // namespace wr
