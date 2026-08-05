# Legacy C++ review

Date: 2026-08-05

The review covered both historical C++ source trees, their CMake/Visual Studio
context, runtime configuration, binary/object artifacts, vendored dependencies,
licenses, and current Twitch requirements. A reviewer produced the findings; an
independent review validator requested one revision and then awarded 10/10 before
implementation began.

## Summary

| Severity | Count |
| --- | ---: |
| Critical | 3 |
| High | 11 |
| Medium | 6 |
| Low | 2 |

The historical trees are not safe publication or deployment bases. This new
folder uses their intended behavior as requirements without importing their Git
history, credentials, networking/thread code, binaries, or vendored packages.

## Findings and disposition

| # | Severity | Finding | Disposition in this project |
| ---: | --- | --- | --- |
| 1 | Critical | Two OAuth values exist in source/runtime config; one is copied into five EXE/IOBJ/OBJ artifacts. | No historical config or artifact copied; secrets come only from ignored `.env`; release scans and ignore rules added. Token revocation remains an owner action. |
| 2 | Critical | PASS transmits the token over plaintext TCP/6667. | Replaced with verified TLS 1.2+ to `irc.chat.twitch.tv:6697`, SNI, peer and hostname verification, with no insecure fallback. |
| 3 | Critical | `HeapAlloc` is used for a type containing an unconstructed `std::string`. | Manual heap/thread path removed; delayed messages are value-owned Asio queue entries. |
| 4 | High | EOF becomes a huge string length and TCP fragments are parsed as frames. | Async reads feed a bounded persistent CRLF assembler; errors are handled before parsing. |
| 5 | High | Unchecked parameters and numeric conversion allow remote process termination. | Parser returns optional/error, bounds input, and protocol/automation handlers verify arity before access. |
| 6 | High | Output uses LF, corrupts PONG, ignores partial writes, and permits false success. | Central CRLF encoder, exact PONG payload, injection/length checks, and Asio `async_write` write-all semantics. |
| 7 | High | Login/CAP have no state machine; JOIN starts before success. | Explicit auth/CAP/Ready/fatal/reconnect states; JOIN only after auth plus all required ACKs. |
| 8 | High | Hype is historically nonfunctional and has empty/wrong-vector crashes. | Reconstructed as opt-in validated behavior with non-empty enabled lists and independent indices. |
| 9 | High | No chat or JOIN rate limiting. | Global/per-channel chat limits, JOIN pacing, bounded queue, and backpressure. |
| 10 | High | `RECONNECT`, disconnect recovery, and stop are not service-safe. | Async cancelable I/O, Twitch reconnect handling, bounded exponential backoff/jitter, and Asio signals. |
| 11 | High | Socket ownership and Unix close logic are broken. | Replaced entirely by Boost.Asio/Beast RAII networking. |
| 12 | High | Win32-only APIs, mutable literal pointers, and missing includes break portability. | Clean C++20 code; MSVC/GCC/Clang CI and warnings-as-errors. |
| 13 | High | The app is untracked inside a dirty foreign CMake-template clone. | Standalone project, fresh boundary, modern CMake, presets, manifest, CTest, and CI. |
| 14 | High | LGPL provenance and obsolete vendored cURL/JSON prevent MIT-only copying. | Clean rewrite; no old code/dependency trees; MIT project license and third-party notices. |
| 15 | Medium | Config parsing is unvalidated, rewrites secrets, depends on CWD, and returns success on failure. | Validated env plus explicit config path; no writes; field errors and nonzero fatal exit. |
| 16 | Medium | Unbounded raw IRC logs retain chat and user data. | No file logger; raw inbound logging is explicit, off by default, and privacy-documented. |
| 17 | Medium | Sender substring matching is spoofable; duplicate CLEARCHAT routing is unreachable. | Exact normalized sender/channel matching; no duplicate legacy event branch. |
| 18 | Medium | Locale byte conversion is unsafe and IRCv3 tags stay escaped. | Safe ASCII command handling and dedicated IRCv3 tag decoder. |
| 19 | Medium | `volatile bool` is not a portable stop/signal mechanism. | Boost.Asio `signal_set`, cancellation, and single event loop. |
| 20 | Medium | No production bot tests exist. | CTest imports the production core and covers parser, config, state, automation, and limits. |
| 21 | Low | Unused thread-wrapper destructor calls `pthread_exit`. | Wrapper not copied. |
| 22 | Low | Cooldowns use adjustable wall-clock time. | All delay/cooldown/rate calculations use `steady_clock`. |

## Historical sources assessed

- `F:\Projects\GeBot\source\applications\gebot`
- `F:\Projects\GeGeGeSchuetz\src\Appilcations\GeBot`
- `F:\Projects\GeGeGeSchuetz\bin\Win32`
- `F:\Projects\GeGeGeSchuetz\obj\Win32`
- adjacent CMake, test, dependency, and license files

These local paths document provenance only; none is a runtime or build dependency.

## External owner action

Before publishing any historical repository or reusing an old token, revoke both
OAuth values discovered during review. This project does not contain the values
and cannot revoke account credentials on the owner's behalf.
