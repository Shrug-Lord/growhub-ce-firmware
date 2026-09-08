# Release update verification

Status: implementation complete; release publication gates remain open.

## Candidate

- CE development version: `1.2.0C` (both application version fields).
- Companion development version: `0.2.0` (root/server packages and lockfiles).
- Final local firmware SHA-256:
  `cad690f8392001bb215facb29235d300978eb81cebc2e58c92eb11b577fb7ceb`.
- Final local first-flash ZIP SHA-256:
  `4bd7d1cd08b87dd48cdde3b8270965c95c5a6cba0b3555a66591c73417b11925`.
- These are local development artifacts, not frozen Linux CI release artifacts.

## Verification mapped to the plan

- Firmware compilation and deterministic first-flash packaging pass. The image
  fits the 1200 KiB OTA slot. Native version-parser tests pass with AddressSanitizer
  and UndefinedBehaviorSanitizer, including downgrade, prerelease, malformed,
  leading-zero, and large-number cases.
- A selected bench controller boots `1.2.0C` after CE-to-CE file upload, preserving
  identity, outlet state, Wi-Fi/MQTT connectivity, and valid time.
- Real GitHub release discovery reads the published `v1.1.0C` asset and digest,
  correctly offering no downgrade from `1.2.0C`.
- An isolated instance of the actual Command Center MQTT mirror reads retained
  update state. MQTT preference changes appear on the local HTTP API; HTTP
  changes return through MQTT. Journal rows remain unchanged. A delayed duplicate cannot undo a newer
  preference change. Checks are restored
  to off after verification. Unconfirmed install requests are rejected.
- Command Center tests cover disabled background checks, explicit confirmation,
  exact release identity, per-version dismissal, 24-hour deferral, malformed
  metadata, stale firmware state, duplicate active installs, and host requests
  that must not be retried. Migration tests preserve journal data and disable
  legacy unattended-install preferences.
- Browser checks use simulated newer releases with the real Command Center
  update service and a disposable database. Cancel creates no install request;
  confirming creates a single request for the displayed version with
  `requested_by: user` and `confirmed: true`. Later/Skip hide prompts while keeping
  manual installation available. A simulated firmware failure leaves retry
  user-controlled. Desktop and 390px mobile layouts have no horizontal overflow.
- The actual standalone controller page renders its release controls, with checks
  off, and its Check now button performs release discovery.
- Existing browser smoke/accessibility checks pass with the final client build.

## Commands

From the firmware repository:

```sh
scripts/build-verified-firmware.sh
cc -Wall -Wextra -Werror -fsanitize=address,undefined tests/release_version_test.c -o /tmp/growhub-release-version-test
/tmp/growhub-release-version-test
git diff --check
```

From the Command Center repository, using the required Node 24 runtime:

```sh
npm test
npm run test:integration
npm run lint
npm run format:check
npm run test:e2e:smoke
node --test deploy/server/test/apiContracts.test.js
git diff --check
```

Live bench scripts and Playwright CLI checks use private connection arguments
outside the repository. Screenshots are retained locally under the companion
repository's ignored `output/playwright/` directory.

## Official download path hardware proof

An isolated copy of the same updater was built with display/application version
`1.0.0C`, allowing it to offer the already published `v1.1.0C` release without
publishing a new tag. Production sources stayed at `1.2.0C` throughout.

- A temporary fault-injection build changed one expected SHA-256 digit after
  discovery. The device downloaded all 1,077,552 bytes from GitHub, rejected the
  mismatch, and continued running without rebooting or selecting that image.
- The normal isolated build downloaded the same official release over HTTPS,
  verified its SHA-256, size, and embedded version, flashed it, and rebooted into
  the published `v1.1.0C` with identity, connectivity, and outlet state preserved.
- The final `1.2.0C` candidate is restored after the test. Test binaries and the
  checksum fault injection exist only in temporary storage; neither is a release
  artifact or committed source change.

This proves the implemented official download/install path using an existing
release. It does not replace validating the exact eventual Linux CI `1.2.0C`
release artifact or intentionally exercising power loss/boot rollback.

## Failure tickets resolved

- Initial Command Center regression failures referenced the removed unattended
  API or the prior schema version. Updated fixtures and migration expectations,
  added behavior regressions, and reran until green.
- The first real official-download test failed before receiving any image bytes.
  GitHub's signed redirect request line exceeded ESP-IDF's default 512-byte
  transmit buffer. Verified this limit in the pinned SDK and increased the
  official HTTP client's transmit buffer to 2048 bytes, matching the bounded
  redirect URL handling. This affects only the official release updater.

## Remaining release gates

- Validate the exact frozen Linux CI artifact and intentionally exercise transfer
  interruption and boot rollback on release hardware. The controlled official
  download and checksum-mismatch tests above passed against an existing release.
- The updated Linux/systemd host service has not been exercised through a real
  Docker rebuild/restart on a Linux/Pi release host in this session. Tests cover
  request validation/consumption, confirmation, and failure behavior; the existing
  backup-first script now also checks the running server version after readiness.
- Complete the first-flash and other hardware checks in `RELEASES.md`, validate
  the exact Linux CI candidate, then freeze its release hashes before tagging.
- Command Center production deployment and publishing either release were not
  performed as part of implementation verification.
