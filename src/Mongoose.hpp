#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "ErrorOr.hpp"
#include "Http.hpp"
#include "Server.hpp"
#include "StringView.hpp"

/* mongoose.h uses fn and noinline as ordinary identifiers, which collide with
   the Common.hpp macros, so they are neutralized across the include. */
#pragma push_macro("fn")
#pragma push_macro("noinline")
#undef fn
#undef noinline
#include "mongoose.h"
#pragma pop_macro("noinline")
#pragma pop_macro("fn")

namespace wr {

/* The mongoose-backed HTTP server. An accepted connection inherits the
   listener's user pointer, so the static trampoline recovers this server. */
class MongooseServer final : public HttpServer
{
public:
  explicit MongooseServer(Allocator allocator);
  ~MongooseServer() override;

  mustuse fn listen(StringView url, HttpServerHandler handler, opaque *user)
      -> ErrorOr<Ok> override;
  mustuse fn poll(u32 timeout_ms) -> ErrorOr<Ok> override;
  mustuse fn reply(opaque *connection, u16 status, const HttpHeaders &headers,
                   StringView body) -> ErrorOr<Ok> override;

private:
  static fn handle_event(mg_connection *connection, int event,
                         opaque *event_data) -> void;
  fn dispatch(mg_connection *connection, int event, opaque *event_data) -> void;

  Allocator m_allocator;
  mg_mgr m_manager;
  HttpServerHandler m_handler{nullptr};
  opaque *m_user{nullptr};
};

} // namespace wr
