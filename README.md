# wr

A small, cute and simple webring backend.
See [original issue](https://t.me/b4cksp4ce_issues/762).

## Quickstart

```
cd web
bun run install
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

An admin is seeded by setting `is_admin` on an account row inside the database
once it has signed in at least once.

## API

See [openapi.yaml](openapi.yaml).
