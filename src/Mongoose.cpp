#include "Mongoose.hpp"

#include "Common.hpp"
#include "String.hpp"
#include "Trace.hpp"

namespace wr {

namespace {

/* The panel and the admin bodies are small JSON, so a larger body is refused at
   the boundary to keep a request from pinning memory. */
constexpr usize MAX_REQUEST_BODY_LENGTH = usize{1} << 20;

/* The view aliases the mongoose buffer and is valid only while the message is.
 */
mustuse pure fn to_view(mg_str text) noexcept -> StringView
{
  return StringView{text.buf, text.len};
}

/* The logger verbosity drives the mongoose level, so one knob controls both. */
pure fn mongoose_level_for(verbosity level) noexcept -> int
{
  return level == verbosity::All ? MG_LL_VERBOSE : MG_LL_NONE;
}

} // namespace

MongooseServer::MongooseServer(Allocator allocator) : m_allocator(allocator)
{
  mg_mgr_init(&m_manager);

  /* The default mongoose sink prints a per-connection trace to standard output
     and ignores the -L log file. The sink is redirected into the logger and the
     level is taken from the logger verbosity. */
  mg_log_set(mongoose_level_for(LOGGER_VERBOSITY));
  mg_log_set_fn(&MongooseServer::log_sink, this);
}

fn MongooseServer::log_sink(char character, opaque *user) -> void
{
  let const server = static_cast<MongooseServer *>(user);
  if (server == nullptr) return;

  if (character == '\n') {
    if (server->m_log_line_length > 0) {
      let const line =
          StringView{server->m_log_line, server->m_log_line_length};
      LOG(Everything, "mongoose: %.*s", static_cast<int>(line.count()),
          line.data);
      server->m_log_line_length = 0;
    }
    return;
  }

  if (server->m_log_line_length < MAX_LOG_LINE_LENGTH)
    server->m_log_line[server->m_log_line_length++] = character;
}

MongooseServer::~MongooseServer() { mg_mgr_free(&m_manager); }

fn MongooseServer::listen(StringView url, HttpServerHandler handler,
                          opaque *user) -> ErrorOr<Ok>
{
  m_handler = handler;
  m_user = user;

  let const listen_url = String{m_allocator, url};
  let const connection =
      mg_http_listen(&m_manager, listen_url.c_str(), &handle_event, this);
  if (connection == nullptr) {
    String message{m_allocator};
    message.append("Failed to listen on ");
    message.append(url);
    return Error{message.view()};
  }

  LOG(Info, "http server is listening on %s", listen_url.c_str());
  return Success;
}

fn MongooseServer::poll(u32 timeout_ms) -> ErrorOr<Ok>
{
  mg_mgr_poll(&m_manager, static_cast<int>(timeout_ms));
  return Success;
}

fn MongooseServer::reply(opaque *connection, u16 status,
                         const HttpHeaders &headers, StringView body,
                         StringView static_headers) -> ErrorOr<Ok>
{
  let const mongoose_connection = static_cast<mg_connection *>(connection);

  let const reason = status_text(static_cast<HttpStatus>(status));

  let head_length = static_headers.count() + reason.count() + 64;
  headers.for_each([&](StringView name, StringView value) {
    head_length += name.count() + value.count() + 4;
  });

  String head{m_allocator};
  head.reserve(head_length);

  char line[48];
  let const line_length = std::snprintf(line, sizeof(line), "HTTP/1.1 %d ",
                                        static_cast<int>(status));
  if (line_length > 0)
    head.append(StringView{line, static_cast<usize>(line_length)});
  head.append(reason);
  head.append("\r\n");

  head.append(static_headers);
  headers.for_each([&](StringView name, StringView value) {
    /* mongoose appends its own Content-Length, so a caller-supplied one is
       dropped to keep the response from carrying two framing headers. */
    if (name == "content-length") return;
    head.append(name);
    head.append(": ");
    head.append(value);
    head.append("\r\n");
  });

  char framing[40];
  let const framing_length =
      std::snprintf(framing, sizeof(framing), "Content-Length: %lu\r\n\r\n",
                    static_cast<unsigned long>(body.count()));
  if (framing_length > 0)
    head.append(StringView{framing, static_cast<usize>(framing_length)});

  /* mongoose printf reads the body through %s, which stops at the first null
     byte and truncates a binary asset such as a woff2 font or a webp image. The
     head is sent as one buffer and the body is sent as raw bytes. */
  mg_send(mongoose_connection, head.c_str(), head.length());
  mg_send(mongoose_connection, body.data, body.count());
  mongoose_connection->is_resp = 0;
  return Success;
}

fn MongooseServer::handle_event(mg_connection *connection, int event,
                                opaque *event_data) -> void
{
  let const server = static_cast<MongooseServer *>(connection->fn_data);
  if (server != nullptr) server->dispatch(connection, event, event_data);
}

fn MongooseServer::dispatch(mg_connection *connection, int event,
                            opaque *event_data) -> void
{
  if (m_handler == nullptr) return;

  switch (event) {
  case MG_EV_ACCEPT: {
    HttpServerEvent open_event{HttpServerEvent::Kind::Open, *this, connection};
    m_handler(open_event, m_user);
    break;
  }
  case MG_EV_CLOSE: {
    HttpServerEvent close_event{HttpServerEvent::Kind::Close, *this,
                                connection};
    m_handler(close_event, m_user);
    break;
  }
  case MG_EV_ERROR: {
    HttpServerEvent error_event{HttpServerEvent::Kind::Error, *this,
                                connection};
    error_event.set_error_message(
        StringView{static_cast<const char *>(event_data)});
    m_handler(error_event, m_user);
    break;
  }
  case MG_EV_HTTP_MSG: {
    let const message = static_cast<mg_http_message *>(event_data);

    if (message->body.len > MAX_REQUEST_BODY_LENGTH) {
      let const reason = status_text(HttpStatus::ContentTooLarge);
      mg_printf(connection, "HTTP/1.1 413 %.*s\r\nContent-Length: 0\r\n\r\n",
                static_cast<int>(reason.count()), reason.data);
      connection->is_resp = 0;
      break;
    }

    /* The header map lives only for the handler call. */
    HttpHeaders request_headers{m_allocator};

    for (usize i = 0; i < MG_MAX_HTTP_HEADERS; i++) {
      let const &header = message->headers[i];
      if (header.name.len == 0) break;
      request_headers.set(to_view(header.name), to_view(header.value));
    }

    HttpServerEvent request_event{HttpServerEvent::Kind::Request, *this,
                                  connection};
    request_event.set_request(to_view(message->method), to_view(message->uri),
                              to_view(message->query), to_view(message->body),
                              request_headers);

    char client_ip[52];
    let ip_length = mg_snprintf(client_ip, sizeof(client_ip), "%M", mg_print_ip,
                                &connection->rem);
    /* mg_snprintf reports the count it would have written, so a truncation is
       clamped to the buffer before the view is taken. */
    if (ip_length >= sizeof(client_ip)) ip_length = sizeof(client_ip) - 1;
    request_event.set_client_ip(StringView{client_ip, ip_length});

    m_handler(request_event, m_user);
    break;
  }
  default: break;
  }
}

} // namespace wr
