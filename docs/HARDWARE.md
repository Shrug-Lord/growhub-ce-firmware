# Growhub CE Firmware Hardware Reference

This document captures the hardware facts the Community Edition firmware currently depends on. It is intentionally conservative: anything not verified on the bench or in the current source is called out as an assumption.

For public first-flash instructions, see [INSTALL.md](./INSTALL.md). For first-flash rationale and why stock-firmware OTA is not supported, see [ADR-0003](./adr/0003-uart-required-first-flash.md).

## Verified baseline

- Controller class: NIWA Growhub / Growhub+ built around ESP32-D0WDQ6 rev 1.0 or 1.1 / ESP32-WROOM-32 class modules
- Build assumptions: PlatformIO `esp32dev`, 4 MB flash, DIO mode
- Bench hardware: 3 physical units (2 Growhub+, 1 original Growhub)
- Recorded sensor board variant on both Growhub+ units:
  - Top board: `SH_NP01_S_134368b_V1.1`
  - Bottom board: `SA-24-B 01.01.0644`
- The original Growhub returns valid readings through the same CE UART protocol;
  its sensor-board marking has not been recorded
- CO2 hardware: not available on the three verified units
  - Current firmware behavior is `co2=0`, `co2_valid=false`
- Do not hardcode or document any specific MAC address, hostname suffix, or LAN IP as if it were universal

## Product variants and electrical limits

The combined NIWA manual distinguishes the original Growhub from Growhub+.
These enclosure ratings, not the printed component rating on an individual
relay, are the limits users should follow.

| Variant | Operation LED | Outlet limit | Total limit | Other distinction |
|---|---|---|---|---|
| Growhub (original) | Blue | 10 A per outlet | 15 A total | No external circuit-breaker reset |
| Growhub+ | Green | Follow product label/manual | 15 A total | External circuit-breaker reset |

Source: [NIWA Grow Hub / Grow Hub+ manual](https://globalgarden.co/wp-content/uploads/2023/04/Niwa-Grow-Hub-Manual_Public.pdf).

## GPIO pinout

The firmware loads pin assignments from NVS, but these are the confirmed defaults used by the current code and bench units.

| GPIO | Function | Notes |
|---|---|---|
| 33 | Relay output for Outlet 1 | Relay bit 3 |
| 25 | Relay output for Outlet 2 | Relay bit 0 |
| 26 | Relay output for Outlet 3 | Relay bit 1 |
| 27 | Relay output for Outlet 4 | Relay bit 2 |
| 17 | Sensor UART TX | SH_NP01 UART, driven high very early in boot |
| 16 | Sensor UART RX | SH_NP01 UART, driven high very early in boot |
| 0 | ROM-download Boot pad | Active-low boot strap exposed on the UART header; not the front button |
| 4 | Front-panel setup button | Active-low with pull-up; verified on Growhub and Growhub+ bench units |
| 12 | Blue/green operation LED | Active-low; blue on Growhub, green on Growhub+ |
| 14 | Red malfunction LED | Active-low; actionable warning indication |

Notes:

- Relay outputs are currently driven active-high by `relays.c`.
- Outlet numbering and relay bit numbering do not match in natural order. Outlet 1 is bit 3, while Outlets 2-4 are bits 0-2.
- Sensor UART config uses `pin_sensor_uart_tx` / `pin_sensor_uart_rx` internally and NVS keys `pin_sensor_tx` / `pin_sensor_rx`.

## Outlet mapping

The device has four controllable outlets. The firmware represents their state as a 4-bit mask.

| Outlet | Default label | Relay bit | GPIO |
|---|---|---|---|
| 1 | Unassigned | bit 3, value `8` | 33 |
| 2 | Unassigned | bit 0, value `1` | 25 |
| 3 | Unassigned | bit 1, value `2` | 26 |
| 4 | Unassigned | bit 2, value `4` | 27 |

The actuator string used elsewhere in the firmware follows this bit order:

`[outlet2][outlet3][outlet4][outlet1][0][0][0][0]`

That is why the first visible outlet in the enclosure is not the first bit in the MQTT/status representation.

## Status LED patterns

The two active-low front LEDs report operation and malfunction independently.

| LED | Pattern | Meaning |
|---|---|---|
| Blue/green operation | 3 fast pulses + 1.8 s pause | Setup AP active through automatic fallback or the physical-button override |
| Blue/green operation | Fast blink, 200 ms on / 200 ms off | WiFi disconnected and the setup AP fallback timeout is still running |
| Blue/green operation | Slow blink, 1 s on / 1 s off | WiFi connected; configured/enabled MQTT broker is disconnected |
| Blue/green operation | Solid ON | WiFi connected; MQTT is connected or intentionally not enabled |
| Red malfunction | 2 fast pulses + 1.8 s pause | Valid wall time is required by the active AUTO schedule but has not been set |
| Red malfunction | OFF | No blocking wall-time warning |

The time-needed pattern applies only when an active AUTO schedule contains a wall-clock condition, such as a light/fan time window or pump allowed window. Sensor-based conditions and pump intervals without an allowed window can continue without valid wall time.
Sensor-data warnings are shown in the web UI/status payloads, not as a separate LED pattern in v1.

The operation LED is enabled by default. For a suspected GPIO or LED-circuit
fault that could interfere with normal device startup or operation, it can be
disabled per device under **Actions → Hardware override** in the local web
UI. When disabled, GPIO 12 is left as a floating input and is not driven; the
red malfunction LED on GPIO 14 remains active. Saving this advanced setting
reboots the controller.

Growhub v1 bench discovery confirmed GPIO 12 and GPIO 14 are active-low. The
Growhub+ uses the same operation/malfunction LED roles; its operation LED is
green rather than blue.

## Planned device diagnostics (`v1.2.0C`)

The planned local diagnostics surface reports device, connectivity, time,
sensor, and memory health without extending the MQTT contract. Runtime event
counters are volatile, reset on every boot, and are labeled **since boot**.
They are not written to NVS. Uptime and the current boot's reset reason provide
the context needed to interpret those counters.

The planned page has independent **System**, **Connectivity**, **Time**,
**Sensor**, and **Hardware** sections. Each section may report Healthy,
Attention, or Unavailable and list every applicable issue; there is no combined
device score.
Existing firmware rules determine connectivity, time, sensor-staleness, and OTA
states. Panic, watchdog, and brownout reset reasons are called out as prior-boot
events. Heap values are reported without an invented pass/fail threshold.

The live values refresh every five seconds with a manual refresh fallback. A
versioned, local-only `GET /diagnostics.json` response supplies the shareable
data and doubles as the downloadable support export. Schema version `1` is
additive: consumers must ignore unknown fields. Diagnostic read paths do not
write configuration or alter control state.

Firmware identity on that page includes the release version, build timestamp,
running partition and OTA state, and the SHA-256 digest of the running
application image. The live page may abbreviate the digest for readability;
the sanitized JSON export contains all 64 hexadecimal characters so exact
builds can be compared across devices. Firmware computes and caches that digest
outside the HTTP request path so page refreshes do not repeatedly hash flash.
This value is the ESP application image's embedded validation hash, which
identifies the running image but is distinct from the release artifact's
whole-file `sha256sum`. Release evidence must label and record both values
rather than comparing unlike digests.

The persistent operation-LED setting is presented separately as a hardware
override rather than as a normal diagnostic control.

The planned page provides separate momentary tests for the operation and red
malfunction LEDs. A test drives only the selected LED solid ON for three
seconds, runs asynchronously, permits only one LED test at a time, and then
returns the pins to the normal status-pattern engine. The operation-LED test is
unavailable while its hardware override is disabled. LED tests do not change
relay state or pause schedule evaluation. Tests use an explicit local POST
action; diagnostic GET requests are read-only. Relay self-tests are excluded
because the existing manual controls already exercise outlets and may switch
mains-connected loads.

The planned downloadable diagnostics JSON is safe to share by default: it
includes only the last four MAC characters and omits WiFi names and credentials,
LAN and gateway addresses, and MQTT broker connection details and credentials.
The live page may show full LAN details because it remains on the device's
local web interface.

Sensor health will report **since boot** counts for successful responses,
short/missing responses, invalid headers, and checksum failures, plus the most
recent result and its age. A short response includes its received-byte count.
Raw UART response bytes remain available only in serial logs and are not shown
on the page or included in the JSON export.

Connectivity health will report **since boot** WiFi connection and
disconnection events, automatic fallback and manual recovery activations, MQTT
connection, disconnection, and error events, the current retry phase, and the
most recent WiFi and MQTT failure ages. The live page translates the most recent
failures into plain-language explanations. The JSON export also preserves the
raw ESP-IDF reason and error codes for support analysis.

## Sensor interface

### Board and transport

- Sensor board family: Niwa `SH_NP01`
- Transport: UART1
- Baud: `9600`
- Framing: `8N1`
- Poll interval: `3000 ms`
- Confirmed GPIO pair on bench units and firmware defaults: TX=`17`, RX=`16`

The driver contains an optional scan mode that can brute-force candidate GPIO pairs when bringing up an unfamiliar board revision. That scan path is disabled in the normal firmware build once the correct pair is known.

The normal firmware build reads the sensor UART pin pair from config keys `pin_sensor_tx` / `pin_sensor_rx`.

### Boot-time behavior

`sensors_early_gpio_init()` drives GPIO `17` and `16` high as one of the first steps in `app_main()`. The intent is to avoid break conditions while the sensor-side MCU is booting.

### Protocol

The CE firmware talks to the sensor board with a short request/response protocol reverse-engineered from the stock device.

Command from ESP32 to sensor, 5 bytes:

```text
55 AA 05 00 04
```

Response from sensor to ESP32, 11 bytes:

```text
55 AA 0B 10 00 [light] [T_hi T_lo] [RH_hi RH_lo] [checksum]
```

Field decoding:

- `light`: `uint8`, 0-100
- `T_raw`: big-endian `uint16`
  - `temp_c = -45 + 175 * raw / 65535`
- `RH_raw`: big-endian `uint16`
  - `rh_pct = 100 * raw / 65535`
- `checksum`: sum of all prior bytes modulo 256

On the verified non-CO2 hardware, the firmware publishes:

- temperature
- relative humidity
- light percentage from the SH_NP01 phototransistor
- no CO2 reading

If a future board variant with real CO2 support is added, it should continue using the existing `sensor_reading_t` fields and set `co2_valid=true` only when the hardware actually provides a reading.

## Physical UART header

The PCB exposes a six-pad UART header near the `MK1` label. Left to right:

```text
G O V T R G
```

Recommended adapter:

- Any 3.3 V USB-to-TTL adapter that works with ESP32 flashing, such as a CP2102

Wiring:

| Adapter pin | NIWA pad | Notes |
|---|---|---|
| TXD | R | Crossed TX->RX |
| RXD | T | Crossed RX->TX |
| GND | G | Either ground pad works |
| VCC / 3.3V | leave disconnected | Power the Growhub from its own supply |

Entering ROM download mode for `esptool`:

1. Bridge `O` to `G`
2. Power-cycle the Growhub while the bridge is held
3. Start the flash command
4. Release the bridge after flashing begins, or leave it held for the full operation

Holding the UART header's GPIO 0 Boot pad low during reset enters ROM download mode. The front-panel setup button is a separate GPIO 4 input.

## Flash layout and partition table

The firmware deliberately matches the stock Niwa flash layout so CE-to-CE OTA can reuse the same two OTA app slots.

| Name | Type | Offset | Size | Purpose |
|---|---|---|---|---|
| `nvs` | data | `0x9000` | `0x6000` (24 KiB) | WiFi, device, MQTT, and schedule config |
| `otadata` | data | `0xF000` | `0x2000` (8 KiB) | OTA slot selection metadata |
| `phy_init` | data | `0x11000` | `0x1000` (4 KiB) | RF calibration |
| `factory` | app | `0x20000` | `0x177000` (1500 KiB) | First flashed application image |
| `ota_0` | app | `0x1A0000` | `0x12C000` (1200 KiB) | OTA slot A |
| `ota_1` | app | `0x2D0000` | `0x12C000` (1200 KiB) | OTA slot B |

Implications:

- Community Edition must fit inside a `0x12C000` OTA slot for normal OTA updates.
- The initial UART flash still uses the same bootloader and partition table, with the app image written at `0x20000`.
- Any future rollback design must preserve the two-slot OTA layout unless we explicitly choose to break compatibility.

## Hardware assumptions and caveats

- The verified mapping above is specific to the three development units. Treat other NIWA revisions as unverified until checked on real hardware.
- The web UI is served on every active WiFi interface. Depending on the saved
  setup-AP preference, a configured device may run STA-only while connected;
  the setup AP returns after five continuous minutes without a station IP or
  immediately through the session button override.
