# wr

A small webring backend in C++ and C. The server lists the member sites, serves
a user panel and an admin panel behind a session, signs people in through GitHub
and Telegram, and runs a periodic liveness check over the ring. The frontend is
a Preact bundle, and the boundary between the server and the frontend is a JSON
API.

[original issue](https://t.me/b4cksp4ce_issues/762)

## Build

The server builds with clang at C++23.

```
make MODE=dbg    # ../wr-dbg, with the sanitizers, the default
make MODE=rel    # ../wr, the static release
make MODE=cov    # ../wr-cov, with coverage
```

The frontend builds under the web directory.

```
cd web
bun install
bun run build    # writes web/dist
```

## Run

```
./wr --listen http://0.0.0.0:8000 --database wr.db --web-root web/dist \
     --public-url https://your.ring
```

The OAuth secrets and the session key are read from the environment, so they are
never passed on the command line.

```
WR_GITHUB_CLIENT_ID, WR_GITHUB_CLIENT_SECRET, WR_TELEGRAM_BOT_TOKEN, WR_SESSION_KEY
```

An admin is seeded by setting `is_admin` on an account row once it has signed in
at least once.

## API

The public navigation API is documented in arch.md, alongside the architecture
and the data model. The endpoints are GET /sites and the per-slug navigation
routes /{slug}, /{slug}/data, /{slug}/next, /{slug}/prev, and /{slug}/random,
each with a /data variant.

## Layout

The architecture, the diagrams, and the data model are in arch.md. The code
conventions and the build detail are in CLAUDE.md.
