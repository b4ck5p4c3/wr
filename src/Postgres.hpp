#pragma once

#include "Allocator.hpp"
#include "ArrayList.hpp"
#include "Common.hpp"
#include "ErrorOr.hpp"
#include "Errors.hpp"
#include "Sql.hpp"
#include "String.hpp"
#include "StringView.hpp"

struct pg_conn;

namespace wr {

class Postgres final : public SqlDatabase
{
public:
  explicit Postgres(Allocator allocator) : m_allocator(allocator) {}
  ~Postgres() override;

  fn open(StringView connection_string) -> ErrorOr<Ok> override;
  fn execute(StringView sql) -> ErrorOr<Ok> override;
  mustuse fn prepare(StringView sql) -> ErrorOr<SqlStatement> override;
  fn clear_statement_cache() noexcept -> void override;
  mustuse fn dialect() const noexcept -> sql_dialect override
  {
    return sql_dialect::postgresql;
  }

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
  struct pq_statement
  {
    String name;
    ArrayList<String> parameters;
    opaque *result;
    int row_position;
    int row_count;
    bool was_executed;
  };

  struct cached_statement
  {
    String sql;
    pq_statement *handle;
    u64 last_used_count;
  };

  static constexpr usize STATEMENT_CACHE_CAPACITY = 32;

  mustuse fn make_error(StringView context,
                        ErrorBase::Severity severity =
                            ErrorBase::Severity::Recoverable) const -> Error;
  mustuse fn make_result_error(opaque *result, StringView context) const
      -> Error;
  mustuse fn acquire_statement(StringView sql) -> ErrorOr<pq_statement *>;
  mustuse fn create_statement(StringView sql) -> ErrorOr<pq_statement *>;
  fn destroy_statement(pq_statement *statement) noexcept -> void;

  Allocator m_allocator;
  pg_conn *m_connection{nullptr};
  ArrayList<cached_statement> m_statement_cache{m_allocator};
  u64 m_use_count{0};
  u64 m_prepared_count{0};
};

} // namespace wr
