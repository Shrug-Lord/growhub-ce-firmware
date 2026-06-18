# Growhub CE Firmware — agent guide

Community Edition firmware for the NIWA Growhub ESP32 grow controller. Replaces Niwa's AWS IoT cloud with fully standalone operation; an optional local MQTT broker enables [[Growhub Command Center]] (separate companion repo) for fleet management and logs.

## Doc map

- `CONTEXT.md` — glossary of project-specific terms (read first when ambiguous language comes up)
- `docs/HARDWARE.md` — GPIO pinout, sensor protocol, partition table
- `docs/MQTT.md` — topic schema, payload formats, NVS keys
- `docs/INSTALL.md` — public first-flash install guide
- `docs/OTA.md` — CE-to-CE update model
- `docs/COMMAND-CENTER.md` — companion-app integration contract
- `docs/RELEASES.md` — release packaging and artifact model
- `docs/DEVELOPMENT.md` — source build and developer workflow
- `docs/adr/` — architecture decision records (read these before challenging a design choice)
- `README.md` — public project front page

If your question is "how does X actually work?" → `docs/`. If your question is "what does this term mean?" → `CONTEXT.md`. If your question is "why was this designed this way?" → `docs/adr/`.

## Build & flash

```bash
scripts/build-verified-firmware.sh   # compile + refresh first-flash package
cd firmware && .venv/bin/pio run     # compile-only check
cd firmware && .venv/bin/pio device monitor -b 115200
```

First flash (replacing stock firmware) requires UART; there is no over-the-air path from stock to CE. The public install path is the GitHub Release first-flash ZIP documented in `docs/INSTALL.md`. For local packaging tied to the verified OTA `firmware.bin`, run `scripts/build-verified-firmware.sh`.

Subsequent updates (CE to CE) are OTA via the web UI, MQTT, or `/ota_upload` POST. See `docs/OTA.md`.

## Key source files

```
firmware/src/main.c       boot, task init
firmware/src/sensors.c    SHT-class sensor over UART (protocol decoded; see header comment)
firmware/src/mqtt.c       MQTT publish/subscribe (optional — empty broker = disabled)
firmware/src/relays.c     4-outlet relay control (see docs/HARDWARE.md for GPIO map)
firmware/src/wifi.c       WiFi STA + captive-portal AP mode
firmware/src/webserver.c  config web UI on port 80
firmware/src/config.c     NVS storage for WiFi/MQTT/device config
firmware/src/ota.c        OTA download + apply
firmware/src/schedule.c   grow schedule engine
firmware/src/button.c     physical button (5s hold = factory reset)
```

## CO2

There is no CO2 hardware on the units this project was built against. `sensors.c` returns `co2=0`, `co2_valid=false`. If you add CO2 hardware support later, plumb it through this same struct and flip the flag. Don't hardcode CO2 as mandatory.

## Surgical-fix rules

- **Match scope to the request.** A bug fix doesn't earn a refactor. A one-line change doesn't earn a 200-line diff.
- **Read before modifying.** Understand the existing pattern in the file. Don't add new patterns where existing ones work.
- **Verify before claiming done.** "Compiles" is not "verified." For firmware changes, confirm against the actual device when it matters (relay control, OTA path, schedule execution).
- **Don't reintroduce personal info.** No hardcoded MACs, no hardcoded LAN IPs (use empty/generic defaults), no WiFi credentials. The scrubbing was deliberate — see Phase B C3 history.

## Hardware (bench)

Two physical NIWA Growhub units are used for development. Agents should not assume any specific MAC or LAN IP — read MAC via `esptool chip-id`, discover IP via DHCP / router admin / mDNS. Sensor board variant on both units: `SH_NP01_S_134368b_V1.1` (top) + `SA-24-B 01.01.0644` (bottom). Detailed hardware reference is in `docs/HARDWARE.md`.

## Known issues

- *(none currently tracked — known issues live with the relevant docs once Phase B completes the doc rewrites)*
