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

} // namespace wr
