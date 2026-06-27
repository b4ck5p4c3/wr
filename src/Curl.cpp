#include "Curl.hpp"

#include "Net.hpp"
#include "String.hpp"
#include "Trace.hpp"

#include <curl/curl.h>
#include <sys/socket.h>

namespace wr {

namespace {

fn ensure_curl_is_initialized() noexcept -> void
{
  static const CURLcode init_result = curl_global_init(CURL_GLOBAL_DEFAULT);
  unused(init_result);
}

fn append_response_body(char *pointer, size_t size, size_t count,
                        opaque *user_data) -> size_t
{
  let const total_length = size * count;
  let response = static_cast<HttpResponse *>(user_data);
  response->append_body(StringView{pointer, total_length});
  return total_length;
}

fn collect_response_header(char *pointer, size_t size, size_t count,
                           opaque *user_data) -> size_t
{
  let const total_length = size * count;
  let response = static_cast<HttpResponse *>(user_data);
  let const line = StringView{pointer, total_length};

  /* A continuation line of an obsolete folded header starts with whitespace, so
     it is skipped rather than parsed as a header whose name is the whitespace.
   */
  if (line.count() > 0 && (line[0] == ' ' || line[0] == '\t')) {
    return total_length;
  }

  let const colon = line.find_character(':');
  if (colon.has_value()) {
    let const name = line.substring_of_length(0, colon.value());

    usize value_start_position = colon.value() + 1;
    while (value_start_position < line.count() &&
           (line[value_start_position] == ' ' ||
            line[value_start_position] == '\t'))
      value_start_position++;

    usize value_end_position = line.count();
    while (value_end_position > value_start_position &&
           (line[value_end_position - 1] == '\r' ||
            line[value_end_position - 1] == '\n'))
      value_end_position--;

    response->headers().set(
        name,
        line.substring_of_length(value_start_position,
                                 value_end_position - value_start_position));
  }

  return total_length;
}

/* The socket is opened only when the address curl resolved is public, so a host
   that rebinds to a private address between the early check and the connect is
   refused at connect time. */
fn open_public_socket(opaque *user_data, curlsocktype purpose,
                      curl_sockaddr *address) -> curl_socket_t
{
  unused(user_data);
  unused(purpose);
  if (address_is_private(reinterpret_cast<const sockaddr *>(&address->addr)))
    return CURL_SOCKET_BAD;
  return socket(address->family, address->socktype, address->protocol);
}

} // namespace

fn CurlClient::send(const HttpRequest &request) -> ErrorOr<HttpResponse>
{
  ensure_curl_is_initialized();

  HttpResponse response{m_allocator};

  CURL *handle = curl_easy_init();
  if (handle == nullptr) return Error{"Failed to create a curl handle"};
  defer { curl_easy_cleanup(handle); };

  let const url = String{m_allocator, request.url()};
  curl_easy_setopt(handle, CURLOPT_URL, url.c_str());

  let const method_name = http_method_name(request.method());
  curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, method_name.data);

  if (request.method() == HttpMethod::Head)
    curl_easy_setopt(handle, CURLOPT_NOBODY, 1L);

  curl_slist *header_list = nullptr;
  String header_line{m_allocator};
  request.headers().for_each([&](StringView name, StringView value) {
    header_line.clear();
    header_line.append(name);
    header_line.append(": ");
    header_line.append(value);

    // A failed append returns null and leaves the prior list untouched, so the
    // pointer is kept and only the one header is dropped.
    curl_slist *appended = curl_slist_append(header_list, header_line.c_str());
    if (appended != nullptr) header_list = appended;
  });
  defer
  {
    if (header_list != nullptr) curl_slist_free_all(header_list);
  };
  if (header_list != nullptr)
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, header_list);

  /* The body aliases the request, which outlives the perform. */
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
  if (m_options.connect_timeout_ms > 0)
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS,
                     static_cast<long>(m_options.connect_timeout_ms));
  curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER,
                   m_options.should_verify_peer ? 1L : 0L);
  curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST,
                   m_options.should_verify_peer ? 2L : 0L);
  if (m_options.should_verify_peer && m_options.ca_path != nullptr) {
    curl_easy_setopt(handle, CURLOPT_CAINFO, m_options.ca_path);
  }
  curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
  if (m_options.should_follow_redirects)
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
  if (m_options.should_reject_private_addresses)
    curl_easy_setopt(handle, CURLOPT_OPENSOCKETFUNCTION, &open_public_socket);

  LOG(Debug, "curl request %s %s", method_name.data, url.c_str());

  let const result = curl_easy_perform(handle);
  if (result != CURLE_OK) {
    LOG(Info, "curl request failed, %s %s: %s", method_name.data, url.c_str(),
        curl_easy_strerror(result));
    String message{m_allocator};
    message.append("Curl request failed, ");
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
