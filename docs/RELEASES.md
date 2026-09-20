# Release Process

GitHub Releases are the canonical home for published Growhub CE firmware artifacts.

## Version Policy

Growhub CE uses `MAJOR.MINOR.PATCHC`, where the trailing `C` identifies the
Community Edition product line. This is a deliberate project convention rather
than strict Semantic Versioning syntax; compatibility meaning follows
[Semantic Versioning](https://semver.org/) for the three numeric components.

- `PATCHC`, for example `1.1.1C`: compatible bug fixes after a release
- `MINOR.0C`, for example `1.2.0C`: new backward-compatible functionality or
  public MQTT/config contract additions after the previous minor scope freezes
- next major, for example `2.0.0C`: intentionally incompatible public MQTT,
  schedule, configuration, OTA, or documented behavior changes

The amount of code changed does not determine the version. A release version
may accumulate features and fixes until its scope is frozen, but published tag
contents are immutable. `1.1.0C` is the current firmware release and initial
Command Center compatibility baseline. Further compatible feature work belongs
in `1.2.0C`; post-release fixes belong in `1.1.1C`.

The current release-candidate firmware is `1.2.0C`, including the optional retained
MQTT management-address report described in the
[development release notes](RELEASE-NOTES-v1.2.0C.md). Its release artifacts have
frozen Linux CI hashes but are not yet published. Both `GROWHUB_VERSION` in
`firmware/platformio.ini` and `CONFIG_APP_PROJECT_VER` in
`firmware/sdkconfig.growhub` must match the intended firmware version; building
does not increment either value automatically.

Command Center is versioned separately. Its first planned release is `v0.1.0`
and records the exact CE firmware commit and `1.1.0C` compatibility evidence.

## Release Assets

Each tagged release should include:

- `firmware.bin` - app image for CE-to-CE OTA updates
- `bootloader.bin` - UART/recovery artifact
- `partitions.bin` - UART/recovery artifact
- `ota_data_initial.bin` - UART first-flash metadata
- `merged-firmware.bin` - complete first-flash image written at `0x0`
- `growhub-ce-first-flash-<version>.zip` - user-facing first-flash bundle
- `SHA256SUMS` - checksums for release assets

For normal users:

- first install from stock firmware uses `growhub-ce-first-flash-<version>.zip`
- later CE-to-CE updates use `firmware.bin`

## Local Packaging

Build firmware and release assets locally:

```bash
scripts/build-verified-firmware.sh
```

The firmware configuration enables ESP-IDF reproducible-build mode so clean
builds omit date, time, and local-path metadata. Release evidence uses the
retained Linux CI artifact because host-platform linker output may still differ.

The verified build command:

1. builds firmware with `firmware/.venv/bin/pio run`
2. refreshes the release assets from that exact build output
3. verifies the packaged `firmware.bin` matches the OTA `firmware.bin`
4. verifies every file named by the outer and bundled checksum manifests
5. verifies the bundled merged image matches the release image
6. verifies the first-flash ZIP integrity
7. creates the ZIP twice and refuses packaging if the bytes differ

Linux CI retains the complete candidate bundle. After that exact artifact has
passed hardware validation, maintainers freeze every release-file hash in
`release-manifests/<version>.sha256`. Both the main-branch build and release
workflow verify that manifest, and the release workflow refuses to create or
update a draft when the manifest is missing or any byte differs.

Build from an existing local PlatformIO build without rebuilding:

```bash
SKIP_BUILD=1 VERSION=v1.1.0C scripts/package-first-flash.sh
```

When called without `VERSION`, the packager uses `GROWHUB_VERSION` from
`firmware/platformio.ini`. Local calls use PlatformIO's normal user core unless
`PLATFORMIO_CORE_DIR` is explicitly set. Main and release CI share the same
PlatformIO core and toolchain cache so both workflows build with identical
resolved tools; the frozen release manifest still remains the final authority.

The packager:

1. builds the firmware with PlatformIO unless `SKIP_BUILD=1`
2. collects `firmware.bin`, `bootloader.bin`, `partitions.bin`, and `ota_data_initial.bin`
3. creates `merged-firmware.bin`
4. creates the outer `SHA256SUMS` for all release binaries
5. creates a bundle-specific `SHA256SUMS` containing only files included in the ZIP
6. creates `growhub-ce-first-flash-<version>.zip` with fixed, deterministic ZIP metadata
7. recreates the ZIP and requires an exact byte-for-byte match

## GitHub Release Workflow

[`.github/workflows/release.yml`](../.github/workflows/release.yml) runs when a tag matching `v*` is pushed, or manually through `workflow_dispatch`.

The workflow creates a draft release if one does not already exist, then uploads the generated assets.

Manual run input:

```text
version: v1.1.0C
```

## Firmware File Meanings

`firmware.bin` is only the CE application image. It is used by the web UI, OTA URL updates, and MQTT OTA.

`merged-firmware.bin` contains the bootloader, partition table, OTA metadata, and app image at their ESP32 flash offsets:

```text
0x1000   bootloader.bin
0x8000   partitions.bin
0xf000   ota_data_initial.bin
0x20000  firmware.bin
```

The first-flash script writes `merged-firmware.bin` at `0x0`.

Before writing, the first-flash script also dumps the device's current 4 MB flash to `stock-backups/` and writes a `.sha256` sidecar. This backup is not a release artifact; it is created on the user's machine during flashing and should not be uploaded because it may contain device-specific settings.

## Pre-Publish Checklist

- Package assets with `scripts/build-verified-firmware.sh`.
- Verify `SHA256SUMS`.
- Flash a bench Growhub from the generated first-flash ZIP.
- Confirm the generated `stock-backups/*.bin` is 4194304 bytes and has a `.sha256` sidecar.
- Confirm first boot exposes the `growhub_<last4mac>` WiFi AP.
- Confirm web UI setup works at `http://192.168.4.1`.
- Confirm `firmware.bin` works as a CE-to-CE web UI upload.
- Complete the Command Center `CE-1.2.0C` hardware-contract checklist against
  the exact firmware commit and record its binary hash. The runnable checklist
  is `docs/release-evidence/CE-1.2.0C.md` in the companion Command Center
  repository; its release validator requires every item to be checked and the
  evidence record to be marked `Status: passed`.
- Confirm hardware-specific behavior on both Growhub and Growhub+ or explicitly
  inherit unchanged paths from completed evidence for the same unit. Confirm the
  operation LED where the output is safe; for a documented per-device LED-circuit
  fault, verify the persistent hardware override remains disabled across OTA
  instead of driving the faulty output. Never erase a required hardware override
  solely to repeat a release check.
- Review draft release notes before publishing.
