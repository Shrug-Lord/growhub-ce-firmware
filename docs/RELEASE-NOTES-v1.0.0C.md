# Growhub CE v1.0.0C Release Notes

Initial Community Edition firmware release for NIWA Growhub ESP32 controllers.

Growhub CE replaces the discontinued Niwa cloud dependency with local firmware that runs on the controller itself. First install from stock firmware requires UART flashing. After CE is installed, later CE-to-CE updates can be installed from the web UI or through MQTT.

Growhub CE is provided as-is, without warranty. Opening the controller and flashing third-party firmware can render the device unusable if wiring, power, or flashing steps are wrong. You are responsible for deciding whether to install it and for any damage, data loss, or device failure that may result.

## Download

Use these release assets:

- `growhub-ce-first-flash-v1.0.0C.zip` for first install from stock firmware
- `firmware.bin` for later CE-to-CE updates
- `SHA256SUMS` to verify release assets

Do not use `merged-firmware.bin` for normal web UI OTA updates. It is only for UART first flashing.

## Highlights

- Local WiFi setup and web UI
- Four outlet relay control with manual and AUTO modes
- Assignment-aware scheduling for lights, fans, humidifiers, dehumidifiers, heaters, AC controllers, and water pumps
- Time windows, always-on schedules, temperature/rH bands, and pump interval schedules
- Browser time sync and configurable SNTP servers
- Time-source and sensor-health warnings in the web UI and MQTT schedule state
- WiFi Recovery Mode for reconnecting after configured WiFi becomes unavailable
- CE-to-CE OTA updates by web UI upload, web UI URL, or MQTT URL
- Optional local MQTT integration for Command Center or other local tooling

## Verified Hardware

This release was bench-tested on two NIWA Growhub units with:

- ESP32 controller hardware
- sensor board variant `SH_NP01_S_134368b_V1.1`
- bottom board marking `SA-24-B 01.01.0644`

## Known Limitations

- First install from stock firmware requires opening the controller and flashing over UART.
- The verified hardware does not expose working CO2 support in CE; CO2 reports invalid/unsupported.
- The local web UI is intended for trusted LAN use and has no authentication.
- MQTT is optional and local-network oriented; TLS and username/password MQTT auth are not implemented in this firmware release.
- First-boot setup AP is open so the user can configure WiFi.

## Before Publishing

- Confirm `scripts/build-verified-firmware.sh` passes.
- Confirm the first-flash ZIP boots a bench unit from stock/blank state.
- Confirm `firmware.bin` works for CE-to-CE web UI upload.
- Confirm `SHA256SUMS` matches the release assets attached to GitHub.
