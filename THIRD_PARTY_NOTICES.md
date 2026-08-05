# Third-party notices

No third-party source code is vendored in this repository. Dependencies are
declared in `vcpkg.json` and resolved from its pinned vcpkg baseline.

## Boost.Beast / Boost.Asio

- Purpose: asynchronous TCP/TLS networking
- License: Boost Software License 1.0
- Project: <https://www.boost.org/>

## OpenSSL

- Purpose: TLS encryption and certificate verification
- License: Apache License 2.0
- Project: <https://www.openssl.org/>

## JSON for Modern C++

- Purpose: public behavior-configuration parsing
- License: MIT
- Project: <https://github.com/nlohmann/json>

The historical cURL 7.59.0, nlohmann-json 3.1.2, UTF-8 helper, GoogleTest,
IRC socket/client, and thread-wrapper copies are intentionally not included.
