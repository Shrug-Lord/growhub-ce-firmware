# ADR 0003: First flash requires UART

**Status:** Accepted
**Date:** 2026-05-29
**Supersedes:** N/A (initial decision on first-flash UX)
**Related:** ADR-0001 (GitHub Releases as canonical hosting), ADR-0002 (OTA rollback)

## Context

CE firmware replaces stock Niwa firmware on a NIWA Growhub. To install it, a user must overwrite the stock app partition with the CE app image. There are two conceivable categories of install path:

1. **Over-the-air (OTA) from stock firmware** — hijack the stock firmware's existing OTA mechanism to deliver CE firmware instead of a NIWA update. No hardware required by the user.
2. **UART (serial flash)** — connect a USB-to-TTL adapter to the PCB's UART header and use `esptool` to write the CE app image directly.

OTA-from-stock is the friendlier UX. We investigated it exhaustively because the project's adoption story is much stronger if no user has to open a device, identify a header, and wire up a serial adapter.

## Decision

**First flash requires UART. No OTA-from-stock path will be supported.**

OTA-after-first-flash (i.e., device-to-CE-version-to-CE-version) is unaffected — that's our own update mechanism running on CE firmware and remains supported per ADR-0001/ADR-0002.

## Rationale — four independent blockers

Bench investigation found four independent blockers for OTA-from-stock:

### 1. Captive portal `?ota=<URL>` does not accept arbitrary URLs

Stock firmware's captive portal at `http://192.168.4.1/` handles a `?ota=Update` GET param. The handler only honors the literal value `Update` — any other value (including a URL) returns `404 "This URI does not exist"`. Verified empirically with `curl` and corroborated by extracting the form HTML directly from the stock binary: the form has no URL input field, only ssid/password text inputs and Connect/Return/Update submit buttons.

### 2. CloudFront fetch is gated on a successful `notify-ota` POST

Disassembly of the stock firmware (ota_0 partition of `niwa-firmware.bin`) shows the OTA control flow:

- `https_ota_task` (function at `0x400d81d8`) blocks on a semaphore at `0x3ffb4398`.
- The captive portal `?ota=Update` flag (or boot-time NVS check) gives the semaphore.
- `https_ota_task` wakes, calls the function at `0x400d8104` which issues a POST to `https://v2.api.niwa.io/api/v1/iot-devices/notify-ota` with the device's BSSID + firmware version.
- Only on a *successful* notify-ota response does a separate task (`ota_task` at `0x400d82c8`) fire the actual CloudFront download.

With the Niwa cloud shut down, `v2.api.niwa.io` is dead. The notify-ota POST times out, the CloudFront fetch never starts. Verified on a UART log from a bench unit on 2026-05-26: the POST attempt logs `ESP_ERR_HTTP_CONNECT` with `Last mbedtls failure: 0x0` (TCP-level failure, no TLS reached), and no CloudFront-related log lines ever appear afterward.

### 3. `v2.api.niwa.io` TLS is pinned to Amazon Root CA 1

Even if we DNS-spoofed `v2.api.niwa.io` to a server we control, the device's `esp-tls` client validates the server cert chain against a CA store. The stock binary bundles **exactly one** server CA: Amazon Root CA 1 (cert at file offset `0x25878` in `niwa-firmware.bin`, ~1162 bytes, decoded from base64 preview). The CA is loaded via `set_global_ca_store` — a singleton API, so the same trust anchor applies firmware-wide.

### 4. CloudFront TLS likewise pinned

Same trust anchor, same blocker. Even if we surmounted #1, #2, and #3 (somehow forging a `notify-ota` response that triggers the CloudFront fetch with a custom URL), the CloudFront-bound TLS handshake would also need an Amazon-signed cert.

## Dead-end paths explicitly considered

- **DNS spoof of `d36vvu9671ntl6.cloudfront.net` to a laptop server**: blocked by #4. Bench testing verified the DNS redirection leg before the binary disassembly closed the question.
- **DNS spoof of `v2.api.niwa.io` + forged notify-ota response**: blocked by #3, and additionally we don't know the expected response shape that would trigger CloudFront fetch.
- **MQTT-trigger backdoor**: the device authenticates to AWS IoT with a per-device mTLS client cert burned into firmware. We can't impersonate AWS IoT MQTT without that cert chain.
- **Pi-appliance running DNS spoof + private CA install**: would require modifying the device's CA store, which requires re-flashing — defeating the purpose of avoiding UART. (A Pi-appliance retains value as the [[Growhub Command Center]] companion, but not for first flash.)

## Consequences

**Positive:**
- v1 scope shrinks substantially — no UART-free flasher tool to build, no `migration/` Pi-appliance to maintain, no captive-portal-flasher-from-browser nonsense.
- README's first-page install story is linear and honest.
- Cert-pinning gating means there's no plausible "supply chain compromise" path through stock firmware to inject malicious CE images — the same property that blocks us blocks attackers.

**Negative:**
- Higher install friction. Users must own (or buy) a USB-to-TTL adapter, identify the UART header on their device, wire it up correctly, and run an `esptool` command. The README must walk this carefully.
- The audience self-selects to "people willing to UART-flash an ESP32" — a real reduction in addressable users compared to a hypothetical captive-portal-flasher install.

**Mitigations:**
- The install docs include the UART hardware shopping list (CP2102 adapter ~$5-10) and a photo guide.
- An OTA mechanism within CE firmware (Q6: URL paste + file upload + MQTT trigger) means users only do UART **once** — every subsequent update is over-the-air via our own pipeline.
- Rollback safety (Q8 + ADR-0002) reduces the cost of a bad OTA.

## Reversibility

This decision could be revisited if any of the four blockers becomes surmountable:

- New firmware version from Niwa: blocked by Niwa being shut down.
- CA bypass in stock firmware: would require finding a verification vulnerability not present in `niwa-firmware.bin` analysis. Unlikely.
- Pi-appliance with CA management: only works if we accept it as an install path that requires a Pi. Could be added as an optional v2 install method, but doesn't replace UART for users without a Pi.

If a reversal is warranted, this ADR should be superseded with a new one explaining what changed and why.
