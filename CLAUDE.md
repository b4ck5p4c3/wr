# wr project notes

wr is a C++ webring backend. A dynamic page that lists the member sites is
served, alongside a user panel, an admin panel, GitHub and Telegram auth, and a
periodic liveness check.

## Build

- The static binary is written to ../wr by `make MODE=rel`.
- ../wr-dbg is written by `make MODE=dbg` with AddressSanitizer and UndefinedBehaviorSanitizer, and dbg is the default.
- ../wr-cov is written by `make MODE=cov` with coverage instrumentation.
- The portable ../wr-cosmo.com and ../wr-cosmo_dbg.com are written by `make MODE=cosmo` and `make MODE=cosmo_dbg` through the cosmopolitan toolchain. The cosmo modes compile the foundation and the server core, but the outbound curl layer is not compiled under the cosmopolitan toolchain yet, so the curl-dependent server links only in the dbg, rel, and cov modes.
- The frontend is built under the web directory by `bun install` and `bun run build`, which writes web/dist. The build first runs `bun gen-docs.js`, which regenerates web/public/docs.html from openapi.yaml, then runs vite. A bare `make web` from the root runs the `bun run build` step. The frontend is formatted by `bun run format`, which runs prettier over web/src.
- The built frontend is embedded into the binary. `make web` writes web/dist, then the server build runs `bun web/gen-embed.js`, which writes src/EmbeddedAssets.gen.cpp, one byte array per file with a table from the url path to the bytes. The static handler serves a request from that table through src/Embedded, and the `-w` web-root flag is removed. The bytes are emitted as a plain array, since the cosmopolitan GCC 14.1 toolchain does not accept the C23 #embed directive. The generated source is regenerated when web/dist changes and is removed by `make clean`.
- The dbg, rel, and cov modes are built exceptionless with -fno-exceptions, while the cosmo modes are built with -fexceptions, since the cosmopolitan toolchain requires it. The source stays exception-free in every mode.
- UndefinedBehaviorSanitizer is kept on in the release build, and the release is compiled at -O2.
- A bare `make` builds the wr target, since the object tree under src/o/$(MODE) is created as an order-only prerequisite.
- clang and clang++ at -std=c++23 are the toolchain, and mold or lld is used as the linker when present.
- clang-format and clang-tidy are run by `make fmt` and `make tidy`, and the artifacts are removed by `make clean`.
- The pre-commit hook is symlinked by `./install-hooks.sh`. It rejects a checkin marker in any staged file, runs `make fmt` in src and restages the formatted C++ sources, and when a staged web/src JS, JSX, or CSS file is present and bun is on the path it runs the prettier format script in web and restages those files.

## Vendoring

- mongoose is a submodule at vendor/mongoose, and all server work is driven through it.
- curl is a submodule at vendor/curl, and all outbound work is driven through it. The curl library tree is compiled into the object tree against a committed config header at vendor/curl_config.h, and only the QUIC and SSH backends are left out. The QUIC core is kept for the HTTP/3 stubs the HTTPS path references.
- mbedtls is the TLS backend that curl is built against. mbedtls is a submodule at vendor/mbedtls pinned to the v3.6.6 tag. The release tag bundles the generated sources, so the checkout compiles without the framework submodule and without the code generation a development checkout needs. It is compiled into the object tree against a committed config header at vendor/mbedtls_config.h, and TLS 1.2 and TLS 1.3 are enabled.
- sqlite is the amalgamation at vendor/sqlite, and it is the only store.
- rapidjson is a header-only submodule at vendor/rapidjson pinned to the v1.1.0 tag, and only its SAX reader is used to parse a request body and an OAuth response behind the Json class in src/Json. The header is included only in src/Json.cpp. v1.1.0 offsets a null pointer in its internal stack. The minimal UndefinedBehaviorSanitizer runtime traps that offset, so src/Json.o is built without that sanitizer through a target rule in the makefile.
- The vendored mongoose, sqlite, curl, and mbedtls C sources are compiled into the same object tree under o/$(MODE)/vendor.
- The curl and mbedtls sources are compiled in the dbg, rel, and cov modes. The cosmo modes leave them out for now, so the portable build carries no outbound TLS.
- The curl config header is generated once by curl's cmake against mbedtls, then committed, and the mbedtls config header is the upstream default. The curl config is selected through HAVE_CONFIG_H, and the mbedtls config is selected through MBEDTLS_CONFIG_FILE.
- A dependency upgrade waits for approval.

## Code conventions

- The macro layer is defined in src/Common.hpp.
- The C++ runtime library is not used. move, forward, and remove_reference are defined in-house in src/Common.hpp, the log value formatter selects its overload through the compiler type-trait builtins, and the liveness stop flag is read and written through the atomic builtins. Only the C library headers and the freestanding <initializer_list> and <new> are included.
- The container and the allocator foundation is split across src/String, src/StringView, src/ArrayList, src/Maybe, src/StringMap, src/PackedStringKey, src/StaticStringMap, src/Path, src/Allocator, and src/Debug, all under namespace wr. src/StringMap is an open-addressing string-keyed hash table, and src/PackedStringKey is its probe key. src/StaticStringMap is a consteval string-keyed lookup table read by the dispatch and the content-type paths as data.
- The flag parser is src/Cli, and ErrorBase, Error, and Warning are held in src/Errors.
- The build is exceptionless. -fno-exceptions is set, so a fallible function returns ErrorOr<T> from src/ErrorOr.hpp rather than throwing, and an error is propagated early by the TRY macro.
- An error carries a severity read through ErrorBase::is_critical(), and the default is recoverable. A recoverable error is reported and the server keeps serving, a request fails with a 500 and a liveness probe is logged and retried. A critical error means the service cannot run, the database open is one, so startup exits and the supervisor restarts it. A library result code is translated to text in the message, the sqlite message, the mbedtls_strerror text, the curl_easy_strerror text, and strerror(errno) for a file or thread call.
- An unrecoverable allocation failure aborts, since a constructor cannot return an ErrorOr.
- A raw pointer to untyped memory is written `opaque *`, the alias for `void *` defined in src/Common.hpp.
- A function is written in the `fn name(...) -> ret` form, and a function that never unwinds is marked `noexcept`.
- A comment is avoided where the code can carry the meaning. A clearer name or a smaller function is reached for first, and a comment is kept only when the reason still does not read from the code. A comment states why the code is the way it is, never what it does, and the "so" justification shape and the justify-by-contrast shape are not used.
- A new comment is the last resort. A comment that restates the code or repeats a clear name is deleted on sight, and a comment survives only when it records a reason that cannot be recovered from the code, such as an external constraint, a security invariant, or a non-obvious sentinel value.
- A local is written with `let` and `let const`, so a deducible type is never spelled out. A literal counter such as `usize i = 0` keeps its spelled type, since `let i = 0` would deduce int rather than the unsigned type.
- The integer aliases are `usize`, `u8` through `u64`, and `i8` through `i64`.
- A null pointer check is written `!= nullptr` or `== nullptr`, never a bare truthiness test.
- A boolean is named with an `is_`, `should_`, `was_`, `did_`, or `has_` prefix, and a number carries a `_count` or a measure suffix.
- A variable-bound lambda is named `do_`, and an accessor is named with a `get_` or `set_` prefix.
- An if whose condition has `&&` or `||` is braced, while a single-condition if stays unbraced.
- A chain of three or more name comparisons is written as a `consteval StaticStringMap` rather than an if ladder.
- A stray enum or struct is lower_snake_case, and only a class and a nested type are CamelCase.
- A file operation takes a Path, not a String or a StringView.
- State is threaded through context structs and constructors, and no mutable global holds per-request state.
- A logical block is separated by a blank line, before and after a loop, before a return, and after a group of declarations.
- A free helper whose receiver is a value type is written as a method on that type.
- A fallible lookup returns a `Maybe<T>` rather than a sentinel value.
- A file-scope global is named in SCREAMING_SNAKE_CASE, the same as the logger verbosity.
- An existing helper is reused rather than a second copy written, and a new abstraction waits for approval.

## Architecture

- The sqlite database is opened, the server is built, the liveness sweep is started on its own thread, and the loop is run by src/Main.cpp.
- A request is routed by the HTTP layer to the page renderer, the JSON API, the auth endpoints, or the static asset handler. The JSON API lives under the `/api/v1` prefix. The required method is data in the dispatch route table, a read is reached by GET, a write by POST, and a delete or a reject by DELETE, and a wrong method is answered with a 405.
- The HTTP layer is a generic interface over pluggable backends, modeled on the backend pattern in the oo project. The shared value types HttpMethod, HttpStatus, HttpHeaders, HttpRequest, and HttpResponse are held in src/Http. The abstract HttpClient and the HttpRequestBuilder are held in src/Client, and the abstract HttpServer with its HttpServerEvent is held in src/Server. The curl client backend CurlClient is held in src/Curl, and the mongoose server backend MongooseServer is held in src/Mongoose. A type that owns memory takes an explicit Allocator in its constructor.
- The application context App in src/App owns the dispatch and the request helpers, and it routes a request to the public navigation API, the auth endpoints, the panel API, or the static asset handler. The JSON responses are built by the writer in src/Json, and a request body or an OAuth response is parsed once into the Json value class there. A member is read through operator[] and a leaf is converted through to, backed by the rapidjson SAX reader. The GitHub and Telegram auth, the sessions, and the current-account lookup are in src/Auth. The user and admin panel handlers are in src/Panel. The liveness sweep runs on its own thread from src/Liveness with its own store connection and curl client.
- The store layer is split over a backend interface. The abstract SqlDatabase and the prepared SqlStatement are held in src/Sql, and the concrete Sqlite backend in src/Sqlite owns the sqlite3 connection and is opened from a connection string. src/Store borrows a SqlDatabase by reference, runs the migration through migrate(), and holds the row types for sites, accounts, sessions, pending actions, audit entries, and comments. The liveness history is kept in the liveness_buckets table, one row per site per hour, and is read back as a 168 entry array through get_liveness_history. The emoji reactions are kept in the reactions table, one row per site per emoji per identity, toggled through toggle_reaction and counted through get_reactions. The audit actions are kept in the audit_log table, one row per recorded action, written through record_audit and read back newest first through list_audit. The acting identity, the acting client address, the action, the target, and a detail are stored, and the client address is read from the socket peer, or from a forwarded header under `--trust-forwarded-headers`. An identity is two columns, the provider source as an integer enum and the handle, so the packed prefix string is gone. The audit JSON carries the actor provider and handle as actor_oauth and actor_tag, derived from the stored source and handle, so the actor name links to the github or telegram profile in the admin panel. The footer comments are kept in the comments table, one row per comment, written through add_comment with an is_approved flag set for an admin author and unset for a user. An approved page is read newest first through list_comments by a limit and an offset, the unapproved queue is read through list_pending_comments, a single row is read through find_comment, a comment is approved through approve_comment, and a comment is removed through delete_comment. Main.cpp opens the Sqlite backend, constructs the store over it, and calls migrate(). A column is read through SqlStatement::get<T>() and a parameter is bound through bind(), and the position advances on every call, so a call site spells no positional number. A compiled statement is kept by the Sqlite backend keyed by its sql, the cache is bounded and the least recently used entry is finalized when it is full, so a repeated query is reset instead of recompiled. A leased statement is reset on destruction and the handle is returned to the cache. Each connection owns its own cache, so the event loop and the liveness sweep take no lock on the prepare path. The event-loop connection's cache is flushed on demand through clear_statement_cache, which the admin cache clear endpoint calls, so the cached statements are finalized while the liveness connection is left untouched.
- The Preact frontend lives under the web directory, and the JSON API is the only boundary between the server and the bundle.
- The public webring API documentation is served at /docs from the embedded docs.html, directly by the static handler rather than the single-page shell. The docs link sits in the top nav and loads the static page with a full navigation. docs.html is generated from openapi.yaml by web/gen-docs.js, which the web build runs ahead of vite. The page lists each endpoint by method, path, and summary, with its request body fields and its responses. The raw openapi.yaml is copied beside the page and linked from it.
- curl is wrapped by the outbound layer for the liveness probes and the OAuth token exchange.
- The member sites, the panel users, the panel admins, and the sessions are held in the data model.
- A user owns sites through the user panel, and an admin approves a site, removes a site, manages the users, and reads the audit trail and the streaming server log tail through the admin panel. The causing actions are recorded to the audit_log through Store::record_audit, the admin site add, edit, and remove, the pending approve and reject, and the user submit and reaction toggle, each with the acting identity and the client address, and the trail is read through the `/api/v1/admin/audit` endpoint. An admin clears the server prepared-statement cache through the `/api/v1/admin/cache/clear` endpoint, and the cache clear is recorded in the trail.
- Authentication is run through GitHub OAuth and the Telegram login widget, and a session row is opened and the session cookie is set on a login.
- The `--enable-dangerous-developer-environment` flag turns on dev mode, which exposes a login bypass at /auth/dev for an admin or a user. The client reads the dev state and the available providers from /api/v1/config. Outside dev mode the server refuses to start unless at least one auth provider is configured, GitHub through both the client id and secret or Telegram through the bot token, and unless WR_SESSION_KEY is set.
- The server flags are `--listen-address` for the bind URL, `--database-url` for the store connection string, `--database-backend` which only accepts sqlite and defaults to it, and `--web-root-url` for the public base URL the OAuth redirect is built from, which defaults to the listen URL when it is not given. Each of these four falls back to an environment variable named WR_ plus the long flag uppercased with the dashes turned into underscores, so `--listen-address` reads WR_LISTEN_ADDRESS, and the CLI value wins over the environment. Only `--listen-address` and `--database-url` are required. The `--enable-metrics` flag turns on the per-site click and hop recording. The `--trust-forwarded-headers` flag reads the client address from the `x-forwarded-for` and `x-real-ip` headers for running behind a reverse proxy, and the default reads the un-spoofable socket peer. The `--list-embedded-assets` flag prints the embedded asset tree with the size of each file in decimal kB and exits, and `--version` prints the build banner with the copyright line.
- Each request is rate limited per client IP by the RateLimiter in src/RateLimiter, keyed by the address read through client_address and bucketed by the route. The address defaults to the socket peer, which a client cannot spoof, and the forwarded headers are read only under `--trust-forwarded-headers`, where the rightmost `x-forwarded-for` hop is taken since the trusted proxy appends it. The cap and the window are data in the route table in src/App, the comment post is three an hour, the site submit is two a day, the site rename is ten a day, the reaction is fifty an hour, and the slug hop is a thousand a minute. A violation grows an exponential block that ends in a 24 hour ban once the strikes pile up, and the strikes decay after an hour without a fresh block. The entry table is swept of idle addresses once it crosses a cap, so a flood of fresh addresses cannot grow it without bound. A throttled request is answered with a 429, and dev mode skips the limiter so the test suite can hammer the endpoints.
- The per-site traffic metrics are kept in the site_metrics table, one row per site, written through Store::record_click and Store::record_hop and read back busiest first through Store::get_site_metrics. A click is the outbound follow from a carousel card, recorded through the `/api/v1/sites/click` endpoint, and a hop is a next, prev, or random traversal, recorded on the redirect in handle_navigation. Both are recorded only when the server runs with `--enable-metrics`, and the admin reads the tallies through the `/api/v1/admin/stats` endpoint shown above the server logs in the admin panel.
- Each site is probed by the liveness sweep, which wakes every 60 seconds and re-probes an up site after 300 seconds and a down site after 60 seconds. The reachability and the last seen time are recorded, and the probe result is added to the current hour bucket through Store::record_liveness. Each sweep rotates the buckets through Store::rotate_liveness, so buckets older than seven days are dropped. An admin edit zeroes the last seen time through Store::schedule_recheck, so the next sweep re-probes the site at once, and the request thread signals the sweep through the shared database rather than shared memory.
- A site that fails the probe is marked down rather than removed. An admin removal is a soft delete that sets the is_deleted column, so the row and its history survive.

## Logging

The logger lives in src/Trace.hpp. A message is checked against a verbosity level
and written to the active sink.

- A printf-style line is printed by `LOG(level, ...)`, and the named variables are dumped by `LOG_VARS(level, ...)`.
- The levels are Nothing, Info, Debug, and All, and a line is printed when its level is at or below LOGGER_VERBOSITY.
- A line carries the `[HH:MM:SS]` timestamp, the severity, the file and line, and the function.
- The logger is kept in every build, since a running server is traced at runtime.
- The sink is standard error by default. `set_log_file` returns an ErrorOr and routes the log to a file opened for append, selected through the `-L` flag.
- A response is traced with a `METHOD URI -> STATUS` access line through App::emit, which funnels the JSON, text, redirect, and message replies. A GET is a read and is traced at the All level, while a POST or a DELETE is a write and is traced at Debug. The login, logout, and successful-login redirects reply directly and are not traced through that line.
- Every response carries a set of security headers filled by fill_response_headers, the content-type-options, the frame-options, the referrer-policy, the permissions-policy, and the HSTS, with a no-store cache-control added to a JSON reply. The helper is funneled through App::emit and is also called on the three auth redirects that bypass emit.
- An outbound request shares the User-Agent and Accept setup through HttpRequestBuilder::add_auxiliary_headers, used by the GitHub OAuth fetches under the wr-webring agent and by the liveness probe under a browser agent.
- A store mutation is logged as it runs, the site upsert, rename, soft delete, and reachability, the account upsert, the session open and close, and the pending action record and status change.
- Every emitted line is also retained in a 256 line ring buffer in src/Trace, guarded by a Mutex, and the admin panel reads the tail through the `/api/v1/admin/logs` endpoint, so the recent runtime trace is visible with no log file on disk.

## Concurrency

The event loop and the liveness worker are isolated by connection rather than
guarded by a shared lock.

- The event loop and the liveness sweep each open a separate sqlite connection, so no in-process state is shared between the threads.
- The write-ahead log journal mode lets a read on one connection run while the other connection writes, and a 5000 ms busy timeout absorbs contention, both set in Sqlite::open.
- The worker runs on a Thread, and its only synchronization with the main thread is a bool stop flag in src/Liveness read and written through the atomic builtins, set by Liveness::stop and polled in the sweep loop.
- The threading primitives are a generic interface in src/Thread, the abstract Mutex, the ScopedLock guard, and the abstract Thread, and pthread is named only in the PthreadMutex and PthreadThread backend in src/Pthread, modeled on the HTTP and SQL backend pattern.
- After the server loop returns, the worker thread is joined and the connections are closed on the way out.

## Frontend

- The dynamic engine is Preact, and the built bundle is embedded into the binary and served as a static asset.
- The boundary between the server and the frontend is the JSON API, and no HTML beyond the bootstrap shell is rendered by the server.
- The user panel and the admin panel are Preact views behind the session cookie.
- The panel rows draw a seven-day uptime graph from the uptime array, an ascii bar per column colored by the hour reachability. The carousel card shows how long a site has been in the ring from its created_at. A site owner, a pending submitter, a comment author, and an audit log actor are linked to their github or telegram profile from the oauth and the handle carried in the JSON.
- A carousel card carries an emoji reaction bar, the open-source Microsoft Fluent Emoji served as images from web/public/emoji. A signed-in visitor toggles a reaction through /api/v1/sites/react, and the public listing carries the per-emoji counts.
- The footer carries a comments section. The approved comments are read by anyone through /api/v1/comments, paged by an offset and a limit, and an owner of a site in the ring posts through /api/v1/comments/add. A comment may mention a site by its slug with an at sign, and the mention is linked to that site when the slug names a real one. The comment carries the author provider and handle as author_oauth and author_tag, stored on the row as the source and the handle, so the author name links to the github or telegram profile. A comment by a user is held for approval and a comment by an admin is approved at once, and a body that carries a blacklisted word is refused by the swear filter in src/Utils. An admin reads the queue through /api/v1/admin/comments, approves a comment through /api/v1/admin/comments/approve, and deletes one through /api/v1/admin/comments/delete, and each approval and deletion is recorded in the audit trail.
- The frontend source and its build live under the web directory.

## Deploy

- An ansible setup is held in the deploy directory, with ansible.cfg, inventory, playbooks, and files.
- The static release binary is built locally and shipped to the inventory hosts by `make deploy`, which builds through `make MODE=rel` and then runs the playbook.
- ../wr is copied to /usr/local/bin/wr by the playbook, the unit is installed, and the service is started.
- The service is run as a dedicated system user under a system unit in /etc/systemd/system, with `Restart=always` and `WantedBy=multi-user.target`.
- The secrets are the GitHub OAuth client id and secret, the Telegram bot token, and the session key, and they are held in an ansible vault under group_vars.
- The vault secrets are rendered into an environment file that the unit reads, and no secret is committed in plaintext.

## Commits

The repository follows conventional commits, lowercase and granular, one logical
change per commit.

- The subject is `type: imperative summary` with no trailing period.
- The type is one of `feat`, `fix`, `chore`, `docs`, `refactor`, `test`, or `build`.
- A feature is added by `feat`, a bug is repaired by `fix`, vendoring and tooling are covered by `chore`, and the docs are covered by `docs`.
- A body is added only when the why is not obvious from the subject.

## Testing

- The debug binary is built and the suites are run against the goldens under test/expected by `make -C test test`.
- The goldens are regenerated by the refill target, and each regenerated golden is read before it is trusted.
- A server launch is wrapped in a timeout, so a blocking accept never freezes the run.

## Finishing a change

- This CLAUDE.md and the README are updated before a plan is finished.
- A new endpoint, a new auth provider, a new store table, or a renamed flag touches the architecture notes here, and a new or changed endpoint touches the API spec in openapi.yaml.
- A deploy change touches the playbook and the unit template under deploy.
