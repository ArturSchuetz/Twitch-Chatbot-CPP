# Twitch Chatbot C++

A secure C++20 reconstruction of an old Twitch IRC chatbot. It preserves the
original project's configurable channels, message-trigger rules, subscription
and gift-response rotation, delays, and cooldowns without carrying over its
credentials, generated binaries, obsolete dependencies, or unsafe socket and
thread code.

This is an archive-oriented reference project. Fork it if you want to extend it.
For new, full-featured bots, Twitch recommends EventSub plus the Send Chat Message
API; this smaller project deliberately uses Twitch's supported TLS IRC interface.
See [Twitch Chat & Chatbots](https://dev.twitch.tv/docs/chat/).

## Features

- TLS 1.2+ connection to `irc.chat.twitch.tv:6697` with SNI, certificate-chain,
  and hostname verification;
- explicit authentication and capability-negotiation states;
- IRCv3 tag decoding and bounded, fragmentation-safe IRC stream parsing;
- exact PING/PONG payload handling and CRLF-safe output encoding;
- automatic reconnect with exponential backoff and Twitch `RECONNECT` support;
- rate-limited JOINs and chat output using Twitch's regular-account limits;
- optional exact-sender message rules;
- optional subscription and gift hype messages with separate round-robin lists,
  steady-clock cooldowns, and non-blocking delays;
- validated environment and JSON configuration with no secret persistence;
- CTest coverage, MSVC/GCC/Clang CI, release-safety regression tests, and a
  non-root Docker image configured to be built and validated in CI.

The architecture is described in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Requirements

- CMake 3.24 or newer
- a C++20 compiler
- Boost.Beast / Boost.Asio 1.74 or newer
- OpenSSL
- JSON for Modern C++
- a Twitch user access token with `chat:read` and `chat:edit` scopes for IRC

Windows users can resolve dependencies from the pinned `vcpkg.json` manifest.
Linux users may use system packages as shown below. Twitch documents IRC
authentication and the TLS endpoints in
[IRC Concepts](https://dev.twitch.tv/docs/chat/irc/).

## Quick start on Windows

Install Visual Studio 2022, CMake, Git, and vcpkg. Then:

```powershell
git clone <your-repository-url>
cd Twitch-Chatbot-CPP

$env:VCPKG_ROOT = "C:\vcpkg"
cmake --preset windows-vcpkg
cmake --build --preset windows-release --parallel
ctest --preset windows-release

Copy-Item .env.example .env
```

Edit `.env`, validate it without connecting, then start the bot:

```powershell
./build/windows-vcpkg/Release/twitch-chatbot.exe --check-config
./build/windows-vcpkg/Release/twitch-chatbot.exe
```

If your OpenSSL installation has no default CA location, set `SSL_CERT_FILE` to a
trusted CA bundle. Never disable certificate verification.

For a standard Git for Windows installation, a typical command is:

```powershell
$env:SSL_CERT_FILE = "C:\Program Files\Git\mingw64\etc\ssl\certs\ca-bundle.crt"
```

## Quick start on Ubuntu

```bash
sudo apt-get update
sudo apt-get install -y cmake g++ libboost-all-dev libssl-dev nlohmann-json3-dev

cmake --preset linux-system
cmake --build --preset linux-release --parallel
ctest --preset linux-release

cp .env.example .env
./build/linux-system/twitch-chatbot --check-config
./build/linux-system/twitch-chatbot
```

## Environment configuration

`.env` is ignored. `.env.example` documents every supported variable:

```dotenv
TWITCH_BOT_USERNAME=your_bot_account
TWITCH_OAUTH_TOKEN=replace_me
TWITCH_CHANNELS=your_channel,another_channel
```

The token may be supplied with or without the `oauth:` prefix. The bot rejects
placeholder values, invalid logins, empty channels, more than 100 channels, and
CR/LF injection. Environment variables override matching `.env` entries.

The historical C++ folders contain old OAuth material in source, config, and
compiled artifacts. Do not publish those folders, and revoke those tokens before
publishing any legacy history. See [`SECURITY.md`](SECURITY.md).

## Behavior configuration

Public behavior lives in [`config/bot.json`](config/bot.json); it never contains
credentials. The two historical game rules and the hype settings are preserved
as disabled examples. Change `enabled` to `true` only after reviewing the sender,
channel, responses, delays, and cooldowns.

Message rules use an exact normalized sender and optional exact channel. Their
`contains` match applies only to the message text:

```json
{
  "enabled": true,
  "sender": "trusted_sender",
  "channel": "your_channel",
  "contains": "trigger text",
  "response": "response text",
  "delaySeconds": 0,
  "cooldownSeconds": 5
}
```

Hype responses listen for Twitch `USERNOTICE` events with `msg-id` values `sub`,
`resub`, `subgift`, or `submysterygift`. Subscription and gift lists rotate
independently. Enabled hype requires a channel and two non-empty message lists.

## Docker

Build and run without copying `.env` into the image:

```powershell
docker build -t twitch-chatbot-cpp .
docker run --rm --env-file .env twitch-chatbot-cpp
```

For service-manager and container guidance, see
[`docs/DEPLOYMENT.md`](docs/DEPLOYMENT.md).

## Verification

```powershell
cmake --build --preset windows-release --parallel
ctest --preset windows-release
```

The tests import and exercise the production parser, stream assembler,
configuration loader, auth/capability state machine, automation engine, output
encoder, and rate limiter. Details are in [`docs/TESTING.md`](docs/TESTING.md),
and the latest local results are recorded in
[`docs/TEST_REPORT.md`](docs/TEST_REPORT.md).

## Review and provenance

The source requirements came from the `GeBot` CMake worktree and the more
feature-rich `GeGeGeSchuetz/src/Appilcations/GeBot` snapshot. A reviewer and an
independent validator completed a 22-finding review; validation reached 10/10
before this implementation began. The complete publication-oriented summary and
fix mapping are in [`docs/LEGACY_REVIEW.md`](docs/LEGACY_REVIEW.md).

No old `.git` history, token, user config, log, binary, object file, vendored cURL,
vendored JSON tree, LGPL IRC client, or Win32 thread wrapper is included.

## License

The newly written project code is available under the [MIT License](LICENSE).
Dependency licenses are listed in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

Read [`docs/PUBLISHING.md`](docs/PUBLISHING.md) before the first public push.
