#include "Curl.hpp"

#include "String.hpp"
#include "Trace.hpp"

#include <curl/curl.h>

namespace wr {

namespace {

/* curl initializes its global state once per process. A function-local static
   runs the init exactly once and is safe across threads. */
fn ensure_curl_is_initialized() noexcept -> void
{
  static const CURLcode init_result = curl_global_init(CURL_GLOBAL_DEFAULT);
  unused(init_result);
}

/* The write callback appends each body chunk into the response. curl passes the
   response pointer through CURLOPT_WRITEDATA. */
fn append_response_body(char *pointer, size_t size, size_t count,
                        opaque *user_data) -> size_t
{
  let const total_length = size * count;
  let response = static_cast<HttpResponse *>(user_data);
  response->append_body(StringView{pointer, total_length});
  return total_length;
}

/* The header callback receives one header line at a time. The status line and
   the terminating blank line carry no colon and are skipped. */
fn collect_response_header(char *pointer, size_t size, size_t count,
                           opaque *user_data) -> size_t
{
  let const total_length = size * count;
  let response = static_cast<HttpResponse *>(user_data);
  let const line = StringView{pointer, total_length};

  let const colon = line.find_character(':');
  if (colon.has_value()) {
    let const name = line.substring_of_length(0, colon.value());

    usize value_start = colon.value() + 1;
    while (value_start < line.count() &&
           (line[value_start] == ' ' || line[value_start] == '\t'))
      value_start++;

    usize value_end = line.count();
    while (value_end > value_start &&
           (line[value_end - 1] == '\r' || line[value_end - 1] == '\n'))
      value_end--;

    response->headers().set(
        name, line.substring_of_length(value_start, value_end - value_start));
  }

  return total_length;
}

} // namespace

fn CurlClient::send(const HttpRequest &request) -> ErrorOr<HttpResponse>
{
  ensure_curl_is_initialized();

  HttpResponse response{m_allocator};

  CURL *handle = curl_easy_init();
  if (handle == nullptr) return Error{"curl_easy_init failed"};
  defer { curl_easy_cleanup(handle); };

  let const url = String{m_allocator, request.url()};
  curl_easy_setopt(handle, CURLOPT_URL, url.c_str());

  /* The method token is a string literal, so it is NUL terminated and is
     handed to curl directly. */
  let const method_name = http_method_name(request.method());
  curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, method_name.data);

  if (request.method() == HttpMethod::Head)
    curl_easy_setopt(handle, CURLOPT_NOBODY, 1L);

  curl_slist *header_list = nullptr;
  request.headers().for_each([&](StringView name, StringView value) {
    String line{m_allocator};
    line.append(name);
    line.append(": ");
    line.append(value);
    header_list = curl_slist_append(header_list, line.c_str());
  });
  defer
  {
    if (header_list != nullptr) curl_slist_free_all(header_list);
  };
  if (header_list != nullptr)
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, header_list);

  /* The body pointer aliases the request, which outlives the perform below, so
     curl reads it in place with the length given. */
  if (!request.body().is_empty()) {
    curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE,
                     static_cast<curl_off_t>(request.body().count()));
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request.body().data);
  }

  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &append_response_body);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, &collect_response_header);
  curl_easy_setopt(handle, CURLOPT_HEADERDATA, &response);

  curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS,
                   static_cast<long>(m_options.timeout_ms));
  curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER,
                   m_options.should_verify_peer ? 1L : 0L);
  curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST,
                   m_options.should_verify_peer ? 2L : 0L);
  curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);

  LOG(Debug, "curl request %s %s", method_name.data, url.c_str());

  let const result = curl_easy_perform(handle);
  if (result != CURLE_OK) {
    String message{m_allocator};
    message.append("curl request failed, ");
    message.append(curl_easy_strerror(result));
    return Error{message.view()};
  }

  long status_code = 0;
  curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status_code);
  response.set_status(static_cast<u16>(status_code));

  LOG(Debug, "curl response %s %s gave %ld", method_name.data, url.c_str(),
      status_code);

  return response;
}

} // namespace wr
