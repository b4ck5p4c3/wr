#include "App.hpp"
#include "Client.hpp"
#include "Crypto.hpp"
#include "Http.hpp"
#include "Json.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

#include <cstdlib>

namespace wr {

fn App::handle_login_github(HttpServerEvent &event) -> void
{
  let const state_or = random_token(m_allocator);
  if (state_or.is_error()) {
    reply_message(event, 500, "Unable to start the login");
    return;
  }
  let const &state = state_or.value();

  String url{m_allocator};
  url.append("https://github.com/login/oauth/authorize?client_id=");
  url.append(m_config.github_client_id.view());
  url.append("&redirect_uri=");
  url.append(m_config.public_base_url.view());
  url.append("/auth/github/callback&scope=read:user&state=");
  url.append(state.view());

  String cookie{m_allocator};
  cookie.append("wr_oauth_state=");
  cookie.append(state.view());
  cookie.append("; Path=/; HttpOnly; SameSite=Lax; Max-Age=600");

  HttpHeaders headers{m_allocator};
  headers.set("Location", url.view());
  headers.set("Set-Cookie", cookie.view());
  unused(event.reply(302, headers, "").is_error());
}

fn App::handle_github_callback(HttpServerEvent &event) -> void
{
  let const code = find_query_param(event.query(), "code", m_allocator);
  let const state = find_query_param(event.query(), "state", m_allocator);
  let const cookie_header = event.request_headers().get("cookie");
  if (!code.has_value() || !state.has_value() || !cookie_header.has_value()) {
    reply_message(event, 400, "Missing the OAuth parameters");
    return;
  }
  let const cookie_state = find_cookie(cookie_header.value(), "wr_oauth_state");
  if (!cookie_state.has_value() ||
      !constant_time_equal(state.value().view(), cookie_state.value()))
  {
    reply_message(event, 400, "The OAuth state did not match");
    return;
  }

  String token_body{m_allocator};
  token_body.append("client_id=");
  token_body.append(m_config.github_client_id.view());
  token_body.append("&client_secret=");
  token_body.append(m_config.github_client_secret.view());
  token_body.append("&code=");
  token_body.append(code.value().view());

  HttpRequestBuilder token_builder{m_allocator};
  let const token_request =
      token_builder.set_method(HttpMethod::Post)
          .set_url("https://github.com/login/oauth/access_token")
          .add_header("Accept", "application/json")
          .add_header("Content-Type", "application/x-www-form-urlencoded")
          .set_body(token_body.view())
          .build();
  let token_response = m_client.send(token_request);
  if (token_response.is_error()) {
    reply_message(event, 502, "The token exchange failed");
    return;
  }
  let const access_token = json_get_string(
      m_allocator, token_response.value().body(), "access_token");
  if (!access_token.has_value()) {
    reply_message(event, 401, "GitHub refused the code");
    return;
  }

  String authorization{m_allocator};
  authorization.append("token ");
  authorization.append(access_token.value().view());

  HttpRequestBuilder user_builder{m_allocator};
  let const user_request =
      user_builder.set_method(HttpMethod::Get)
          .set_url("https://api.github.com/user")
          .add_header("Authorization", authorization.view())
          .add_header("User-Agent", "wr-webring")
          .add_header("Accept", "application/json")
          .build();
  let user_response = m_client.send(user_request);
  if (user_response.is_error()) {
    reply_message(event, 502, "The identity fetch failed");
    return;
  }

  let const id =
      json_get_number(m_allocator, user_response.value().body(), "id");
  let const login =
      json_get_string(m_allocator, user_response.value().body(), "login");
  if (!id.has_value() || !login.has_value()) {
    reply_message(event, 502, "GitHub returned no identity");
    return;
  }

  char id_text[24];
  std::snprintf(id_text, sizeof(id_text), "%lld",
                static_cast<long long>(id.value()));
  String identity{m_allocator};
  identity.append("github:");
  identity.append(id_text);
  finish_login(event, identity.view(), login.value().view());
}

fn App::handle_telegram_callback(HttpServerEvent &event) -> void
{
  let const query = event.query();
  let const provided_hash = find_query_param(query, "hash", m_allocator);
  let const id = find_query_param(query, "id", m_allocator);
  if (!provided_hash.has_value() || !id.has_value()) {
    reply_message(event, 400, "Missing the Telegram parameters");
    return;
  }

  /* The check string is the present fields in alphabetical order, each as a
     key=value line, with the hash field left out. */
  let const fields = {"auth_date", "first_name", "id",
                      "last_name", "photo_url",  "username"};
  String check{m_allocator};
  bool is_first = true;
  for (const char *field : fields) {
    let const value = find_query_param(query, field, m_allocator);
    if (!value.has_value()) continue;
    if (!is_first) check.push('\n');
    is_first = false;
    check.append(field);
    check.push('=');
    check.append(value.value().view());
  }

  /* The secret key is the SHA-256 of the bot token, and the signature is the
     HMAC of the check string under that key. */
  unsigned char secret[32] = {};
  sha256(m_config.telegram_bot_token.view(), secret);

  unsigned char digest[32] = {};
  hmac_sha256(
      StringView{reinterpret_cast<const char *>(secret), sizeof(secret)},
      check.view(), digest);

  String computed{m_allocator};
  append_hex(computed, digest, sizeof(digest));
  if (!constant_time_equal(computed.view(), provided_hash.value().view())) {
    reply_message(event, 401, "The Telegram signature did not match");
    return;
  }

  /* A valid signature can be replayed forever, so the login is rejected once it
     is more than a day old. */
  let const auth_date = find_query_param(query, "auth_date", m_allocator);
  let const signed_at = auth_date.has_value()
                            ? static_cast<i64>(std::strtoll(
                                  auth_date.value().c_str(), nullptr, 10))
                            : 0;
  if (signed_at <= 0 || now_seconds() - signed_at > 86400) {
    reply_message(event, 401, "The Telegram login has expired");
    return;
  }

  let const display = find_query_param(query, "first_name", m_allocator);
  String identity{m_allocator};
  identity.append("telegram:");
  identity.append(id.value().view());
  finish_login(event, identity.view(),
               display.has_value() ? display.value().view()
                                   : id.value().view());
}

fn App::handle_logout(HttpServerEvent &event) -> void
{
  let const account_cookie = event.request_headers().get("cookie");
  if (account_cookie.has_value()) {
    let const token = find_cookie(account_cookie.value(), "wr_session");
    if (token.has_value())
      unused(m_store.delete_session(token.value()).is_error());
  }

  HttpHeaders headers{m_allocator};
  headers.set("Location", "/");
  headers.set("Set-Cookie",
              "wr_session=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0");
  unused(event.reply(302, headers, "").is_error());
}

fn App::finish_login(HttpServerEvent &event, StringView identity,
                     StringView display_name) -> void
{
  bool was_admin = false;
  let const existing = m_store.find_account(identity);
  if (!existing.is_error() && existing.value().has_value()) {
    was_admin = existing.value().value().is_admin;
  }

  if (m_store.upsert_account(identity, display_name, was_admin).is_error()) {
    reply_message(event, 500, "Unable to store the account");
    return;
  }

  let const token_or = random_token(m_allocator);
  if (token_or.is_error()) {
    reply_message(event, 500, "Unable to open the session");
    return;
  }
  let const &token = token_or.value();

  let const expires_at = now_seconds() + i64{30} * 24 * 60 * 60;
  if (m_store.create_session(token.view(), identity, expires_at).is_error()) {
    reply_message(event, 500, "Unable to open the session");
    return;
  }

  String cookie{m_allocator};
  cookie.append("wr_session=");
  cookie.append(token.view());
  cookie.append("; Path=/; HttpOnly; SameSite=Lax; Max-Age=2592000");

  HttpHeaders headers{m_allocator};
  headers.set("Location", was_admin ? "/admin" : "/panel");
  headers.set("Set-Cookie", cookie.view());
  LOG(Info, "login for %s", String{m_allocator, identity}.c_str());
  unused(event.reply(302, headers, "").is_error());
}

fn App::current_account(HttpServerEvent &event) -> Maybe<account>
{
  let const cookie_header = event.request_headers().get("cookie");
  if (!cookie_header.has_value()) return None;
  let const token = find_cookie(cookie_header.value(), "wr_session");
  if (!token.has_value()) return None;

  let const session_row = m_store.find_session(token.value());
  if (session_row.is_error() || !session_row.value().has_value()) return None;
  if (session_row.value().value().expires_at < now_seconds()) return None;

  let const found =
      m_store.find_account(session_row.value().value().identity.view());
  if (found.is_error()) return None;
  return found.value();
}

} // namespace wr
