# Security policy

## Supported version

This is an archive-oriented reference project. Security fixes may be accepted,
but no response time is guaranteed. Use the latest commit and rebuild all
dependencies from the pinned vcpkg manifest.

## Credentials

- Store Twitch credentials only in `.env` or the deployment platform's secret
  store. `.env` is ignored by Git.
- Never add a token to `config/bot.json`, command-line arguments, logs, images,
  binaries, crash dumps, or issue reports.
- Revoke a token immediately if it is exposed.
- The bot rejects placeholder tokens and CR/LF characters in all credentials and
  outgoing IRC values.

## Transport and logs

The client connects only to `irc.chat.twitch.tv:6697` using TLS 1.2 or newer,
SNI, peer verification, and hostname verification. Verification failures stop
the connection; there is no insecure fallback.

Raw IRC logging is off by default because IRC frames contain usernames, tags,
IDs, and chat content. Enable `TWITCH_LOG_RAW_IRC=true` only temporarily and
handle the terminal output as personal data.

## Reporting

Please report vulnerabilities privately to the repository owner. Do not include
real tokens or private chat logs in a report.
