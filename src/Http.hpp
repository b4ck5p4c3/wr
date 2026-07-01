#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "Maybe.hpp"
#include "String.hpp"
#include "StringMap.hpp"
#include "StringView.hpp"

namespace wr {

enum class HttpMethod : u8
{
  Get,
  Post,
  Put,
  Delete,
  Patch,
  Head,
  Options,
};

/* The uppercase request-line token. */
mustuse fn http_method_name(HttpMethod method) noexcept -> StringView;

/* Every standard HTTP status code. A response carries the raw u16, so a code
   outside this set is still kept. */
enum class HttpStatus : u16
{
  Continue = 100,
  SwitchingProtocols = 101,
  Processing = 102,
  EarlyHints = 103,

  Ok = 200,
  Created = 201,
  Accepted = 202,
  NonAuthoritativeInformation = 203,
  NoContent = 204,
  ResetContent = 205,
  PartialContent = 206,
  MultiStatus = 207,
  AlreadyReported = 208,
  ImUsed = 226,

  MultipleChoices = 300,
  MovedPermanently = 301,
  Found = 302,
  SeeOther = 303,
  NotModified = 304,
  UseProxy = 305,
  TemporaryRedirect = 307,
  PermanentRedirect = 308,

  BadRequest = 400,
  Unauthorized = 401,
  PaymentRequired = 402,
  Forbidden = 403,
  NotFound = 404,
  MethodNotAllowed = 405,
  NotAcceptable = 406,
  ProxyAuthenticationRequired = 407,
  RequestTimeout = 408,
  Conflict = 409,
  Gone = 410,
  LengthRequired = 411,
  PreconditionFailed = 412,
  ContentTooLarge = 413,
  UriTooLong = 414,
  UnsupportedMediaType = 415,
  RangeNotSatisfiable = 416,
  ExpectationFailed = 417,
  ImATeapot = 418,
  MisdirectedRequest = 421,
  UnprocessableContent = 422,
  Locked = 423,
  FailedDependency = 424,
  TooEarly = 425,
  UpgradeRequired = 426,
  PreconditionRequired = 428,
  TooManyRequests = 429,
  RequestHeaderFieldsTooLarge = 431,
  UnavailableForLegalReasons = 451,

  InternalServerError = 500,
  NotImplemented = 501,
  BadGateway = 502,
  ServiceUnavailable = 503,
  GatewayTimeout = 504,
  HttpVersionNotSupported = 505,
  VariantAlsoNegotiates = 506,
  InsufficientStorage = 507,
  LoopDetected = 508,
  NotExtended = 510,
  NetworkAuthenticationRequired = 511,
};

/* The reason phrase for the status line, owned by the wr HTTP layer since the
   mongoose helper is private. */
mustuse fn status_text(HttpStatus status) noexcept -> StringView;

/* A case-insensitive header collection. A name is stored lowercased and a set
   overwrites the previous value. */
class HttpHeaders
{
public:
  explicit HttpHeaders(Allocator allocator) : m_map(allocator) {}

  fn set(StringView name, StringView value) -> void
  {
    char buffer[NORMALIZE_CAPACITY];
    String spill{m_map.allocator()};
    m_map.set(normalize(name, buffer, spill), value);
  }

  mustuse fn get(StringView name) const -> Maybe<StringView>
  {
    char buffer[NORMALIZE_CAPACITY];
    String spill{m_map.allocator()};
    if (const String *found = m_map.find(normalize(name, buffer, spill));
        found != nullptr)
      return found->view();
    return None;
  }

  mustuse fn contains(StringView name) const -> bool
  {
    char buffer[NORMALIZE_CAPACITY];
    String spill{m_map.allocator()};
    return m_map.find(normalize(name, buffer, spill)) != nullptr;
  }

  fn remove(StringView name) -> void
  {
    char buffer[NORMALIZE_CAPACITY];
    String spill{m_map.allocator()};
    m_map.erase(normalize(name, buffer, spill));
  }

  mustuse pure fn count() const noexcept -> usize { return m_map.count(); }

  template <class Fn>
  fn for_each(Fn callback) const -> void
  {
    m_map.for_each([&](StringView name, const String &value) {
      callback(name, value.view());
    });
  }

  mustuse pure fn allocator() const noexcept -> Allocator
  {
    return m_map.allocator();
  }

private:
  static constexpr usize NORMALIZE_CAPACITY = 128;

  /* Lowercase a header name into the stack buffer when it fits, so a lookup of
     a normal-length name allocates nothing. A longer name is lowercased into
     the spill String instead. */
  static fn normalize(StringView name, char *buffer, String &spill) noexcept
      -> StringView
  {
    if (name.count() <= NORMALIZE_CAPACITY) {
      for (usize i = 0; i < name.count(); i++) {
        let const c = name[i];
        buffer[i] = c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c;
      }
      return StringView{buffer, name.count()};
    }
    for (usize i = 0; i < name.count(); i++) {
      let const c = name[i];
      spill.push(c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c);
    }
    return spill.view();
  }

  StringMap<String> m_map;
};

/* An owned outbound request, assembled by the builder in Client.hpp. */
class HttpRequest
{
public:
  explicit HttpRequest(Allocator allocator)
      : m_url(allocator), m_headers(allocator), m_body(allocator)
  {}

  mustuse pure fn method() const noexcept -> HttpMethod { return m_method; }
  fn set_method(HttpMethod method) noexcept -> void { m_method = method; }

  mustuse pure fn url() const noexcept -> StringView { return m_url.view(); }
  fn set_url(StringView url) -> void
  {
    m_url.clear();
    m_url.append(url);
  }

  mustuse fn headers() noexcept -> HttpHeaders & { return m_headers; }
  mustuse pure fn headers() const noexcept -> const HttpHeaders &
  {
    return m_headers;
  }

  mustuse pure fn body() const noexcept -> StringView { return m_body.view(); }
  fn set_body(StringView body) -> void
  {
    m_body.clear();
    m_body.append(body);
  }

private:
  HttpMethod m_method{HttpMethod::Get};
  String m_url;
  HttpHeaders m_headers;
  String m_body;
};

/* An owned response, filled by a client backend as the transfer completes. */
class HttpResponse
{
public:
  explicit HttpResponse(Allocator allocator)
      : m_headers(allocator), m_body(allocator)
  {}

  mustuse pure fn status() const noexcept -> u16 { return m_status; }
  fn set_status(u16 status) noexcept -> void { m_status = status; }

  mustuse fn headers() noexcept -> HttpHeaders & { return m_headers; }
  mustuse pure fn headers() const noexcept -> const HttpHeaders &
  {
    return m_headers;
  }

  mustuse pure fn body() const noexcept -> StringView { return m_body.view(); }
  fn append_body(StringView chunk) -> void { m_body.append(chunk); }

private:
  u16 m_status{0};
  HttpHeaders m_headers;
  String m_body;
};

} // namespace wr
