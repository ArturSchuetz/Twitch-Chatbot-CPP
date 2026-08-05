# Testing

## Local verification

Windows:

```powershell
$env:VCPKG_ROOT = "C:\vcpkg"
cmake --preset windows-vcpkg -DTWITCHBOT_WARNINGS_AS_ERRORS=ON
cmake --build --preset windows-release --parallel
ctest --preset windows-release
```

Linux:

```bash
cmake --preset linux-system -DTWITCHBOT_WARNINGS_AS_ERRORS=ON
cmake --build --preset linux-release --parallel
ctest --preset linux-release
```

## Covered production behavior

- IRC tags, prefixes, commands, trailing parameters, empty values, and tag escapes;
- fragmented and coalesced TCP input plus bounded incomplete frames;
- malformed input and 1,000 deterministic random byte sequences;
- exact CRLF/PONG output, CR/LF injection, and maximum output length;
- login/channel/token normalization, token/PASS length boundaries, placeholders,
  deduplication, and invalid JSON;
- authentication success/failure, partial/full CAP ACK, CAP NAK, Ready gating, and
  Twitch `RECONNECT`;
- exact sender/channel matching and false-positive substring cases;
- rule cooldown/delay and independent unequal-size subscription/gift rotation;
- global, per-channel, and JOIN rate-limit boundaries. Limiter state is tested
  directly; its lifetime across replacement sessions is verified by code review.

CI is configured to build with current MSVC on Windows and both GCC and Clang on
Ubuntu, with warnings treated as errors. A separate release-safety job is configured
to run Python regression tests and reject credential assignments, arbitrary binary
content, and tracked or untracked private/generated artifacts. Another job is
configured to build the Docker image and run its offline configuration check.

## Deliberate integration boundary

The unit suite does not contact Twitch or use real credentials. Before deployment,
`--check-config` validates local settings without connecting. A live smoke test
should use a freshly generated, least-privilege token and a test channel, then
revoke that token if logs or terminal capture may have exposed it.

The TLS socket orchestration itself is not exercised by a local fake server; TLS,
SNI, hostname checks, timeouts, cancellation, and reconnect ownership therefore
still require integration coverage if active development resumes.
