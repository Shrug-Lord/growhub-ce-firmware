# Release update verification

Status: implementation and exact Linux CI OTA validation complete; destructive
first-flash release checks remain open.

## Candidate

- CE development version: `1.2.0C` (both application version fields).
- Companion development version: `0.2.0` (root/server packages and lockfiles).
- Candidate commit: `5fad38eff1400add60a7e6ec7a37ec090ad48daf`.
- Exact Linux CI firmware SHA-256:
  `6237d69d0d872335374fe2accdf71a0eca611b2db8da151ba3a3bdff1d43e510`.
- Exact Linux CI first-flash ZIP SHA-256:
  `45c7d2873eed38b9abc79d02b3a0e1e43812c59120a5378478ad83b9c927fd2f`.
- CI run: `https://github.com/Shrug-Lord/growhub-ce-firmware/actions/runs/34256340407`.
- The tested hashes are frozen in `release-manifests/v1.2.0C.sha256`.

## Verification mapped to the plan

- Firmware compilation and deterministic first-flash packaging pass. The image
  fits the 1200 KiB OTA slot. Native version-parser tests pass with AddressSanitizer
  and UndefinedBehaviorSanitizer, including downgrade, prerelease, malformed,
  leading-zero, and large-number cases.
- A selected bench controller boots `1.2.0C` after CE-to-CE file upload, preserving
  identity, outlet state, Wi-Fi/MQTT connectivity, and valid time.
- The exact retained Linux CI image passed that same OTA/reboot check. A deliberately
  interrupted upload aborted without reboot or boot-slot change. An isolated test
  image with a forced health-gate failure booted, was rejected, and rolled back to
  the exact CI image. Test-only source and binaries remained outside the repository.
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
release. The exact Linux CI `1.2.0C` image was subsequently validated through
file upload, interrupted transfer, and boot-health rollback as described above.

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

- Complete the exact first-flash ZIP, stock-backup, setup-AP, front-button, LED,
  and recovery checks in `RELEASES.md` on appropriate Growhub and Growhub+
  hardware. These reset or directly manipulate hardware and were not folded into
  the OTA validation above.
- Review the draft release and its generated assets before publishing.
