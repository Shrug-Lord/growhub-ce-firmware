# Growhub CE v1.1.0C Release Notes

Status: released. Command Center hardware-contract, host-compatibility, and
accessibility evidence is complete, and Command Center `v0.1.0` is published.

Growhub CE `v1.1.0C` is the firmware release after public `v1.0.0C` and
the initial tested firmware baseline for Growhub Command Center. It remains
fully functional without Command Center or an MQTT broker.

## Highlights

- Verified support for both original Growhub and Growhub+ controllers
- Configurable setup access point that can turn off after WiFi connects
- Automatic setup-AP fallback after five continuous minutes without a station IP
- Three-second front-button setup-AP override, with the same hold used to cancel it
- Correct front-button mapping on active-low GPIO 4
- Correct two-LED support:
  - blue/green operation LED on active-low GPIO 12
  - red malfunction LED on active-low GPIO 14
- Solid operation LED for healthy standalone use; MQTT disconnect blinking only
  when a configured and enabled broker is unexpectedly unavailable
- Per-device operation-LED disable setting that leaves GPIO 12 inactive while
  preserving the red malfunction LED
- Outlet assignments and user-facing labels published as retained firmware-owned
  MQTT state
- MQTT commands and typed errors for outlet configuration and time sync
- Retained schedule mirrors with time health, sensor health, warnings, and all
  four outlet status summaries
- Correct raw `0`-`255` light-level contract and local display wording; the
  sensor byte is not a calibrated percentage
- Documented `schedule` source for automatic schedule-engine mirror updates
- Immediate schedule reevaluation after relevant time, mode, assignment, and
  configuration changes
- CE-to-CE local firmware upload through the web UI in addition to URL OTA
- Complete local web pages for fully configured four-outlet schedules, without
  response-buffer truncation in the Power Outlets section
- Local schedule-editor support for the full documented pump range: intervals
  from 1-168 hours and run durations from 1-240 minutes

## WiFi And Recovery

The setup AP remains enabled by default to preserve `v1.0.0C` behavior. Users
may disable **Keep setup access point active while connected** in the web UI.
When disabled:

- the setup AP turns off after the device receives a station IP
- the confirmation page links directly to the device's LAN IP before shutdown
- five continuous minutes without a station IP reactivate the setup AP
- station recovery attempts continue while the fallback AP is available
- a three-second button hold enables or cancels the AP for the current boot
  session without erasing configuration

A five-second hold still performs a factory reset.

## Hardware Corrections

Earlier CE builds incorrectly assumed the front button used GPIO 0 and the
status LED used GPIO 2. Bench discovery established the shared control mapping:

| Function | GPIO | Polarity |
|---|---:|---|
| ROM-download Boot pad | 0 | Active-low boot strap |
| Front setup button | 4 | Active-low with pull-up |
| Blue/green operation LED | 12 | Active-low |
| Red malfunction LED | 14 | Active-low |

Existing CE NVS values using the earlier GPIO 0/GPIO 2 assumptions migrate to
the verified button and operation-LED pins during boot.

## MQTT And Command Center Contract

`v1.1.0C` adds the public contracts required by the initial Command Center
release:

- retained `outlets/state`
- full-replacement `outlets/config` and typed `outlets/error`
- retained `schedule/state` including the active CE v3 schedule and health state
- `time/action` and typed `time/error`
- typed `schedule/error` and `control/error`
- authoritative mode, relay, warning, and outlet-status confirmation

Command Center treats `1.1.0C` as its initial bench-tested baseline, but runtime
workflows validate topic capabilities and payload contract versions rather than
relying only on the display-version string.

## Compatibility And Upgrade Notes

- Existing CE devices update with the normal `firmware.bin` OTA path; UART is
  required only when replacing stock firmware for the first time.
- The first-flash ZIP carries its own verified checksum manifest containing only
  the instructions, flasher, and merged image actually present in the bundle.
- The public `v1.0.0C` schedule contract was already CE schedule version 3 and
  remains the accepted schedule version in `v1.1.0C`.
- New MQTT fields and topics are additive. Consumers should ignore unknown
  fields and branch on documented payload version and error-reason fields.
- MQTT remains optional. An empty or soft-disabled broker configuration is a
  healthy standalone state.
- The setup AP remains on by default across upgrades unless the user explicitly
  disables it.
- The operation LED remains enabled by default across upgrades. It may be
  disabled per device from **Actions → Hardware override** in the local web
  UI; saving that advanced setting reboots the controller and does not disable
  the red malfunction LED.

## Verified Hardware

The release firmware was exercised on three physical controllers:

- two NIWA Growhub+ units
- one original NIWA Growhub
- ESP32-D0WDQ6 rev 1.0/1.1 class modules with 4 MB flash
- all four physical relay mappings on the original Growhub
- sensor telemetry, WiFi, SNTP, OTA, front button, setup-AP override, and both
  LED outputs across the supported product variants

The original Growhub manual rating is 10 A per outlet and 15 A total. Growhub+
is rated 15 A total and includes the external circuit-breaker reset. Users must
follow the enclosure label and product manual.

## Known Limitations

- First installation from stock firmware still requires opening the controller
  and flashing over UART.
- CO2 is unavailable on the verified hardware; CE reports
  `co2=0`, `co2_valid=false`.
- The local web UI is intended for a trusted LAN and has no authentication.
- MQTT is plain local-network MQTT without TLS or username/password support.
- The initial setup AP is open so first-time WiFi configuration is possible.

## Release Evidence

The Command Center `CE-1.1.0C` MQTT hardware checklist is complete against CE
firmware commit `15dcf80807e8c9c6cd9faaf49ded4d5175c407b1`. The verified OTA
image SHA-256 is
`b547e5052afed85f846bc5e2496b47df86404b820b89127f7b635a2f9d6b4461`,
the merged first-flash image SHA-256 is
`df3bc3f7ee436d14f8ac51644fef757b23f62856fabf63a5f0025c7f59c8891a`,
and the reproducible release ZIP SHA-256 is
`157a73f1e26871ff8c11763e11de8e07d5e59c75046519512c973b3b3e3ce8a8`.

The earlier bench ZIP was recorded as
`8e0d10a2bb3ff1909a38bcd47a48234761eaa9c4f7c44dc8bb38b7f4932e19a9`.
Its container hash included creation timestamps. Release packaging now fixes
ZIP metadata and verifies a second byte-for-byte build; the tested firmware and
merged-image payloads are unchanged.

The GitHub Release keeps the generated `SHA256SUMS`, first-flash ZIP, merged
image, and CE-to-CE OTA image together.
