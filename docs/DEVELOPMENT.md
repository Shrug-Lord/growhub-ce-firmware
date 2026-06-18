# Development Guide

This document is for people building Growhub CE from source.

Normal users should start with the [install guide](INSTALL.md), not this page.

## Build

The firmware uses PlatformIO with ESP-IDF.

Use the verified build path when producing a firmware binary that may be tested,
shared, or released:

```bash
scripts/build-verified-firmware.sh
```

This runs `firmware/.venv/bin/pio run`, refreshes the matching first-flash
bundle under `dist/`, and verifies that the packaged `firmware.bin` matches the
PlatformIO build output.

For a compile-only check:

```bash
cd firmware
.venv/bin/pio run
```

Build artifacts land under:

```text
firmware/.pio/build/growhub/
```

Important artifacts:

- `firmware.bin` - application image for CE-to-CE OTA updates
- `bootloader.bin` - ESP32 bootloader
- `partitions.bin` - partition table
- `ota_data_initial.bin` - initial OTA slot metadata

## Flash From Source

For development hardware already wired for UART, PlatformIO can flash directly:

```bash
cd firmware
.venv/bin/pio run -t upload
```

If PlatformIO picks the wrong serial port:

```bash
cd firmware
.venv/bin/pio run -t upload --upload-port /dev/cu.usbserial-0001
```

## Serial Monitor

```bash
cd firmware
.venv/bin/pio device monitor -b 115200
```

## Local Release Bundle

To build firmware and the matching first-flash ZIP in one verified pass:

```bash
scripts/build-verified-firmware.sh
```

For advanced packaging from an existing build without rebuilding:

```bash
SKIP_BUILD=1 VERSION=v1.0.0C scripts/package-first-flash.sh
```

Generated assets land under:

```text
dist/growhub-ce-<version>/
```

The first-flash bundle is:

```text
dist/growhub-ce-<version>/growhub-ce-first-flash-<version>.zip
```

## Restore Stock Firmware

If you have a 4 MB stock firmware dump named `niwa-firmware.bin`, restore it with:

```bash
scripts/restore-stock.sh
```

The device must be in ESP32 download mode.
