#pragma once

#include "Allocator.hpp"
#include "Client.hpp"
#include "Common.hpp"
#include "ErrorOr.hpp"
#include "Http.hpp"

namespace wr {

/* The curl-backed HTTP client. One easy handle is built per request, so the
   client carries no per-request state. */
class CurlClient final : public HttpClient
{
public:
  /* Peer verification is on by default, and redirects are not followed, so an
     OAuth exchange never replays a secret to a redirect target and a liveness
     probe never reports a site that redirects to a parking page as up. */
  struct Options
  {
    u32 timeout_ms{30000};
    /* The connect phase is bounded separately when this is non-zero, so a host
       that stalls the handshake fails on its own short budget. */
    u32 connect_timeout_ms{0};
    bool should_verify_peer{true};
    bool should_follow_redirects{false};
    /* Refuse a connection to a private or loopback address, checked on the
       address curl actually connects to, so a DNS rebinding cannot steer the
       request at an internal host. The liveness probe sets this. */
    bool should_reject_private_addresses{false};
    /* The vendored OpenSSL carries no usable default trust store, so the
       system CA bundle is named for a verified TLS request. */
    const char *ca_path{"/etc/ssl/certs/ca-certificates.crt"};
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
