#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "Containers.hpp"
#include "Json.hpp"
#include "Maybe.hpp"
#include "Server.hpp"
#include "Store.hpp"
#include "Utils.hpp"

namespace wr {

class HttpClient;

/* The runtime configuration, from the flags and the environment. */
struct config
{
  String listen_url;
  String database_path;
  String web_root;
  String public_base_url;
  String github_client_id;
  String github_client_secret;
  String telegram_bot_token;
  String session_key;
  bool is_dev_mode = false;
};

/* The application context threaded through every request. It owns no
   per-request state, so the same instance serves the whole loop. The store and
   the client are borrowed and outlive the App. */
class App
{
public:
  App(Allocator allocator, Store &store, HttpClient &client, const config &cfg)
      : m_allocator(allocator), m_store(store), m_client(client), m_config(cfg)
  {}

  /* The mongoose handler entry. It recovers the App from the user pointer and
     dispatches the request. */
  static fn on_event(HttpServerEvent &event, opaque *user) -> void;

  mustuse pure fn allocator() const noexcept -> Allocator
  {
    return m_allocator;
  }

private:
  fn dispatch(HttpServerEvent &event) -> void;

  fn handle_sites(HttpServerEvent &event) -> void;
  fn handle_navigation(HttpServerEvent &event, StringView slug, StringView step,
                       bool wants_data) -> void;

  fn handle_login_github(HttpServerEvent &event) -> void;
  fn handle_github_callback(HttpServerEvent &event) -> void;
  fn handle_telegram_callback(HttpServerEvent &event) -> void;
  fn handle_dev_login(HttpServerEvent &event) -> void;
  fn handle_logout(HttpServerEvent &event) -> void;

  fn handle_config(HttpServerEvent &event) -> void;
  fn handle_me(HttpServerEvent &event) -> void;
  fn handle_user_add(HttpServerEvent &event, const account &who) -> void;
  fn handle_user_rename(HttpServerEvent &event, const account &who) -> void;
  fn handle_admin_add(HttpServerEvent &event) -> void;
  fn handle_admin_delete(HttpServerEvent &event) -> void;
  fn handle_admin_edit(HttpServerEvent &event) -> void;
  fn handle_admin_pending(HttpServerEvent &event) -> void;
  fn handle_admin_resolve(HttpServerEvent &event, bool should_approve) -> void;

  fn serve_static(HttpServerEvent &event) -> void;

  fn emit(HttpServerEvent &event, u16 status, const HttpHeaders &headers,
          StringView body) -> void;
  fn reply_json(HttpServerEvent &event, u16 status, StringView json) -> void;
  fn reply_text(HttpServerEvent &event, u16 status, StringView content_type,
                StringView body) -> void;
  fn reply_redirect(HttpServerEvent &event, StringView location) -> void;
  fn reply_message(HttpServerEvent &event, u16 status, StringView message)
      -> void;
  fn finish_login(HttpServerEvent &event, StringView identity,
                  StringView display_name, Maybe<bool> force_admin = {})
      -> void;
  mustuse fn current_account(HttpServerEvent &event) -> Maybe<account>;
  mustuse fn require_admin(HttpServerEvent &event) -> Maybe<account>;

  Allocator m_allocator;
  Store &m_store;
  HttpClient &m_client;
  const config &m_config;
};

/* The value of a query parameter, percent-decoded, or None when it is absent.
 */
mustuse fn find_query_param(StringView query, StringView name,
                            Allocator allocator) -> Maybe<String>;

/* The value of a cookie from a Cookie header, or None when it is absent. */
mustuse fn find_cookie(StringView cookie_header, StringView name)
    -> Maybe<StringView>;

/* Write a site as a PublicSite object, the slug, the name, the url, and the
   favicon. */
fn write_site_json(JsonWriter &writer, const site &row) -> void;

} // namespace wr
