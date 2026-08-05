# Contributing

This project is primarily a published reference implementation. Small fixes,
tests, security improvements, and documentation corrections are welcome.

Before opening a pull request:

1. Build with `TWITCHBOT_WARNINGS_AS_ERRORS=ON`.
2. Run CTest with `--output-on-failure`.
3. Confirm no credentials or generated artifacts are tracked.
4. Update `TODO.md` and relevant documentation.

Do not include private Twitch data or copy code from the historical LGPL IRC
client into this MIT-licensed implementation.
