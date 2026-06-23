#pragma once

#include "Common.hpp"
#include "Debug.hpp"
#include "ErrorOr.hpp"
#include "Http.hpp"
#include "StringView.hpp"

namespace wr {

class HttpServer;

/* One inbound event for the handler. A Request event carries the method, the
   uri, the body, and the request headers as non-owning views valid only during
   the handler call, and a reply is written within that call, since the
   connection handle is borrowed and is freed once the dispatch returns. */
class HttpServerEvent
{
public:
  enum class Kind : u8
  {
    Open,
    Request,
    Close,
    Error,
  };

  HttpServerEvent(Kind kind, HttpServer &server, opaque *connection)
      : m_kind(kind), m_server(&server), m_connection(connection)
  {}

  mustuse pure fn kind() const noexcept -> Kind { return m_kind; }
  mustuse pure fn method() const noexcept -> StringView { return m_method; }
  mustuse pure fn uri() const noexcept -> StringView { return m_uri; }
  mustuse pure fn query() const noexcept -> StringView { return m_query; }
  mustuse pure fn body() const noexcept -> StringView { return m_body; }
  mustuse pure fn error_message() const noexcept -> StringView
  {
    return m_error_message;
  }
  mustuse pure fn request_headers() const noexcept -> const HttpHeaders &
  {
    ASSERT(m_request_headers != nullptr);
    return *m_request_headers;
  }

  /* Filled by a backend before a Request event is dispatched. */
  fn set_request(StringView method, StringView uri, StringView query,
                 StringView body, const HttpHeaders &headers) noexcept -> void
  {
    m_method = method;
    m_uri = uri;
    m_query = query;
    m_body = body;
    m_request_headers = &headers;
  }
  fn set_error_message(StringView message) noexcept -> void
  {
    m_error_message = message;
  }

  mustuse fn reply(u16 status, const HttpHeaders &headers, StringView body)
      -> ErrorOr<Ok>;

private:
  Kind m_kind;
  HttpServer *m_server;
  opaque *m_connection;
  StringView m_method;
  StringView m_uri;
  StringView m_query;
  StringView m_body;
  StringView m_error_message;
  const HttpHeaders *m_request_headers{nullptr};
};

using HttpServerHandler = void (*)(HttpServerEvent &event, opaque *user);

/* The abstract HTTP server. A backend such as MongooseServer binds a listener
   and pumps the loop. */
class HttpServer
{
public:
  virtual ~HttpServer();

  HttpServer(const HttpServer &) = delete;
  HttpServer &operator=(const HttpServer &) = delete;

  mustuse virtual fn listen(StringView url, HttpServerHandler handler,
                            opaque *user) -> ErrorOr<Ok> = 0;
  mustuse virtual fn poll(u32 timeout_ms) -> ErrorOr<Ok> = 0;

  /* Pumps the loop until a poll fails, for the process lifetime. */
  mustuse fn run(u32 poll_interval_ms = 1000) -> ErrorOr<Ok>
  {
    loop { TRY(poll(poll_interval_ms)); }
  }

  /* Write a response on the event's connection, serialized by the backend. */
  mustuse virtual fn reply(opaque *connection, u16 status,
                           const HttpHeaders &headers, StringView body)
      -> ErrorOr<Ok> = 0;

protected:
  HttpServer() = default;
};

} // namespace wr
