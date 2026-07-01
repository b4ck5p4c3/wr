#include "Server.hpp"

namespace wr {

/* Anchored here so the vtable is emitted in one translation unit. */
HttpServer::~HttpServer() = default;

fn HttpServerEvent::reply(u16 status, const HttpHeaders &headers,
                          StringView body, StringView static_headers)
    -> ErrorOr<Ok>
{
  return m_server->reply(m_connection, status, headers, body, static_headers);
}

} // namespace wr
