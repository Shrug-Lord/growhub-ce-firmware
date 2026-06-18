# ADR 0001: GitHub Releases as canonical firmware hosting

**Status:** Accepted
**Date:** 2026-06-08
**Supersedes:** N/A
**Related:** ADR-0002 (OTA rollback), ADR-0003 (UART-required first flash)

## Context

Growhub CE needs a simple public distribution model for:

1. first-flash artifacts used to replace stock firmware over UART
2. CE-to-CE OTA app images used after CE is already installed

The project considered browser-based first-flash flows, including a GitHub Pages flasher and stock-firmware OTA repointing. Those paths were rejected: browser mixed-content rules make HTTPS pages a poor fit for POSTing to a local HTTP captive portal, and ADR-0003 documents why the stock firmware cannot be used as a UART-free CE installer.

## Decision

Use GitHub Releases as the single canonical home for published firmware artifacts.

Each release publishes:

- `firmware.bin` for CE-to-CE OTA
- `bootloader.bin`, `partitions.bin`, and `ota_data_initial.bin` for recovery/reproducibility
- `merged-firmware.bin` for UART first flash
- `growhub-ce-first-flash-<version>.zip` as the user-facing first-flash bundle
- `SHA256SUMS` for artifact verification

First install from stock firmware uses the release ZIP and UART flashing. Later CE-to-CE updates use `firmware.bin` through the web UI, local file upload, or MQTT-triggered OTA.

## Rationale

GitHub Releases fits the project constraints:

- public, stable URLs for release assets
- a natural place to attach multiple binary artifacts and checksums
- works with a tag-triggered GitHub Actions release workflow
- avoids maintaining separate hosting infrastructure
- keeps first-flash and OTA artifacts visible but explicitly separated

For normal OTA updates, the stable URL pattern is:

```text
https://github.com/Shrug-Lord/Growhub-CE-Firmware/releases/latest/download/firmware.bin
```

## Consequences

**Positive:**

- Users have one canonical download location.
- Maintainers can reproduce and audit release assets from tags.
- The firmware does not need an automatic update checker or release manifest for v1.

**Negative:**

- Users must choose the correct artifact unless the docs and first-flash ZIP make that path obvious.
- GitHub availability is a dependency for URL-based OTA to official releases.
- There is no signed-release verification inside the device in v1; URL trust remains the operator's responsibility.

## Verification

The release workflow and packaging scripts must produce the documented artifact set, and the install/OTA docs must keep this split clear:

- `growhub-ce-first-flash-<version>.zip` for first install from stock firmware
- `firmware.bin` only for CE-to-CE OTA
