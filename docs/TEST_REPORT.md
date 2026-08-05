# Test report

Date: 2026-08-05

## Verified locally

- CMake configured successfully with the pinned vcpkg manifest.
- Dependencies resolved to Boost.Beast 1.89.0, nlohmann-json 3.12.0, and OpenSSL
  3.6.0 in the local manifest installation.
- The Visual Studio 2022 x64 Release build completed with MSVC 19.44.
- A separate Visual Studio 2022 x64 Release build completed with ClangCL 19.1.5.
- `TWITCHBOT_WARNINGS_AS_ERRORS=ON` completed without a compiler warning.
- CTest passed the production-core test executable under both MSVC and ClangCL.
- `clang-tidy` completed over every project translation unit; targeted follow-up
  runs confirmed zero project-code diagnostics after corrections.
- Two Python regression tests passed for the release-safety scanner, including
  force-added `build/bin/obj` content and an extensionless binary fixture.
- `twitch-chatbot --check-config` accepted normalized dummy credentials and two
  channels without contacting Twitch.
- A live Twitch IRC smoke test established TLS, completed authentication and
  capability negotiation, and remained connected beyond the 20-second auth
  timeout. Automations were disabled and no chat message was sent.
- The local vcpkg OpenSSL build required `SSL_CERT_FILE` on Windows; the repeated
  live test succeeded with the CA bundle supplied by Git for Windows. Certificate
  verification remained enabled throughout.

## Test executable coverage

The test executable contains seven named groups and multiple assertions covering
the production IRC parser/tag decoder, TCP stream assembler, output safety,
configuration validation, authentication/capability state machine, reconstructed
automation, and Twitch rate limits. It also feeds 1,000 deterministic random byte
sequences into the production parser and verifies false-positive username cases.
The current source has 57 direct check call sites and eight exception-boundary call
sites. Token/PASS boundaries and component-level limiter windows are included; the
production client's cross-session limiter ownership is verified by code review
rather than a network-session test.

## Not verified locally

- Docker was not available on this Windows host, so the Docker image was not built
  locally.
- Linux GCC and Clang were not available on this Windows host. GitHub Actions
  defines both Linux builds in addition to the MSVC build; ClangCL was verified
  locally.
- No local fake-TLS integration server exercised socket handshake, certificate,
  timeout, cancellation, or reconnect orchestration; those paths remain covered by
  code review and the compiler/static-analysis passes only.

These boundaries are intentional; they must not be described as successful tests
until the corresponding CI or deployment smoke test has actually run.
