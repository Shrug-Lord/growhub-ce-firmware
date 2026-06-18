# Growhub CE Firmware

Community Edition firmware for the NIWA Growhub ESP32 grow controller.

Growhub CE keeps Growhub hardware usable after Niwa's cloud shutdown. Once installed, the controller runs locally over WiFi with a built-in web UI. MQTT is optional.

## Install CE Firmware

Start here if you just want to put CE firmware on your Growhub:

1. Read the [install guide](docs/INSTALL.md).
2. Download `growhub-ce-first-flash-<version>.zip` from the latest GitHub Release.
3. Follow the wiring guide and run the included `flash-growhub-ce.sh` script.

First install from stock firmware requires UART. There is no stock-to-CE over-the-air install path. After CE is installed once, later CE updates can be done from the web UI.

Growhub CE is provided as-is, without warranty. Opening the controller and flashing third-party firmware can render the device unusable if wiring, power, or flashing steps are wrong. You are responsible for deciding whether to install it and for any damage, data loss, or device failure that may result.

## What You Need

- NIWA Growhub controller
- 3.3 V USB-to-TTL adapter, typically CP2102
- jumper wires
- macOS or Linux computer with `python3`
- internet access the first time the flash script installs `esptool`

Important: do not connect the adapter's `3.3V` or `VCC` pin to the Growhub. Power the Growhub from its own power supply.

## Current Status

Verified on bench:

- 2 physical NIWA Growhub units
- sensor board variant `SH_NP01_S_134368b_V1.1` + `SA-24-B 01.01.0644`

Current CE firmware supports:

- local WiFi setup through the built-in web UI
- 4 relay-controlled outlets
- local schedules and manual outlet control
- CE-to-CE OTA updates
- optional MQTT integration

Known limitations:

- first install requires UART
- verified units do not have working CO2 hardware in CE
- the web UI is intended for trusted LAN use and has no authentication
- MQTT is intended for trusted local networks; TLS and username/password auth are not implemented in v1
- first-boot setup AP is open so WiFi can be configured

## After Install

On first boot, CE firmware creates a WiFi access point named:

```text
growhub_<last4mac>
```

Connect to that WiFi network, then open:

```text
http://192.168.4.1
```

From the web UI you can save WiFi credentials, name the device, configure outlets, create schedules, and later update firmware.

## Status LED

The front status LED shows the highest-priority active state:

| Pattern | Meaning |
|---|---|
| 3 fast pulses + pause | WiFi Recovery Mode: periodically scanning for configured WiFi |
| 2 fast pulses + pause | A schedule needs valid time, but time is not set yet |
| Fast blink | AP-only mode, no WiFi credentials, or manually disconnected |
| Slow blink | WiFi connected, MQTT disconnected or disabled |
| Solid ON | WiFi connected and MQTT connected |

The time-needed pattern appears only when an active AUTO schedule uses a wall-clock condition, such as a light/fan time window or pump allowed window. Sensor-based conditions and pump intervals without an allowed window can still run without valid time.
Sensor-data warnings are shown in the web UI/status payloads, not as a separate LED pattern in v1.

## Updating Later

Use `firmware.bin` for CE-to-CE updates. Do not use `merged-firmware.bin` or the first-flash ZIP for normal OTA updates.

Supported update paths:

- web UI: firmware URL
- web UI: local `firmware.bin` upload
- MQTT: publish a firmware URL to `growhub/<MAC>/ota`

See [docs/OTA.md](docs/OTA.md) for details.

## Documentation

- [Install guide](docs/INSTALL.md): first-flash walkthrough for normal users
- [Hardware reference](docs/HARDWARE.md): UART pads, GPIO map, sensor notes, partition table
- [OTA reference](docs/OTA.md): update behavior and release artifacts
- [MQTT reference](docs/MQTT.md): topics and payloads
- [Command Center integration](docs/COMMAND-CENTER.md): companion-app firmware contract
- [Release process](docs/RELEASES.md): maintainer packaging and GitHub Releases
- [Development guide](docs/DEVELOPMENT.md): building from source
- [Security policy](SECURITY.md): trusted-LAN threat model
- [Why UART is required](docs/adr/0003-uart-required-first-flash.md): stock-to-CE install rationale
