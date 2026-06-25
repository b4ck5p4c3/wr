#include "Sqlite.hpp"

#include "Trace.hpp"
#include "sqlite3.h"

namespace wr {

Sqlite::~Sqlite()
{
  if (m_connection != nullptr) sqlite3_close(m_connection);
}

fn Sqlite::make_error(StringView context) const -> Error
{
  String message{m_allocator};
  message.append(context);
  message.append(", ");
  message.append(sqlite3_errmsg(m_connection));
  return Error{message.view()};
}

fn Sqlite::open(StringView connection_string) -> ErrorOr<Ok>
{
  let const path = String{m_allocator, connection_string};
  let const flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
  if (sqlite3_open_v2(path.c_str(), &m_connection, flags, nullptr) != SQLITE_OK)
    return make_error("Unable to open the database");

  sqlite3_busy_timeout(m_connection, 5000);

  /* The server and the liveness sweep open separate connections, so the
     write-ahead log lets a read run while the other connection writes. */
  TRY(execute("PRAGMA journal_mode=WAL;"));

  LOG(Info, "sqlite opened at %s", path.c_str());
  return Success;
}

fn Sqlite::execute(StringView sql) -> ErrorOr<Ok>
{
  let const statement = String{m_allocator, sql};
  if (sqlite3_exec(m_connection, statement.c_str(), nullptr, nullptr,
                   nullptr) != SQLITE_OK)
    return make_error("Unable to execute the statement");
  return Success;
}

fn Sqlite::prepare(StringView sql) -> ErrorOr<SqlStatement>
{
  let const statement = String{m_allocator, sql};
  sqlite3_stmt *handle = nullptr;
  if (sqlite3_prepare_v2(m_connection, statement.c_str(), -1, &handle,
                         nullptr) != SQLITE_OK)
    return make_error("Unable to prepare the statement");
  return SqlStatement{*this, handle, m_allocator};
}

fn Sqlite::finalize(opaque *handle) noexcept -> void
{
  sqlite3_finalize(static_cast<sqlite3_stmt *>(handle));
}

fn Sqlite::bind_text(opaque *handle, int index, StringView text) -> void
{
  sqlite3_bind_text(static_cast<sqlite3_stmt *>(handle), index, text.data,
                    static_cast<int>(text.count()), SQLITE_TRANSIENT);
}

fn Sqlite::bind_int(opaque *handle, int index, i64 value) -> void
{
  sqlite3_bind_int64(static_cast<sqlite3_stmt *>(handle), index, value);
}

fn Sqlite::step(opaque *handle) -> ErrorOr<bool>
{
  let const result = sqlite3_step(static_cast<sqlite3_stmt *>(handle));
  if (result == SQLITE_ROW) return true;
  if (result == SQLITE_DONE) return false;
  return make_error("Unable to step the statement");
}

fn Sqlite::column_text(opaque *handle, int column, Allocator allocator) const
    -> String
{
  let const statement = static_cast<sqlite3_stmt *>(handle);
  let const text = sqlite3_column_text(statement, column);
  if (text == nullptr) return String{allocator};

  let const length =
      static_cast<usize>(sqlite3_column_bytes(statement, column));
  return String{
      allocator, StringView{reinterpret_cast<const char *>(text), length}
  };
}

fn Sqlite::column_int(opaque *handle, int column) const -> i64
{
  return sqlite3_column_int64(static_cast<sqlite3_stmt *>(handle), column);
}

} // namespace wr
