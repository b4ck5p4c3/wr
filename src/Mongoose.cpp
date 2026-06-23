#include "Mongoose.hpp"

#include "String.hpp"
#include "Trace.hpp"

namespace wr {

namespace {

/* The view aliases the mongoose buffer and is valid only while the message is.
 */
mustuse pure fn to_view(mg_str text) noexcept -> StringView
{
  return StringView{text.buf, text.len};
}

} // namespace

MongooseServer::MongooseServer(Allocator allocator) : m_allocator(allocator)
{
  mg_mgr_init(&m_manager);
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
                         const HttpHeaders &headers, StringView body)
    -> ErrorOr<Ok>
{
  let const mongoose_connection = static_cast<mg_connection *>(connection);

  String header_block{m_allocator};
  headers.for_each([&](StringView name, StringView value) {
    /* mongoose appends its own Content-Length, so a caller-supplied one is
       dropped to keep the response from carrying two framing headers. */
    if (name == "content-length") return;
    header_block.append(name);
    header_block.append(": ");
    header_block.append(value);
    header_block.append("\r\n");
  });

  /* The length-bounded conversion keeps a percent sign in the body from being
     read as a format directive. The precision is an int, so the body length is
     bounded below INT_MAX, which every served payload satisfies. */
  ASSERT(body.count() <= 0x7fffffffULL,
         "reply body too large for mg_http_reply");
  mg_http_reply(mongoose_connection, static_cast<int>(status),
                header_block.c_str(), "%.*s", static_cast<int>(body.count()),
                body.data);
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
                              to_view(message->body), request_headers);
    m_handler(request_event, m_user);
    break;
  }
  default: break;
  }
}

} // namespace wr
