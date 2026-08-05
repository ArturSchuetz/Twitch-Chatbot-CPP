# Deployment

## Pre-deployment checklist

1. Revoke the OAuth tokens found in the historical C++ source/configuration.
2. Build only this clean folder; never package old `bin/`, `obj/`, logs, or config.
3. Copy `.env.example` to a secret location and replace every placeholder.
4. Run `twitch-chatbot --check-config`.
5. Run the full CMake build and CTest suite.
6. Confirm the deployment host has an up-to-date CA trust store.

Twitch credentials require the IRC scopes `chat:read` and `chat:edit`. Twitch's
current endpoints and authentication sequence are documented in
[IRC Concepts](https://dev.twitch.tv/docs/chat/irc/).

## Docker

```bash
docker build -t twitch-chatbot-cpp:local .
docker run --rm --name twitch-chatbot \
  --env-file /secure/path/twitch-chatbot.env \
  twitch-chatbot-cpp:local
```

The image runs as the unprivileged `twitchbot` user. It contains the public
`config/bot.json` but no `.env`. To use another behavior file, bind-mount it
read-only and replace the entrypoint argument:

```bash
docker run --rm --env-file /secure/path/twitch-chatbot.env \
  --mount type=bind,src=/secure/path/bot.json,dst=/config/bot.json,readonly \
  twitch-chatbot-cpp:local --config /config/bot.json
```

## systemd example

Install the binary and public JSON under `/opt/twitch-chatbot-cpp`, then store
credentials in `/etc/twitch-chatbot-cpp.env` with permissions readable only by a
dedicated service account.

```ini
[Unit]
Description=Twitch Chatbot C++
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=twitchbot
Group=twitchbot
WorkingDirectory=/opt/twitch-chatbot-cpp
EnvironmentFile=/etc/twitch-chatbot-cpp.env
ExecStart=/opt/twitch-chatbot-cpp/twitch-chatbot --config /opt/twitch-chatbot-cpp/config/bot.json
Restart=on-failure
RestartSec=5
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true

[Install]
WantedBy=multi-user.target
```

The client handles Twitch `RECONNECT` and transient network failures internally.
The service manager remains useful for process crashes, host restarts, and fatal
dependency failures. Invalid credentials deliberately produce a nonzero exit.

## Operational privacy

Leave `TWITCH_LOG_RAW_IRC` disabled. If it is temporarily enabled, send output to
a bounded log sink with restricted access and retention because raw lines contain
usernames, IDs, tags, and chat text.
