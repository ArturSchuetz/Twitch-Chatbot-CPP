# TODO

## Next session

- [ ] Revoke every Twitch OAuth token found in the historical C++ source or
      configuration before publishing any legacy repository.
- [ ] Push this clean project, verify all four CI areas and rendered documentation,
      then optionally archive it.

## Open

- [ ] Configure real Twitch credentials only in the ignored local `.env` file.

## Ideas / later

- [ ] If active development resumes, add a local fake-TLS IRC integration suite
      for certificate, timeout, cancellation, write, and reconnect orchestration.
- [ ] Add automated dependency-vulnerability scanning and SBOM publication to CI
      if this archive returns to active maintenance.
- [ ] If active development resumes, evaluate EventSub plus the Send Chat Message
      API while preserving the transport-independent parser and event engine.

## Done

- [x] Complete a live TLS/authentication Twitch IRC smoke test without enabling
      automations or sending a chat message.
- [x] Create the empty GitHub repository and prepare the local initial commit.
- [x] Run the complete pre-push build, test, license, credential, artifact, and
      committed-whitespace audit.
- [x] Pass the final implementation reviewer/validator loop with zero findings and
      an independent 10/10 validation.
- [x] Complete the local pre-commit build, test, static-analysis, documentation,
      credential, artifact, and license audit.
- [x] Complete the deep-review reviewer/validator loop with a 10/10 approval and
      record all 22 findings plus their disposition.
- [x] Implement the reviewed C++20 replacement with secure Twitch IRC transport.
- [x] Add production-component tests, documentation, CI, and container deployment
      files.
- [x] Locate and compare both historical C++ GeBot implementations.
- [x] Establish a clean project directory without importing legacy Git history.
