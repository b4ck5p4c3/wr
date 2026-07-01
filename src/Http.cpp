#include "Http.hpp"

namespace wr {

fn http_method_name(HttpMethod method) noexcept -> StringView
{
  switch (method) {
  case HttpMethod::Get: return "GET";
  case HttpMethod::Post: return "POST";
  case HttpMethod::Put: return "PUT";
  case HttpMethod::Delete: return "DELETE";
  case HttpMethod::Patch: return "PATCH";
  case HttpMethod::Head: return "HEAD";
  case HttpMethod::Options: return "OPTIONS";
  }
  unreachable();
}

fn status_text(HttpStatus status) noexcept -> StringView
{
  switch (status) {
  case HttpStatus::Ok: return "OK";
  case HttpStatus::Found: return "Found";
  case HttpStatus::BadRequest: return "Bad Request";
  case HttpStatus::Unauthorized: return "Unauthorized";
  case HttpStatus::Forbidden: return "Forbidden";
  case HttpStatus::NotFound: return "Not Found";
  case HttpStatus::MethodNotAllowed: return "Method Not Allowed";
  case HttpStatus::Conflict: return "Conflict";
  case HttpStatus::ContentTooLarge: return "Payload Too Large";
  case HttpStatus::TooManyRequests: return "Too Many Requests";
  case HttpStatus::InternalServerError: return "Internal Server Error";
  case HttpStatus::BadGateway: return "Bad Gateway";
  default: return "OK";
  }
}

} // namespace wr
