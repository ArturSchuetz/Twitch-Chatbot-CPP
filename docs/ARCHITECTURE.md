# Architecture

## Boundaries

The project separates pure protocol and behavior logic from network I/O:

1. `config.cpp` loads `.env` plus process environment and validates public JSON
   behavior settings. It never writes configuration or secrets.
2. `irc.cpp` assembles bounded CRLF frames, parses IRC/IRCv3 data, decodes tags,
   and encodes injection-safe outgoing lines.
3. `session_protocol.cpp` owns authentication, capability negotiation, PING/PONG,
   Ready, fatal-login, and reconnect state transitions.
4. `automation.cpp` turns matching `PRIVMSG` and `USERNOTICE` events into
   value-owned scheduled chat messages.
5. `rate_limiter.cpp` applies global chat, per-channel chat, and JOIN pacing.
6. `twitch_irc_client.cpp` is the only Boost/OpenSSL boundary. It owns asynchronous
   DNS, TCP, TLS, reads, writes, timers, signals, and reconnect scheduling.

The CTest executable imports the same production library used by the bot; it does
not reimplement parser or business behavior.

## Session lifecycle

```text
Disconnected
    -> TCP/TLS connected
    -> PASS + NICK + CAP REQ
    -> authentication response and CAP ACK
    -> Ready
    -> paced JOINs and event processing
    -> Reconnect / network failure / Stop
```

No channel JOIN is queued until authentication and all three required Twitch
capabilities (`membership`, `tags`, and `commands`) are acknowledged. A CAP NAK,
login-failure NOTICE, or authentication timeout is fatal; ordinary network
failures reconnect with bounded exponential backoff and jitter.

## Concurrency and ownership

All socket reads, writes, timers, automation callbacks, and queue operations run
on one Boost.Asio `io_context`. The process uses no manual heap allocation,
platform thread API, detached worker, or shared mutable socket access. Delayed
messages are ordinary values with `steady_clock` due times in the single-writer
queue.

## Limits and backpressure

The output queue is bounded at 1,000 entries. Chat messages are dropped when it is
full; exhaustion by protocol messages ends the session and reconnects instead of
growing memory indefinitely. Runtime configuration is limited to 100 unique
channels.

Default pacing follows Twitch's documented regular-account limits: 20 chat
messages per 30 seconds, one message per second per channel, and 20 JOIN attempts
per 10 seconds. See [Twitch Chat & Chatbots](https://dev.twitch.tv/docs/chat/).

## Security boundary

TLS uses the system trust roots, SNI, peer verification, hostname verification,
and TLS 1.2 or newer. There is no plaintext or verification-disabled fallback.
Outgoing IRC data is limited to 510 payload bytes plus CRLF and rejects embedded
CR or LF.
