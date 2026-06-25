#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "ErrorOr.hpp"
#include "Errors.hpp"
#include "Sql.hpp"
#include "String.hpp"
#include "StringView.hpp"

struct sqlite3;

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
  fn bind_text(opaque *handle, int index, StringView text) -> void override;
  fn bind_int(opaque *handle, int index, i64 value) -> void override;
  mustuse fn step(opaque *handle) -> ErrorOr<bool> override;
  mustuse fn column_text(opaque *handle, int column, Allocator allocator) const
      -> String override;
  mustuse fn column_int(opaque *handle, int column) const -> i64 override;

private:
  mustuse fn make_error(StringView context) const -> Error;

  Allocator m_allocator;
  sqlite3 *m_connection{nullptr};
};

} // namespace wr
