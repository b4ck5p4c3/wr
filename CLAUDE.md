# wr project notes

wr is a C++ and C webring backend. The server serves a dynamic page that lists
the member sites, a user panel, an admin panel, GitHub and Telegram auth, and a
periodic liveness check. The code style and the build skeleton are reused from
the shit shell, and the deploy layout is reused from the fennec.support ansible
setup.

## Build

- `make MODE=rel` writes the static binary to ../wr.
- `make MODE=dbg` writes ../wr-dbg with AddressSanitizer and UndefinedBehaviorSanitizer, and dbg is the default.
- `make MODE=cov` writes ../wr-cov with coverage instrumentation.
- `make MODE=cosmo` and `make MODE=cosmo_dbg` write the portable ../wr-cosmo.com and ../wr-cosmo_dbg.com through the cosmopolitan toolchain.
- The dbg, rel, and cov modes build exceptionless with -fno-exceptions, while the cosmo modes build with -fexceptions, since the cosmopolitan toolchain requires it. The source stays exception-free in every mode.
- The release build keeps UndefinedBehaviorSanitizer on and compiles at -O2.
- A bare `make` builds the wr target, since the object tree under src/o/$(MODE) is created as an order-only prerequisite.
- The toolchain is clang and clang++ at -std=c++23, and the linker is mold or lld when present.
- `make fmt` and `make tidy` run clang-format and clang-tidy, and `make clean` removes the artifacts.
- `./install-hooks.sh` symlinks the pre-commit hook so `make fmt` runs before every commit.

## Vendoring

- mongoose is a submodule at vendor/mongoose and it drives all server work.
- curl is a submodule at vendor/curl and it drives all outbound work.
- sqlite is the amalgamation at vendor/sqlite and it is the only store.
- The vendored mongoose and sqlite C sources are compiled into the same object tree under o/$(MODE)/vendor.
- A dependency upgrade waits for approval.

## Code conventions

- The macro layer is carried over from shit through src/Common.hpp.
- The container and the allocator foundation is ported from shit, src/String, src/StringView, src/ArrayList, src/Maybe, src/Allocator, and src/Debug, all under namespace wr.
- src/Cli is the flag parser ported from shit, and src/Errors holds ErrorBase, Error, and Warning.
- The build is exceptionless. `-fno-exceptions` is set, so a fallible function returns `ErrorOr<T>` from src/ErrorOr.hpp rather than throwing, and the `TRY` macro propagates an error early.
- An unrecoverable allocation failure aborts, since a constructor cannot return an ErrorOr.
- A raw pointer to untyped memory is written `opaque *`, the alias for `void *` defined in src/Common.hpp.
- Functions use the `fn name(...) -> ret` form, and a function that never unwinds is marked `noexcept`. The `throws` and `wontthrow` macros are retired, since the build is exceptionless.
- Comments document why the code is the way it is, and the block rationale comments of the shit port are dropped while the inline comments are kept.
- Locals use `let` and `let const`, so a deducible type is never spelled out.
- The integer aliases are `usize`, `u8` through `u64`, and `i8` through `i64`.
- A null pointer check reads `!= nullptr` or `== nullptr`, never a bare truthiness test.
- A boolean reads `is_`, `should_`, `was_`, `did_`, or `has_`, and a number carries a `_count` or a measure suffix.
- A variable-bound lambda is named `do_`, and an accessor reads `get_` or `set_`.
- An if whose condition has `&&` or `||` is braced, while a single-condition if stays unbraced.
- A chain of three or more name comparisons becomes a `consteval StaticStringMap` rather than an if ladder.
- Stray enums and structs are lower_snake_case, and only a class and a nested type are CamelCase.
- File operations take a Path, not a String or a StringView.
- State threads through context structs and constructors, and no mutable global holds per-request state.
- An existing helper is reused rather than a second copy written, and a new abstraction waits for approval.

## Architecture

- src/Main.cpp opens the database, builds the mongoose event manager, registers the liveness timer, and runs the loop.
- The HTTP layer routes a request to the page renderer, the JSON API, the auth endpoints, or the static asset handler.
- The store owns the sqlite connection, the schema migration, and the prepared statements.
- The outbound layer wraps curl for the liveness probes and the OAuth token exchange.
- The data model holds the member sites, the panel users, the panel admins, and the sessions.
- A user owns sites through the user panel, and an admin approves, removes, and manages users through the admin panel.
- Auth runs through GitHub OAuth and the Telegram login widget, and a login opens a session row and sets the session cookie.
- The liveness sweep probes each site on a periodic timer and records the reachability and the last seen time.
- A site that fails the probe is marked down rather than removed.

## Logging

The logger lives in src/Trace.hpp. The structure is taken from the shit shell,
while the leading wall-clock timestamp is taken from the zest server.

- `LOG(level, ...)` prints a printf-style line, and `LOG_VARS(level, ...)` dumps named variables.
- The levels are Nothing, Info, Debug, and All, and a line prints when its level is at or below LOGGER_VERBOSITY.
- A line carries the `[HH:MM:SS]` timestamp, the severity, the file and line, and the function.
- The logger stays compiled in every build, since the server traces at runtime. The shit logger compiles out in release.
- The sink is standard error by default. `set_log_file` returns an ErrorOr and routes the log to a file opened for append, selected through the `-L` flag.

## Concurrency

The concurrency model is reused from the burner sync-map pattern, with the state
shared between the mongoose event loop and the liveness worker thread guarded by
a synchronized map.

- A synchronized map guards the shared state behind a lock, and the underlying map is private.
- The hot read path, the page render and the routing lookup, takes a read lock through a shared mutex.
- One coarse lock covers the related maps that mutate together, so a paired update stays atomic.
- A reader takes a snapshot under the lock and uses it after the lock is released, so the critical section stays short.
- The cleanup runs on a disconnect or a removal event, and no sweeper thread or TTL is used.
- An accumulator that aggregates stats across reconnections is never destroyed for the lifetime of the server.

## Frontend

- The dynamic engine is Preact, and the built bundle is served by mongoose as a static asset.
- The boundary between the server and the frontend is the JSON API, and the server renders no HTML beyond the bootstrap shell.
- The user panel and the admin panel are Preact views behind the session cookie.
- The frontend source and its build live under the web directory.

## Deploy

- The deploy directory holds an ansible setup at deploy/ansible, with ansible.cfg, inventory, playbooks, and files.
- The playbook builds the release binary through `make MODE=rel`, copies ../wr to the server, installs the unit, and starts the service.
- The service runs as a dedicated system user under a system unit in /etc/systemd/system, with `Restart=always` and `WantedBy=multi-user.target`.
- The secrets are the GitHub OAuth client id and secret, the Telegram bot token, and the session key, and they live in an ansible vault under group_vars.
- The vault secrets are rendered into an environment file that the unit reads, and no secret is committed in plaintext.

## Commits

The repository follows conventional commits, lowercase and granular, one logical
change per commit.

- The subject is `type: imperative summary` with no trailing period, and the existing log shows `chore: vendor curl 8.20.0` and `chore: setup repo skeleton`.
- The type is one of `feat`, `fix`, `chore`, `docs`, `refactor`, `test`, or `build`.
- `feat` adds a feature, `fix` repairs a bug, `chore` covers vendoring and tooling, and `docs` covers the docs.
- A body is added only when the why is not obvious from the subject.

## Testing

- `make -C test test` builds the debug binary and runs the suites against the goldens under test/expected.
- The refill target regenerates the goldens, and each regenerated golden is read before it is trusted.
- A server launch is wrapped in a timeout so a blocking accept never freezes the run.

## Finishing a change

- This CLAUDE.md and the README are updated before a plan is finished.
- A new endpoint, a new auth provider, a new store table, or a renamed flag touches the architecture notes here.
- A deploy change touches the playbook and the unit template under deploy/ansible.
