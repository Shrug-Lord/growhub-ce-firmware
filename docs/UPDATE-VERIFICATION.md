# Release update verification

Status: implementation, exact Linux CI OTA validation, and the Growhub+ hardware
release checklist are complete. A safe OTA-only parity check on the original
Growhub and final draft-release review remain open.

## Candidate

- CE development version: `1.2.0C` (both application version fields).
- Companion development version: `0.2.0` (root/server packages and lockfiles).
- Firmware fix commit: `3322e9490bf6f4dfdb3f26eaadee0d4ef78aa870`.
- Exact Linux CI firmware SHA-256:
  `302eaaea9fdea5d85dca0a15461be80d7c8f33026aadf066a288963ca6aa49b8`.
- Exact Linux CI first-flash ZIP SHA-256:
  `9f002c6bf5c01f9a83a23373402110c0aa8fbbdd940371bb423fba2f6eb7eeb4`.
- Exact Linux CI merged-image SHA-256:
  `a6979fb25362b1e8a03ff1f8372732c51cf826ce2d3ed10406a938582036784b`.
- CI run: `https://github.com/Shrug-Lord/growhub-ce-firmware/actions/runs/35537888732`.
- The tested hashes are frozen in `release-manifests/v1.2.0C.sha256`.

## Verification mapped to the plan

- Firmware compilation and deterministic first-flash packaging pass. The image
  fits the 1200 KiB OTA slot. Native version-parser tests pass with AddressSanitizer
  and UndefinedBehaviorSanitizer, including downgrade, prerelease, malformed,
  leading-zero, and large-number cases.
- A selected bench controller boots `1.2.0C` after CE-to-CE file upload, preserving
  identity, outlet state, Wi-Fi/MQTT connectivity, and valid time.
- The final exact Linux CI image passed that same OTA/reboot check, booted from an
  OTA partition, and was marked valid by the health gate. A deliberately
  interrupted upload aborted without reboot or boot-slot change. An isolated test
  image with a forced health-gate failure booted, was rejected, and rolled back.
  Test-only source and binaries remained outside the repository.
- The final exact Linux CI first-flash ZIP passed checksum and archive checks and
  was installed with its included script on a Growhub+ bench controller. The
  installer identified the expected ESP32, created a checksummed 4 MiB backup,
  wrote the merged image at offset `0x0`, and verified the flash hash. Fresh boot
  exposed the setup AP; setup completed at `192.168.4.1`; a captured reboot loaded
  `1.2.0C` from the factory partition at `0x20000` with ELF SHA-256 prefix
  `df7ddf935`.
- On that exact first-flash image, Wi-Fi, MQTT, SNTP, sensor reads, AUTO mode, and
  all-off relay state survived reboot. The four physical outlets were exercised
  individually while disconnected from mains loads and matched relay masks
  8, 1, 2, and 4. The setup-AP preference, front-button recovery AP, factory reset,
  operation LED, blocking time-warning LED, and sensor-disconnect behavior passed.
- Multiple management pages originally exhausted the ESP32's ten lwIP sockets and
  produced `httpd_accept_conn` error 23. Limiting HTTP clients to four and enabling
  idle-session eviction preserves capacity for MQTT, DNS, and OTA. Six browser
  sessions plus 48 concurrent `/status` requests passed on hardware while MQTT and
  sensor reporting stayed healthy; the same 48-request check passed again on the
  exact Linux CI OTA and first-flash installations.
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
- Repeated management-page sessions exhausted all available lwIP sockets and made
  the HTTP listener reject every new connection with error 23. The server now
  reserves socket headroom and evicts the least-recently-used idle HTTP session;
  the hardware stress checks above verify the correction.
- A macOS release build was intentionally compared with the earlier Linux CI
  candidate and produced different bytes. No local hash was promoted. The final
  manifest instead records the downloaded Linux CI artifacts that were installed
  and verified on hardware.
- The first backup-sidecar verification command ran outside the backup directory,
  so its relative filename could not be opened. Running the checksum from the
  sidecar's directory passed, and the backup size was exactly 4,194,304 bytes.

## Remaining release gates

- On the original Growhub, perform an OTA-only parity check that preserves the
  device's `op_led_en=0` software fuse for its known blue-LED/GPIO 12 hardware
  fault. Verify the override before and after OTA, capture the high-impedance boot
  log, and confirm sensor, Wi-Fi, MQTT, SNTP, front-button recovery AP, retained
  outlet state, and HTTP concurrency. Do not first-flash, factory-reset, drive
  GPIO 12, or switch its live outlets for this release check; unchanged relay and
  LED behavior inherits the completed CE `1.1.0C` original-Growhub evidence.
- Review the draft release and its generated assets before publishing.
