# wr

A cute and overcomplicated self-contained webring backend.
See [original issue](https://t.me/b4cksp4ce_issues/762).

## Quickstart

Only Linux is supported.

Build from source:
```bash
$ cd web
$ bun install
$ cd ..
$ export MODE=rel
$ make web wr
$ stat wr
```
This produces completely static binary with [`web/`](./web) assets embeded
inside.

To run:
```bash
$ WR_GITHUB_CLIENT_ID=<...> \
  WR_GITHUB_CLIENT_SECRET=<...> \
  WR_TELEGRAM_BOT_TOKEN=<...> \
  ./wr --listen-address 'http://0.0.0.0:8000' \
     --database-url='wr.db' \
     --database-backend='sqlite' \
     --web-root-url 'https://your.ring'
```

Or use docker to build and image and run it:
```bash
$ cp ./.env.example ./.env
$ nvim ./.env # include your shit
$ docker compose up --build
```

Only `--listen-address` and `--database-url` are required. The `--web-root-url`
defaults to the listen URL when it is not given. Each of these flags can also
read from the environment. See `--help` output for more information.

## Secrets

The secrets are read from the environment. Outside dev mode at least one login
provider and a session key are required. For a local run without any provider,
pass `--enable-dangerous-developer-environment`, which exposes a login bypass.

**WR_SESSION_KEY**

The session cookie is signed with this key. A 32 byte hex value is generated
by:
```
openssl rand -hex 32
```

**WR_GITHUB_CLIENT_ID** and **WR_GITHUB_CLIENT_SECRET**

Enable the GitHub login. A GitHub OAuth app is registered under GitHub
Settings, Developer settings, OAuth Apps, New OAuth App. The authorization
callback URL is the public url of the ring followed by `/auth/github/callback`,
for example `https://your.ring/auth/github/callback`.

**WR_TELEGRAM_BOT_TOKEN**

Enable the Telegram login. A bot is created by messaging
[@BotFather](https://t.me/BotFather) with `/newbot`, which returns the token.
The widget is bound to a domain, so the bot domain is set by sending
`/setdomain` to @BotFather and giving the public url of the ring. The numeric
bot id before the colon in the token is read by the widget.

## Usage

You sign in first through GitHub or Telegram. Admins are seeded by hand by
making `is_admin` equal to 1 their account row. The accounts table is keyed by
two columns, the provider source as an integer and the handle.

```
sqlite3 wr.db "UPDATE accounts SET is_admin = 1 \
  WHERE name = 'your-handle' AND source = 0;"
```

Other people then sign in and submit their site, which is held for review. An
admin approves a pending site by hand before it joins the ring. Only an owner of
an approved site in the ring may post a footer comment, while every signed in
visitor may leave an emoji reaction.

## API

The internal JSON API is described in [openapi.yaml](openapi.yaml) or `/docs`
route of the running server.

## TODO

- A cute widget;
- An 88x31 button store.
