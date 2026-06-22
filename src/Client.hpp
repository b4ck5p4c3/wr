#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "ErrorOr.hpp"
#include "Http.hpp"
#include "StringView.hpp"

namespace wr {

/* A fluent builder for an outbound request over an explicit allocator. Each
   setter returns the builder, so a request is assembled in one chain and moved
   out by build(). */
class HttpRequestBuilder
{
public:
  explicit HttpRequestBuilder(Allocator allocator) : m_request(allocator) {}

  fn set_method(HttpMethod method) -> HttpRequestBuilder &
  {
    m_request.set_method(method);
    return *this;
  }
  fn set_url(StringView url) -> HttpRequestBuilder &
  {
    m_request.set_url(url);
    return *this;
  }
  fn add_header(StringView name, StringView value) -> HttpRequestBuilder &
  {
    m_request.headers().set(name, value);
    return *this;
  }
  fn set_body(StringView body) -> HttpRequestBuilder &
  {
    m_request.set_body(body);
    return *this;
  }

  mustuse fn build() -> HttpRequest { return steal(m_request); }

private:
  HttpRequest m_request;
};

/* The abstract HTTP client, modeled on the backend interface in oo. A concrete
   backend such as CurlClient performs the request and returns the response, or
   an Error when the transfer fails. */
class HttpClient
{
public:
  virtual ~HttpClient();

  HttpClient(const HttpClient &) = delete;
  HttpClient &operator=(const HttpClient &) = delete;

  mustuse virtual fn send(const HttpRequest &request)
      -> ErrorOr<HttpResponse> = 0;

protected:
  HttpClient() = default;
};

} // namespace wr
