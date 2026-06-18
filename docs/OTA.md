# Growhub CE Firmware OTA Reference

This document describes how updates work in Growhub Community Edition firmware.

There are two very different cases:

- **First flash**: replacing stock Niwa firmware with CE firmware for the first time
- **Subsequent updates**: updating an already-installed CE device to a newer CE build

Only the second case is OTA in the normal sense.

For the first-flash decision and why stock-firmware OTA is not a viable install path, see [ADR-0003](./adr/0003-uart-required-first-flash.md).

## Scope and current status

### First flash from stock firmware

Not supported over the air.

- Required method: UART / `esptool`
- Reason: stock Niwa OTA cannot be repointed to arbitrary CE images
- Reference: [ADR-0003](./adr/0003-uart-required-first-flash.md)

### CE to CE updates

Supported through three manual trigger paths:

1. web UI: flash from URL
2. web UI: upload local `.bin`
3. MQTT: publish a firmware URL to `growhub/<MAC>/ota`

Deliberately not supported:

- automatic update checks
- unattended auto-update
- delta patches
- stock-to-CE migration via OTA

## Release and artifact model

The project decision is that GitHub Releases will be the canonical home for published firmware artifacts. See [ADR-0001](./adr/0001-github-releases-as-canonical-hosting.md).

Once the release workflow is in place, the expected stable URL pattern for the OTA app image is:

```text
https://github.com/Shrug-Lord/Growhub-CE-Firmware/releases/latest/download/firmware.bin
```

Published release artifacts are expected to include:

- `firmware.bin`
- `bootloader.bin`
- `partitions.bin`
- `ota_data_initial.bin`
- `merged-firmware.bin`
- `growhub-ce-first-flash-<version>.zip`
- `SHA256SUMS`

For OTA, use **`firmware.bin` only**.

Do not use these for OTA:

- `bootloader.bin`
- `partitions.bin`
- `ota_data_initial.bin`
- `merged-firmware.bin`
- `growhub-ce-first-flash-<version>.zip`

Those other artifacts exist for UART flashing, recovery, and reproducibility, not for the CE-to-CE OTA path.

The release packaging process is documented in [`docs/RELEASES.md`](RELEASES.md).

## Recommended user flows

### Planned primary release flow: flash from URL

For a normal CE user updating to an official release, the intended primary manual flow is:

1. Open the device web UI
2. Paste the GitHub Releases `firmware.bin` URL into the OTA URL field
3. Trigger the update
4. Let the device reboot into the new image

Why this is the primary planned path:

- matches the GitHub Releases hosting decision
- avoids asking users to identify the correct artifact among multiple release files
- does not require MQTT or Command Center

### Fallback flow: upload local `firmware.bin`

Use file upload when:

- testing a local dev build
- flashing a manually downloaded release artifact
- the device can reach the browser but cannot reach the URL host
- you want to avoid any URL-entry mistakes

### Automation flow: MQTT OTA trigger

Use MQTT when:

- running a local broker already
- coordinating multiple devices
- integrating with Growhub Command Center or custom tooling

The payload is still a URL, so the device must be able to reach the hosted image.

## Current implementation

### 1. Flash from URL in the web UI

The root page exposes a `From URL` form that sends:

```text
GET /save?ota_url=<url>
```

The handler:

- copies the URL into a 256-byte buffer
- starts `ota_task` in its own FreeRTOS task
- returns a progress page that polls `/ota_status`

The URL-based OTA task currently uses `esp_https_ota` and reports these stages:

- `idle`
- `connecting`
- `flashing`
- `done`
- `failed`

Progress JSON from `/ota_status` looks like:

```json
{"stage":"flashing","bytes":327680}
```

On success, the device reboots automatically.

### 2. Upload local `.bin` in the web UI

The root page also exposes a file upload control that sends:

```text
POST /ota_upload
Content-Type: application/octet-stream
```

Current behavior:

- request body is written directly to the next OTA partition
- maximum accepted payload size is `0x12C000` bytes
- on success, the boot partition is switched to that OTA slot and the device reboots

The size limit matches the OTA slot size documented in [docs/HARDWARE.md](./HARDWARE.md).

This path expects a raw application image that fits into a 1200 KiB OTA slot. In practice that means `.pio/build/growhub/firmware.bin` or the published `firmware.bin` release asset.

### 3. MQTT OTA trigger

The current firmware subscribes to:

```text
growhub/<MAC>/ota
```

Payload:

- raw URL string

Behavior:

- MQTT handler copies the payload
- calls `ota_start_from_url()`
- execution then follows the same URL-based OTA path as the web UI

This wiring is present in the current `mqtt.c` implementation.

## Network and trust assumptions

### HTTP and HTTPS

Current project config enables both:

- `CONFIG_OTA_ALLOW_HTTP=y`
- `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y`

The build also enables the ESP-IDF certificate bundle:

- `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y`

Practical guidance:

- prefer **HTTPS** for official hosted artifacts
- use **HTTP** only for trusted LAN or dev scenarios
- file upload is the simplest option when you do not want the device fetching from another host at all

### Reachability requirements

Each update path has different network needs:

- file upload: browser must reach the device web UI
- URL OTA: device must reach the URL host
- MQTT OTA: device must reach both the broker and the URL host

Because the web UI is always served by the device itself, file upload is the least dependent on outside network reachability.

### Trust model

Current OTA trust is intentionally simple and assumes a trusted LAN operator.

Important current facts:

- no firmware signing
- no per-release signature verification
- no URL allowlist
- no in-device release manifest
- no auto-update loop

If you tell the device to fetch a URL, the device trusts that URL.

## Failure behavior and current safety limits

### Current code behavior

The current implementation is reasonably conservative about switching boot partitions:

- URL OTA only finishes and reboots after `esp_https_ota_finish()` succeeds
- file upload only calls `esp_ota_set_boot_partition()` after the entire write and `esp_ota_end()` succeed

That means a failed transfer should normally leave the currently running image in place.

### Rollback health-gated mark-valid

Bootloader app rollback is enabled for CE builds. See [ADR-0002](./adr/0002-ota-rollback-with-health-gated-mark-valid.md) for the design rationale.

After a successful CE-to-CE OTA update, the new app boots in ESP-IDF's pending-verification state and is marked valid only after:

- webserver is up
- WiFi is either connected or intentionally AP-only
- the first sensor read attempt completes without panic

Current implementation details:

- `health_init()` checks the running OTA partition state during boot
- if the image is `ESP_OTA_IMG_PENDING_VERIFY`, the health task waits for the gates above
- on success it calls `esp_ota_mark_app_valid_cancel_rollback()`
- if the gates do not pass within 120 seconds, it calls `esp_ota_mark_app_invalid_rollback_and_reboot()`

This means OTA success is a two-step process:

1. the update image is downloaded/flashed and selected for next boot
2. the new image proves basic boot health before it is marked valid

If the new image crashes, watchdog-resets, loses power, or times out before it is marked valid, the bootloader should roll back to the previous valid OTA image on the next boot. The factory app partition is still a first-flash/recovery artifact; normal rollback is between OTA slots.

Rollback depends on the rollback-enabled bootloader being flashed. The official v1 first-flash package includes that bootloader; an OTA-app-only update cannot retrofit rollback support into a device that still has an older non-rollback bootloader.

## Web UI behavior

### URL update UX

After submitting an OTA URL, the device returns a dedicated progress page that:

- polls `/ota_status` every 1.5 seconds
- shows `Connecting to server`, then `Flashing firmware`
- displays bytes written
- waits for reboot and tries to reload the root page

Failure text is currently:

```text
Update failed. Check URL and try again.
```

### File upload UX

The upload UI:

- shows browser-side upload progress first
- reports upload KB sent from browser to device
- waits for device reboot after a successful `200 OK`

This is a local convenience path, not a signed or authenticated installer.

## Examples

### Local dev file upload

Build the firmware locally, then upload the resulting app image through the web UI:

```bash
cd firmware
.venv/bin/pio run
```

Use:

```text
.pio/build/growhub/firmware.bin
```

### Local dev URL flash

Serve the build artifact from a trusted machine on the same network:

```bash
cd firmware
.venv/bin/pio run
python3 -m http.server 8080
```

Then paste a URL such as:

```text
http://<host-ip>:8080/.pio/build/growhub/firmware.bin
```

Only use this pattern when the device can route to `<host-ip>`.

## Non-goals for v1

- stock-firmware OTA migration
- automatic release checks
- silent background updates
- signed-release verification pipeline inside the device
- a richer in-device update manager or channel selector

Those may be revisited later, but they are intentionally out of scope for the initial CE release.
