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

/* The uppercase token for a method, as it is written on the request line. */
mustuse fn http_method_name(HttpMethod method) noexcept -> StringView;

/* The common status codes. A response carries the raw u16, so a code outside
   this set is still represented. */
enum class HttpStatus : u16
{
  Ok = 200,
  Created = 201,
  NoContent = 204,
  MovedPermanently = 301,
  Found = 302,
  NotModified = 304,
  BadRequest = 400,
  Unauthorized = 401,
  Forbidden = 403,
  NotFound = 404,
  MethodNotAllowed = 405,
  TooManyRequests = 429,
  InternalServerError = 500,
  BadGateway = 502,
  ServiceUnavailable = 503,
};

/* A case-insensitive header collection over a StringMap. A name is stored
   lowercased, since an HTTP header name is case-insensitive, and a set
   overwrites the previous value for that name. */
class HttpHeaders
{
public:
  explicit HttpHeaders(Allocator allocator) : m_map(allocator) {}

  fn set(StringView name, StringView value) -> void
  {
    String key{m_map.allocator()};
    append_lowercased(key, name);
    m_map.set(key.view(), value);
  }

  mustuse fn get(StringView name) const -> Maybe<StringView>
  {
    String key{m_map.allocator()};
    append_lowercased(key, name);
    if (const String *found = m_map.find(key.view()); found != nullptr)
      return found->view();
    return None;
  }

  mustuse fn contains(StringView name) const -> bool
  {
    String key{m_map.allocator()};
    append_lowercased(key, name);
    return m_map.find(key.view()) != nullptr;
  }

  fn remove(StringView name) -> void
  {
    String key{m_map.allocator()};
    append_lowercased(key, name);
    m_map.erase(key.view());
  }

  mustuse pure fn count() const noexcept -> usize { return m_map.count(); }

  /* Invoke the callback with each lowercased name and its value. */
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
  static fn append_lowercased(String &out, StringView text) -> void
  {
    for (usize i = 0; i < text.count(); i++) {
      let const c = text[i];
      out.push(c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c);
    }
  }

  StringMap<String> m_map;
};

/* An owned outbound request. The builder in Client.hpp assembles it, and a
   client backend consumes it. */
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

/* An owned response. A client backend fills the status, the headers, and the
   body as the transfer completes. */
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
