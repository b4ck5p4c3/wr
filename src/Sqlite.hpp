#pragma once

#include "Allocator.hpp"
#include "ArrayList.hpp"
#include "Common.hpp"
#include "ErrorOr.hpp"
#include "Errors.hpp"
#include "Sql.hpp"
#include "String.hpp"
#include "StringView.hpp"

struct sqlite3;
struct sqlite3_stmt;

namespace wr {

/* The connection string is a local file path for the sqlite backend. */
class Sqlite final : public SqlDatabase
{
public:
  explicit Sqlite(Allocator allocator) : m_allocator(allocator) {}
  ~Sqlite() override;

  fn open(StringView connection_string) -> ErrorOr<Ok> override;
  fn execute(StringView sql) -> ErrorOr<Ok> override;
  mustuse fn prepare(StringView sql) -> ErrorOr<SqlStatement> override;

protected:
  fn finalize(opaque *handle) noexcept -> void override;
  fn reset_statement(opaque *handle) noexcept -> void override;
  fn bind_text(opaque *handle, int index, StringView text) -> void override;
  fn bind_int(opaque *handle, int index, i64 value) -> void override;
  mustuse fn step(opaque *handle) -> ErrorOr<bool> override;
  mustuse fn column_text(opaque *handle, int column, Allocator allocator) const
      -> String override;
  mustuse fn column_int(opaque *handle, int column) const -> i64 override;

private:
  /* A prepared statement is compiled once and kept here, keyed by its sql, so a
     repeated query is reset instead of recompiled. The least recently used
     entry is finalized once the cache is full. */
  struct cached_statement
  {
    String sql;
    sqlite3_stmt *handle;
    u64 last_used_count;
  };

  static constexpr usize STATEMENT_CACHE_CAPACITY = 32;

  mustuse fn make_error(StringView context) const -> Error;
  mustuse fn acquire_statement(StringView sql) -> ErrorOr<sqlite3_stmt *>;

  Allocator m_allocator;
  sqlite3 *m_connection{nullptr};
  ArrayList<cached_statement> m_statement_cache{m_allocator};
  u64 m_use_count{0};
};

} // namespace wr
