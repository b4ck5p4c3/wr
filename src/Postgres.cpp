#include "Postgres.hpp"

#include "Trace.hpp"

#include <libpq-fe.h>
#include <stdlib.h>

namespace wr {

Postgres::~Postgres()
{
  m_statement_cache.clear(
      [this](pq_statement *handle) noexcept { destroy_statement(handle); });

  if (m_connection != nullptr) PQfinish(m_connection);
}

fn Postgres::make_error(StringView context, ErrorBase::Severity severity) const
    -> Error
{
  String message{m_allocator};
  message.append(context);
  message.append(", ");
  message.append(PQerrorMessage(m_connection));

  Error error{message.view()};
  if (severity == ErrorBase::Severity::Critical) error.as_critical();
  return error;
}

fn Postgres::make_result_error(opaque *result, StringView context) const
    -> Error
{
  String message{m_allocator};
  message.append(context);
  message.append(", ");
  message.append(PQresultErrorMessage(static_cast<PGresult *>(result)));
  return Error{message.view()};
}

fn Postgres::open(StringView connection_string) -> ErrorOr<Ok>
{
  let const conninfo = String{m_allocator, connection_string};
  m_connection = PQconnectdb(conninfo.c_str());
  if (PQstatus(m_connection) != CONNECTION_OK)
    return make_error("Unable to open the database",
                      ErrorBase::Severity::Critical);

  LOG(Info, "postgresql connected");
  return Success;
}

fn Postgres::execute(StringView sql) -> ErrorOr<Ok>
{
  let const statement = String{m_allocator, sql};
  let const result = PQexec(m_connection, statement.c_str());
  let const status = PQresultStatus(result);
  if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
    let error = make_result_error(result, "Unable to execute the statement");
    PQclear(result);
    return error;
  }

  PQclear(result);
  return Success;
}

fn Postgres::prepare(StringView sql) -> ErrorOr<SqlStatement>
{
  let const handle = TRY(m_statement_cache.acquire(
      sql,
      [this](StringView statement_sql) -> ErrorOr<pq_statement *> {
        return create_statement(statement_sql);
      },
      [this](pq_statement *handle) noexcept { destroy_statement(handle); }));

  return SqlStatement{*this, handle, m_allocator, true};
}

fn Postgres::clear_statement_cache() noexcept -> void
{
  m_statement_cache.clear(
      [this](pq_statement *handle) noexcept { destroy_statement(handle); });
  LOG(Debug, "postgresql statement cache cleared");
}

namespace {

fn translate_placeholders(Allocator allocator, StringView sql) -> String
{
  String translated{allocator};
  bool is_inside_literal = false;
  u64 placeholder_count = 0;
  translated.reserve(sql.count());

  for (usize i = 0; i < sql.count(); i++) {
    let const character = sql[i];

    if (character == '\'') is_inside_literal = !is_inside_literal;

    if (character == '?' && !is_inside_literal) {
      translated.push('$');
      translated.append_number(++placeholder_count);
    } else {
      translated.push(character);
    }
  }

  return translated;
}

} // namespace

fn Postgres::create_statement(StringView sql) -> ErrorOr<pq_statement *>
{
  String name{m_allocator};
  name.append("wr_stmt_");
  name.append_number(m_prepared_count++);

  let const query = translate_placeholders(m_allocator, sql);
  let const prepared =
      PQprepare(m_connection, name.c_str(), query.c_str(), 0, nullptr);
  if (PQresultStatus(prepared) != PGRES_COMMAND_OK) {
    let error = make_result_error(prepared, "Unable to prepare the statement");
    PQclear(prepared);
    return error;
  }
  PQclear(prepared);

  let const memory = m_allocator.alloc_array<pq_statement>(1);
  return new (memory) pq_statement{
      steal(name), ArrayList<String>{m_allocator}, nullptr, 0, 0, false};
}

fn Postgres::destroy_statement(pq_statement *statement) noexcept -> void
{
  if (statement == nullptr) return;

  if (statement->result != nullptr)
    PQclear(static_cast<PGresult *>(statement->result));

  if (m_connection != nullptr) {
    String command{m_allocator};
    command.append("DEALLOCATE ");
    command.append(statement->name.view());
    PQclear(PQexec(m_connection, command.c_str()));
  }

  statement->~pq_statement();
  m_allocator.free_array(statement, 1);
}

fn Postgres::finalize(opaque *handle) noexcept -> void
{
  destroy_statement(static_cast<pq_statement *>(handle));
}

fn Postgres::reset_statement(opaque *handle) noexcept -> void
{
  let const statement = static_cast<pq_statement *>(handle);

  if (statement->result != nullptr) {
    PQclear(static_cast<PGresult *>(statement->result));
    statement->result = nullptr;
  }

  statement->parameters.clear();
  statement->row_position = 0;
  statement->row_count = 0;
  statement->was_executed = false;
}

fn Postgres::bind_text(opaque *handle, int index, StringView text) -> void
{
  unused(index);
  let const statement = static_cast<pq_statement *>(handle);
  statement->parameters.push_managed(text);
}

fn Postgres::bind_int(opaque *handle, int index, i64 value) -> void
{
  unused(index);
  let const statement = static_cast<pq_statement *>(handle);

  String parameter{m_allocator};
  if (value < 0) parameter.push('-');
  let const magnitude =
      value < 0 ? 0 - static_cast<u64>(value) : static_cast<u64>(value);
  parameter.append_number(magnitude);

  statement->parameters.push(steal(parameter));
}

fn Postgres::step(opaque *handle) -> ErrorOr<bool>
{
  let const statement = static_cast<pq_statement *>(handle);

  if (statement->was_executed) {
    statement->row_position++;
    return statement->row_position < statement->row_count;
  }

  let const parameter_count = static_cast<int>(statement->parameters.count());
  const char **values = nullptr;

  if (parameter_count > 0) {
    values =
        m_allocator.alloc_array<const char *>(statement->parameters.count());
    for (usize i = 0; i < statement->parameters.count(); i++)
      values[i] = statement->parameters[i].c_str();
  }

  let const result =
      PQexecPrepared(m_connection, statement->name.c_str(), parameter_count,
                     values, nullptr, nullptr, 0);
  if (values != nullptr)
    m_allocator.free_array(values, statement->parameters.count());

  let const status = PQresultStatus(result);
  if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
    let error = make_result_error(result, "Unable to step the statement");
    PQclear(result);
    return error;
  }

  statement->result = result;
  statement->row_count = PQntuples(result);
  statement->row_position = 0;
  statement->was_executed = true;
  return statement->row_position < statement->row_count;
}

fn Postgres::column_text(opaque *handle, int column, Allocator allocator) const
    -> String
{
  let const statement = static_cast<pq_statement *>(handle);
  let const result = static_cast<PGresult *>(statement->result);
  if (PQgetisnull(result, statement->row_position, column))
    return String{allocator};

  let const length =
      static_cast<usize>(PQgetlength(result, statement->row_position, column));
  return String{
      allocator,
      StringView{PQgetvalue(result, statement->row_position, column), length}
  };
}

fn Postgres::column_int(opaque *handle, int column) const -> i64
{
  let const statement = static_cast<pq_statement *>(handle);
  let const result = static_cast<PGresult *>(statement->result);
  if (PQgetisnull(result, statement->row_position, column)) return 0;

  return strtoll(PQgetvalue(result, statement->row_position, column), nullptr,
                 10);
}

} // namespace wr
