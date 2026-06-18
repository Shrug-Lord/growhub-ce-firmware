# Release Process

GitHub Releases are the canonical home for published Growhub CE firmware artifacts.

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

The verified build command:

1. builds firmware with `firmware/.venv/bin/pio run`
2. refreshes the release assets from that exact build output
3. verifies the packaged `firmware.bin` matches the OTA `firmware.bin`
4. verifies the first-flash ZIP integrity

Build from an existing local PlatformIO build without rebuilding:

```bash
SKIP_BUILD=1 VERSION=v1.0.0C scripts/package-first-flash.sh
```

The packager:

1. builds the firmware with PlatformIO unless `SKIP_BUILD=1`
2. collects `firmware.bin`, `bootloader.bin`, `partitions.bin`, and `ota_data_initial.bin`
3. creates `merged-firmware.bin`
4. creates `SHA256SUMS`
5. creates `growhub-ce-first-flash-<version>.zip`

## GitHub Release Workflow

[`.github/workflows/release.yml`](../.github/workflows/release.yml) runs when a tag matching `v*` is pushed, or manually through `workflow_dispatch`.

The workflow creates a draft release if one does not already exist, then uploads the generated assets.

Manual run input:

```text
version: v1.0.0C
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
- Review draft release notes before publishing.
