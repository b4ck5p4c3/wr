# wr

A small, cute and simple webring backend.
See [original issue](https://t.me/b4cksp4ce_issues/762).

## Quickstart

```
cd web
bun install
export MODE=dbg/rel/cov
make web
make
```

then run:
```
./wr --listen http://0.0.0.0:8000 --database wr.db --web-root web/dist \
     --public-url https://your.ring
```

Environment:
```
WR_GITHUB_CLIENT_ID, WR_GITHUB_CLIENT_SECRET, WR_TELEGRAM_BOT_TOKEN, WR_SESSION_KEY
```

Outside dev mode the server needs at least one login provider and a session key.
For a local run without any of those, pass `--dev`, which exposes a login bypass
for an admin and a user.

An admin is seeded by setting `is_admin` on an account row inside the database
once it has signed in at least once.

## API

The internal JSON API is described in [openapi.yaml](openapi.yaml). The public
webring navigation API is documented at the `/docs` page of a running server.
