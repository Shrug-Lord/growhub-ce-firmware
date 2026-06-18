# Growhub CE Firmware Hardware Reference

This document captures the hardware facts the Community Edition firmware currently depends on. It is intentionally conservative: anything not verified on the bench or in the current source is called out as an assumption.

For public first-flash instructions, see [INSTALL.md](./INSTALL.md). For first-flash rationale and why stock-firmware OTA is not supported, see [ADR-0003](./adr/0003-uart-required-first-flash.md).

## Verified baseline

- Controller class: NIWA Growhub built around an ESP32-D0WDQ6 rev 1.1 / ESP32-WROOM-32 class module
- Build assumptions: PlatformIO `esp32dev`, 4 MB flash, DIO mode
- Bench hardware: 2 physical NIWA Growhub units
- Verified sensor board variant on both units:
  - Top board: `SH_NP01_S_134368b_V1.1`
  - Bottom board: `SA-24-B 01.01.0644`
- CO2 hardware: not present on the two verified units
  - Current firmware behavior on these units is `co2=0`, `co2_valid=false`
- Do not hardcode or document any specific MAC address, hostname suffix, or LAN IP as if it were universal

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
| 0 | Front-panel button | Active-low with pull-up; also the ESP32 boot-strap pin |
| 2 | Status LED | Used for AP/WiFi/MQTT/recovery/time-needed indication |

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

The status LED reports the highest-priority active state.

| Priority | Pattern | Meaning |
|---|---|---|
| 1 | 3 fast pulses + 1.8 s pause | WiFi Recovery Mode: scanning periodically for configured WiFi |
| 2 | 2 fast pulses + 1.8 s pause | Valid wall time is required by the active AUTO schedule but has not been set |
| 3 | Fast blink, 200 ms on / 200 ms off | AP-only mode, no WiFi credentials, or manually disconnected |
| 4 | Slow blink, 1 s on / 1 s off | WiFi connected, MQTT disconnected or disabled |
| 5 | Solid ON | WiFi connected and MQTT connected |

The time-needed pattern applies only when an active AUTO schedule contains a wall-clock condition, such as a light/fan time window or pump allowed window. Sensor-based conditions and pump intervals without an allowed window can continue without valid wall time.
Sensor-data warnings are shown in the web UI/status payloads, not as a separate LED pattern in v1.

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

Because GPIO 0 is also the firmware button input, anything that holds that line low during reset will affect boot mode.

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

- The verified mapping above is specific to the two development units. Treat other NIWA revisions as unverified until checked on real hardware.
- Current firmware keeps the AP and web UI available even when STA mode is configured; that is a product behavior choice, not a separate hardware capability.
