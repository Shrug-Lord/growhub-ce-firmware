# ADR 0002: OTA rollback with health-gated mark-valid

**Status:** Accepted
**Date:** 2026-06-08
**Supersedes:** N/A
**Related:** ADR-0001 (GitHub Releases as canonical hosting), ADR-0003 (UART-required first flash)

## Context

Growhub CE supports manual CE-to-CE OTA updates through the web UI and MQTT. The transfer paths already avoid switching boot partitions unless a complete app image is written successfully, but that only proves the image was flashed. It does not prove the new app can boot far enough to keep the device serviceable.

The launch plan requires rollback safety for v1 so a bad OTA image does not leave a controller stuck on firmware that cannot provide local access or initialize the basic hardware path.

## Decision

Enable ESP-IDF bootloader app rollback with:

```text
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
```

Add a health gate that marks a pending OTA image valid only after:

1. the webserver is running
2. WiFi is either connected as STA or intentionally AP-only because no STA SSID is configured
3. the first sensor UART read attempt has completed without panicking the app

If those gates do not pass within 120 seconds during `ESP_OTA_IMG_PENDING_VERIFY`, the app marks itself invalid and requests rollback.

## Rationale

Use ESP-IDF's rollback state machine instead of a project-specific NVS flag. The bootloader already understands `ESP_OTA_IMG_NEW`, `ESP_OTA_IMG_PENDING_VERIFY`, `ESP_OTA_IMG_VALID`, `ESP_OTA_IMG_INVALID`, and `ESP_OTA_IMG_ABORTED`.

The health gates are intentionally small:

- Webserver readiness proves the local recovery/config surface came up.
- STA-connected-or-AP-only matches the standalone product model. A configured STA device must still join WiFi; a first-boot or intentionally AP-only device should not be blocked.
- Sensor readiness is a read attempt, not a valid reading requirement. This avoids making absent CO2 hardware or a transient SH_NP01 read failure into a rollback trigger while still catching crashes in sensor init/poll setup.

The 120-second timeout keeps the pending state bounded. ESP-IDF rejects starting a new OTA while the running app is still pending verification, so an indefinite wait would create an operational dead end.

## Consequences

**Positive:**

- A crash, watchdog reset, or power loss before mark-valid causes the bootloader to roll back on the next boot.
- URL OTA, file upload OTA, and MQTT-triggered OTA all share the same post-boot validation path.
- The implementation keeps rollback policy separate from the transfer mechanisms.

**Negative:**

- Rollback requires a bootloader built with rollback enabled. The official first-flash package must include that bootloader; OTA-app-only updates cannot retrofit rollback into an older bootloader.
- A device with configured STA credentials may roll back if the configured network is unreachable during the first post-OTA boot, even if the firmware image is otherwise functional.
- Factory images are not normal rollback targets; operational rollback is between OTA slots.

## Verification

Automated build verification:

```bash
cd firmware && .venv/bin/pio run
```

Bench verification still needs to be run on physical hardware before claiming end-to-end OTA rollback behavior:

1. flash a rollback-enabled first-flash package
2. OTA to a known-good app and confirm it marks valid
3. OTA to an app that fails a health gate and confirm rollback to the previous OTA slot
