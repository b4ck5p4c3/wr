#pragma once

#include "Allocator.hpp"
#include "Client.hpp"
#include "Common.hpp"
#include "ErrorOr.hpp"
#include "Http.hpp"

namespace wr {

/* The curl-backed HTTP client. One easy handle is built per request, so a
   client instance carries no per-request state and is reused across calls. */
class CurlClient final : public HttpClient
{
public:
  /* Per-client options. Peer verification is on by default, so a TLS request
     validates the chain unless a caller opts out. */
  struct Options
  {
    u32 timeout_ms{30000};
    bool should_verify_peer{true};
  };

  explicit CurlClient(Allocator allocator) : m_allocator(allocator) {}
  CurlClient(Allocator allocator, Options options)
      : m_allocator(allocator), m_options(options)
  {}

  mustuse fn send(const HttpRequest &request) -> ErrorOr<HttpResponse> override;

private:
  Allocator m_allocator;
  Options m_options;
};

} // namespace wr
