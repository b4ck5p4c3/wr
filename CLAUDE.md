# wr project notes

wr is a C++ webring backend. A dynamic page that lists the member sites is
served, alongside a user panel, an admin panel, GitHub and Telegram auth, and a
periodic liveness check.

## Build

- The static binary is written to ../wr by `make MODE=rel`.
- ../wr-dbg is written by `make MODE=dbg` with AddressSanitizer and UndefinedBehaviorSanitizer, and dbg is the default.
- ../wr-cov is written by `make MODE=cov` with coverage instrumentation.
- The portable ../wr-cosmo.com and ../wr-cosmo_dbg.com are written by `make MODE=cosmo` and `make MODE=cosmo_dbg` through the cosmopolitan toolchain. The cosmo modes compile the foundation and the server core, but the outbound curl layer is not compiled under the cosmopolitan toolchain yet, so the curl-dependent server links only in the dbg, rel, and cov modes.
- The frontend is built under the web directory by `bun install` and `bun run build`, or the npm equivalent, which writes web/dist. The server serves that directory through the `-w` flag.
- The dbg, rel, and cov modes are built exceptionless with -fno-exceptions, while the cosmo modes are built with -fexceptions, since the cosmopolitan toolchain requires it. The source stays exception-free in every mode.
- UndefinedBehaviorSanitizer is kept on in the release build, and the release is compiled at -O2.
- A bare `make` builds the wr target, since the object tree under src/o/$(MODE) is created as an order-only prerequisite.
- clang and clang++ at -std=c++23 are the toolchain, and mold or lld is used as the linker when present.
- clang-format and clang-tidy are run by `make fmt` and `make tidy`, and the artifacts are removed by `make clean`.
- The pre-commit hook is symlinked by `./install-hooks.sh`, so `make fmt` is run before every commit.

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
- The container and the allocator foundation is split across src/String, src/StringView, src/ArrayList, src/Maybe, src/StringMap, src/PackedStringKey, src/StaticStringMap, src/Path, src/Allocator, and src/Debug, all under namespace wr. src/StringMap is an open-addressing string-keyed hash table, and src/PackedStringKey is its probe key. src/StaticStringMap is a consteval string-keyed lookup table read by the dispatch and the content-type paths as data.
- The flag parser is src/Cli, and ErrorBase, Error, and Warning are held in src/Errors.
- The build is exceptionless. -fno-exceptions is set, so a fallible function returns ErrorOr<T> from src/ErrorOr.hpp rather than throwing, and an error is propagated early by the TRY macro.
- An unrecoverable allocation failure aborts, since a constructor cannot return an ErrorOr.
- A raw pointer to untyped memory is written `opaque *`, the alias for `void *` defined in src/Common.hpp.
- A function is written in the `fn name(...) -> ret` form, and a function that never unwinds is marked `noexcept`.
- A comment states why the code is the way it is. A block rationale comment is dropped, and an inline comment is kept.
- A local is written with `let` and `let const`, so a deducible type is never spelled out.
- The integer aliases are `usize`, `u8` through `u64`, and `i8` through `i64`.
- A null pointer check is written `!= nullptr` or `== nullptr`, never a bare truthiness test.
- A boolean is named with an `is_`, `should_`, `was_`, `did_`, or `has_` prefix, and a number carries a `_count` or a measure suffix.
- A variable-bound lambda is named `do_`, and an accessor is named with a `get_` or `set_` prefix.
- An if whose condition has `&&` or `||` is braced, while a single-condition if stays unbraced.
- A chain of three or more name comparisons is written as a `consteval StaticStringMap` rather than an if ladder.
- A stray enum or struct is lower_snake_case, and only a class and a nested type are CamelCase.
- A file operation takes a Path, not a String or a StringView.
- State is threaded through context structs and constructors, and no mutable global holds per-request state.
- An existing helper is reused rather than a second copy written, and a new abstraction waits for approval.

## Architecture

- The database is opened, the event manager is built, the liveness timer is registered, and the loop is run by src/Main.cpp.
- A request is routed by the HTTP layer to the page renderer, the JSON API, the auth endpoints, or the static asset handler.
- The HTTP layer is a generic interface over pluggable backends, modeled on the backend pattern in the oo project. The shared value types HttpMethod, HttpStatus, HttpHeaders, HttpRequest, and HttpResponse are held in src/Http. The abstract HttpClient and the HttpRequestBuilder are held in src/Client, and the abstract HttpServer with its HttpServerEvent is held in src/Server. The curl client backend CurlClient is held in src/Curl, and the mongoose server backend MongooseServer is held in src/Mongoose. A type that owns memory takes an explicit Allocator in its constructor.
- The application context App in src/App owns the dispatch and the request helpers, and it routes a request to the public navigation API, the auth endpoints, the panel API, or the static asset handler. The JSON responses are built by the writer in src/Json, and a request body or an OAuth response is parsed once into the Json value class there. A member is read through operator[] and a leaf is converted through to, backed by the rapidjson SAX reader. The GitHub and Telegram auth, the sessions, and the current-account lookup are in src/Auth. The user and admin panel handlers are in src/Panel. The liveness sweep runs on its own thread from src/Liveness with its own store connection and curl client.
- The sqlite store is owned by src/Store, which holds the connection, the migration, and the row types for sites, accounts, sessions, and pending actions.
- The Preact frontend lives under the web directory, and the JSON API is the only boundary between the server and the bundle.
- The sqlite connection, the schema migration, and the prepared statements are owned by the store.
- curl is wrapped by the outbound layer for the liveness probes and the OAuth token exchange.
- The member sites, the panel users, the panel admins, and the sessions are held in the data model.
- A user owns sites through the user panel, and an admin approves a site, removes a site, and manages the users through the admin panel.
- Authentication is run through GitHub OAuth and the Telegram login widget, and a session row is opened and the session cookie is set on a login.
- Each site is probed by the liveness sweep on a periodic timer, and the reachability and the last seen time are recorded.
- A site that fails the probe is marked down rather than removed.

## Logging

The logger lives in src/Trace.hpp. A message is checked against a verbosity level
and written to the active sink.

- A printf-style line is printed by `LOG(level, ...)`, and the named variables are dumped by `LOG_VARS(level, ...)`.
- The levels are Nothing, Info, Debug, and All, and a line is printed when its level is at or below LOGGER_VERBOSITY.
- A line carries the `[HH:MM:SS]` timestamp, the severity, the file and line, and the function.
- The logger is kept in every build, since a running server is traced at runtime.
- The sink is standard error by default. `set_log_file` returns an ErrorOr and routes the log to a file opened for append, selected through the `-L` flag.

## Concurrency

The state shared between the event loop and the liveness worker thread
is guarded by a synchronized map.

- The shared state is guarded behind a lock, and the underlying map is private.
- The hot read path, the page render and the routing lookup, takes a read lock through a shared mutex.
- One coarse lock covers the related maps that mutate together, so a paired update stays atomic.
- A snapshot is taken under the lock and used after the lock is released, so the critical section stays short.
- The cleanup is run on a disconnect or a removal event, and no sweeper thread or TTL is used.
- An accumulator that aggregates stats across reconnections is never destroyed for the lifetime of the server.

## Frontend

- The dynamic engine is Preact, and the built bundle is served as a static asset.
- The boundary between the server and the frontend is the JSON API, and no HTML beyond the bootstrap shell is rendered by the server.
- The user panel and the admin panel are Preact views behind the session cookie.
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
