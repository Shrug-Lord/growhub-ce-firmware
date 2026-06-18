# Command Center Integration Reference

*Last updated: 2026-06-14 (v3 scheduling + time health contract) | Firmware version: 1.0.0C*

Use this doc to brief the companion Command Center project without reading the full firmware repo.

Normal users do not need this page. Start with [INSTALL.md](INSTALL.md) instead.

---

## Hardware

- **Target:** Niwa Growhub — ESP32-D0WDQ6 rev 1.1
- **MAC format:** `FCE8C0XXXXXX` (last 4 hex used for AP SSID: `growhub_XXXX`)
- **Default AP:** `growhub_<last4mac>` — open, always-on even when connected to home WiFi

### Confirmed GPIO Pins

| Pin | Function |
|-----|----------|
| 33 | Outlet 1 (bit3) |
| 25 | Outlet 2 (bit0) |
| 26 | Outlet 3 (bit1) |
| 27 | Outlet 4 (bit2) |
| 16 | Sensor UART RX (SH_NP01) |
| 17 | Sensor UART TX (SH_NP01) |
| 0  | Button |
| 2  | LED |

---

## Outlets & Relay Bitmask

**4 outlets total.** The bitmask is a 4-bit value (0–15):

| Bit | Outlet | Default label |
|-----|--------|---------------------|
| bit3 (value 8) | Outlet 1 | *(unassigned)* |
| bit0 (value 1) | Outlet 2 | *(unassigned)* |
| bit1 (value 2) | Outlet 3 | *(unassigned)* |
| bit2 (value 4) | Outlet 4 | *(unassigned)* |

Relay names are user-configurable and default to unassigned. Available device types: `None`, `Light`, `Fan`, `Humidifier`, `Dehumidifier`, `Water Pump`, `Heater`, `AC Controller`.

**Important:** The Command Center currently thinks there are 3 outlets and has no concept of outlet profiles. Both need to be fixed in the CC polish phase.

---

## MQTT Interface

**Broker:** User-configured host:port. Soft-disable supported (flag in NVS -- broker config preserved, client won't connect).

All topics use MAC as the device identifier: `growhub/<MAC>/...`

### Device -> CC (publishes)

| Topic | Payload | Notes |
|-------|---------|-------|
| `growhub/<MAC>/sensor/live` | JSON (see below) | Published on schedule (default 6s) |
| `growhub/<MAC>/status` | `"online"` / `"offline"` | Retained, last-will = `"offline"` |
| `growhub/<MAC>/schedule/state` | JSON | Retained active schedule mirror from firmware |
| `growhub/<MAC>/schedule/error` | JSON | Rejected schedule writes/actions |
| `growhub/<MAC>/control/error` | JSON | Rejected control commands |

**Sensor:** Niwa SH_NP01 sensor board -- UART, 9600 baud, GPIO 16/17. Two hardware variants exist:
- **Without CO2** -- provides temp, humidity, light
- **With CO2** -- same protocol, adds CO2 ppm in extended response

**Sensor payload format:**
```json
{
  "nId": "AABBCCDDEEFF",
  "name": "GrowHub_B2C3",
  "fw": "1.0.0C",
  "data": [{
    "l": 75,
    "h": 58.2,
    "t": 24.1,
    "a": "01000000",
    "ts": "2026-04-28 12:00:00:000Z"
  }]
}
```
- `l` = light % (phototransistor on the SH_NP01 sensor board — real sensor reading)
- `h` = humidity %
- `t` = temperature °C (always Celsius on the wire regardless of display preference)
- `a` = actuator string (8 chars): `[outlet2][outlet3][outlet4][outlet1][0][0][0][0]`
  - pos 0 = bit0 Outlet 2, pos 1 = bit1 Outlet 3, pos 2 = bit2 Outlet 4, pos 3 = bit3 Outlet 1
  - pos 4–7 = `"0000"` (reserved, always zero)
  - e.g. Outlet 2 ON only → `"10000000"`, all off → `"00000000"`, all on → `"11110000"`
- `c2` = CO2 ppm — **only present on CO2 sensor variant**, omitted otherwise

**Schedule state payload:**
```json
{
  "active": true,
  "mode": "auto",
  "source": "local",
  "time_valid": true,
  "time_source": "sntp",
  "sntp_status": "synced",
  "time_warning": "",
  "sensor_warning": "",
  "warnings": [],
  "schedule": {
    "v": 3,
    "outlets": [
      {
        "id": 1,
        "conditions": [
          { "type": "time_window", "start": "06:00", "end": "22:00" }
        ]
      },
      {
        "id": 2,
        "conditions": [
          { "type": "time_window", "start": "08:00", "end": "20:00" },
          { "type": "temp_high_band_c", "low_c": 24.0, "high_c": 27.0 },
          { "type": "rh_high_band", "low": 55, "high": 65 }
        ]
      }
    ]
  },
  "outlet_status": [
    {
      "id": 1,
      "state": "on",
      "summary": "ON \u00b7 off at 10:00 PM (5h 55m)"
    },
    {
      "id": 2,
      "state": "off",
      "summary": "waiting for temp > 80.6\u00b0F, rH > 65%, or 8:00 AM"
    },
    {
      "id": 3,
      "state": "off",
      "summary": ""
    },
    {
      "id": 4,
      "state": "off",
      "summary": ""
    }
  ]
}
```
- Retained on `growhub/<MAC>/schedule/state`
- Published on MQTT reconnect, accepted `grow` writes, local firmware schedule saves/clears, and mode changes
- Published when `time_warning` or `sensor_warning` appears or clears, even if relay outputs do not change
- `active=false` uses `"schedule": null`
- `source` is informational: `local`, `mqtt`, or `reconnect`
- `outlet_status` always includes all four outlets; summaries are empty in manual mode, for unassigned outlets, and for outlets with no active schedule entry
- `outlet_status[].summary` is firmware-owned display text, not a structured reason API. CC should display it as text and use `warnings[].code` plus `warnings[].outlets` for stable warning logic.
- Time health fields let CC warn when wall-clock schedules are paused or SNTP is unhealthy
- `time_warning` is general device time health and may be non-empty in manual mode. CC should use sync or drift wording for unhealthy time sources, and reserve automation-paused or waiting-for-time wording for active AUTO wall-clock schedules that cannot run.
- Because `schedule/state` is retained, CC should treat retained `time_warning` as current automation state on subscribe and clear the banner when the retained state publishes `time_warning: ""`.
- `sensor_warning` is non-empty when an active AUTO schedule depends on unavailable or stale temp/rH data; CC should show it as a top-level warning. It is empty in manual mode.
- Because `schedule/state` is retained, CC should treat retained `sensor_warning` as current automation state on subscribe and clear the banner when the retained state publishes `sensor_warning: ""`.
- If both warnings are non-empty, CC should show both in one compact warning area rather than choosing a single highest-priority banner. Order warnings by severity: active AUTO wall-clock automation blocked first, active AUTO temp/rH automation paused second, and drift-only or sync-health time warnings after automation-blocking warnings.
- `warnings` contains machine-readable warning entries with `code`, `message`, `severity`, and optional `outlets`, published in display order. CC should use `warnings[].code` for logic and `warnings[].outlets` to highlight affected outlets. Omitted or empty `outlets` means device-wide. Warning `outlets` are numeric physical outlet IDs only; CC should resolve labels or assignments from current outlet state rather than expecting copied display names in warning entries. `time_sync_required` and `sensor_data_unavailable` include affected outlets when automation is blocked or paused; `time_sntp_unhealthy` stays device-wide because it is a drift/sync risk, not a specific outlet block. `message` is firmware-owned default display copy for the local web UI and simple clients; CC may render its own product-specific copy from `code` while preserving the warning meaning and severity.
- Initial warning codes:

| Code | Severity | Meaning |
|---|---|---|
| `time_sync_required` | `blocking` | Active AUTO wall-clock automation cannot run until valid time is set |
| `sensor_data_unavailable` | `warning` | Active AUTO temp/rH automation is paused because required sensor data is invalid, unavailable, or stale |
| `time_sntp_unhealthy` | `warning` | Configured SNTP has not synced, or its last successful sync is stale while time is otherwise valid |

- Command Center should treat this as the authoritative active device schedule

**Control error payload:**
```json
{
  "command": "control/relay",
  "reason": "manual_mode_required"
}
```
- Published on `growhub/<MAC>/control/error`
- Command Center should surface rejected control commands instead of assuming the relay state changed
- `reason` is a fixed v1 enum. CC should branch on `reason`, not `detail`; unknown future reasons should fall back to a generic rejected-command message.
- Control reasons: `invalid_payload`, `invalid_mode`, `invalid_relay_mask`, `manual_mode_required`

**Schedule error payload:**
```json
{
  "reason": "condition_not_allowed",
  "outlet": 2,
  "detail": "condition not allowed for outlet assignment"
}
```
- Published on `growhub/<MAC>/schedule/error`
- Command Center should keep the previous mirrored schedule active when a schedule write is rejected
- `reason` is a fixed v1 enum. CC should branch on `reason`, not `detail`; unknown future reasons should fall back to a generic rejected-schedule or rejected-action message.
- Schedule reasons: `invalid_payload`, `unsupported_schedule_version`, `empty_schedule`, `invalid_outlet`, `missing_conditions`, `duplicate_condition`, `invalid_condition`, `condition_not_allowed`, `always_on_exclusive`, `invalid_time_window`, `invalid_band`, `invalid_interval`, `unsupported_action`, `auto_mode_required`, `pump_schedule_required`, `time_sync_required`, `pump_window_ineligible`

### CC → Device (subscribes)

| Topic | Payload | Effect |
|-------|---------|--------|
| `growhub/<MAC>/control/mode` | `"2"` = manual, `"3"` = auto, `"7"` = all off + manual | Sets relay mode |
| `growhub/<MAC>/control/relay` | Decimal bitmask string `"0"`–`"15"` | Sets relay state (manual mode) |
| `growhub/<MAC>/schedule/action` | JSON `{"action":"pump_run_now","outlet":4}` | Runs schedule-owned actions |
| `growhub/<MAC>/config` | JSON `{"tZ":"...", "timeSrc":"sntp", "sntpPrimary":"pool.ntp.org", "sntpSecondary":"time.nist.gov", "tmpOff":0, "rhOff":0}` | Updates time settings / temp/rH calibration offsets |
| `growhub/<MAC>/grow` | Schedule JSON v3 (see below) | Loads and persists schedule |
| `growhub/<MAC>/ota` | URL string | Triggers OTA update from URL |

---

## Schedule Format (v3 — outlet conditions)

Command Center should send this format on the `grow` topic when a schedule is loaded onto a device. The firmware persists the schedule to NVS and evaluates it in AUTO mode, so the grow continues if Command Center, MQTT, or the local network connection later goes offline.

Required load sequence:

1. Publish the v3 schedule JSON to `growhub/<MAC>/grow`
2. Publish `"3"` to `growhub/<MAC>/control/mode` to put the device in AUTO mode
3. Subscribe to `growhub/<MAC>/schedule/state` and mirror the retained state back into CC's active schedule display
4. Treat `control/relay` as a manual-mode override path, not the normal schedule execution path

```json
{
  "v": 3,
  "outlets": [
    {
      "id": 1,
      "conditions": [
        { "type": "time_window", "start": "06:00", "end": "22:00" }
      ]
    },
    {
      "id": 2,
      "conditions": [
        { "type": "time_window", "start": "08:00", "end": "20:00" },
        { "type": "temp_high_band_c", "low_c": 24.0, "high_c": 27.0 },
        { "type": "rh_high_band", "low": 55, "high": 65 }
      ]
    },
    {
      "id": 3,
      "conditions": [
        { "type": "rh_low_band", "low": 50, "high": 60 }
      ]
    },
    {
      "id": 4,
      "conditions": [
        {
          "type": "interval",
          "run_mins": 15,
          "every_hrs": 4,
          "window": { "start": "08:00", "end": "20:00" }
        }
      ]
    }
  ]
}
```

**`id`** = outlet number 1-4 (matches the outlet numbering above)

**Allowed conditions per outlet assignment:**

| Outlet assignment | Available conditions |
|-------------------|----------------------|
| Light | `time_window` or mutually exclusive `always_on` |
| Fan | Any combination of `time_window`, `temp_high_band_c`, `rh_high_band`, or mutually exclusive `always_on` |
| Humidifier | `rh_low_band` |
| Dehumidifier | `rh_high_band` |
| Heater | `temp_low_band_c` |
| AC Controller | `temp_high_band_c` |
| Water Pump | one `interval` per Water Pump outlet |

Command Center should use this table to decide schedule-control visibility.
Unsupported condition controls are hidden rather than shown disabled. Editors
should present newly assigned outlets with no selected conditions; persisted
schedule entries include only the conditions the user chooses. Firmware still
validates incoming schedule payloads and rejects unsupported combinations. The
local web UI can pause a saved outlet rule with `sched_dis` without deleting
its persisted schedule conditions.

**Condition fields:**

| Type | Fields |
|------|--------|
| `always_on` | none |
| `time_window` | `start`, `end` as `HH:MM` |
| `rh_low_band` / `rh_high_band` | `low`, `high` |
| `temp_low_band_c` / `temp_high_band_c` | `low_c`, `high_c` |
| `interval` | `run_mins`, `every_hrs`, optional `window: {"start":"HH:MM","end":"HH:MM"}` |

Validation and runtime notes:

- Top-level `outlets` must contain at least one schedule entry; empty schedules are rejected.
- Each scheduled outlet must include at least one condition.
- One condition of each supported type is allowed per outlet.
- `always_on` is mutually exclusive with all other conditions.
- Condition validity is based on the outlet's current assignment.
- Changing an outlet assignment clears that outlet's schedule entry without creating a replacement/default schedule; in auto mode, that outlet turns OFF immediately, while manual mode leaves relay outputs unchanged.
- The new outlet assignment remains without an active schedule entry until the user or Command Center saves a schedule for it.
- Humidity bands use `10`-`95` with at least a 2% gap.
- Temperature bands are stored in Celsius, display in the user's selected unit, and require at least a one display-degree gap.
- Time windows may cross midnight, are start-inclusive and end-exclusive, and reject equal start/end.
- Sensor-based conditions fail inactive when the required reading is invalid, unavailable, or older than 120 seconds.
- When any active AUTO schedule depends on temp/rH and sensor data is invalid, unavailable, or stale, Command Center should show a top-level warning such as `Sensor data unavailable; temp/rH automation is paused`.
- The warning is retained as part of `schedule/state`, so Command Center should show it immediately after subscribe if the retained state is non-empty.
- `sensor_warning` appearance or clearance publishes `schedule/state` immediately even when relay outputs are unchanged; Command Center should update the banner from that warning-only state change.
- In manual mode, Command Center may still mark invalid sensor telemetry near the readings, but should not show the automation-paused `sensor_warning` banner.
- Stale sensor data disables only the affected sensor condition; other valid active conditions, such as a fan `time_window`, can still authorize the outlet.
- When another condition keeps the outlet ON despite stale sensor data, the single-line outlet summary should say something like `ON - time active; sensor data unavailable`.
- When no other condition authorizes the outlet, the single-line outlet summary should say `waiting for sensor data`.
- If stale sensor data removes the last active condition authorizing an outlet, auto mode turns that outlet OFF at the next 30-second schedule evaluation and publishes `schedule/state`.
- On stale-sensor recovery, environmental conditions reset as inactive and evaluate from the recovered reading; a reading already beyond the ON threshold may activate at the next schedule evaluation, while a reading inside the configured band waits for a threshold crossing.
- If a Water Pump interval has an allowed-hours window, the window duration must be at least `run_mins`.
- Multiple outlets may use the Water Pump assignment; each may have one `interval` condition with independent interval state, due/blocked state, `Run Now` eligibility, and status summary.
- Multiple pump outlets may run simultaneously when their independent interval state or `Run Now` actions overlap. Firmware does not provide a global pump queue or one-pump-at-a-time lock.
- Fan turns on when any enabled condition is active and turns off only after all enabled conditions are inactive.
- Water Pump `Run Now` is a schedule-owned action for AUTO mode, not a direct relay write, and obeys the optional interval window.
- Invalid schedules are rejected on `schedule/error`; the previous firmware schedule remains active.

Schedule is persisted to NVS — survives reboot even without CC connected.

---

## REST / HTTP Endpoints (AP at 192.168.4.1)

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/` | Full config UI (HTML) |
| GET/POST | `/save` | Form submissions (schedule uses POST; short actions/settings may use GET query params) |
| GET | `/scan` | WiFi scan -> JSON array `[{"ssid":"...","rssi":-60},...]` |
| GET | `/status` | Device status JSON (see below) |
| GET | `/savetime?epoch=N` | Set device time from browser |

### `/status` JSON Response

```json
{
  "mac": "AABBCCDDEEFF",
  "fw": "1.0.0C",
  "wifi": true,
  "mqtt": true,
  "recovery_mode": false,
  "temp": 24.1,
  "rh": 58.2,
  "co2": 850,
  "light": 75,
  "unit": "C",
  "time": "12:00:00 PM (SNTP)",
  "time_valid": true,
  "time_source": "sntp",
  "sntp_status": "synced",
  "time_warning": "",
  "sensor_warning": "",
  "warnings": [],
  "relays": {
    "o1": false,
    "o2": true,
    "o3": false,
    "o4": false
  },
  "outlet_status": [
    { "id": 1, "state": "off", "summary": "" },
    { "id": 2, "state": "on", "summary": "temp active; off at 75.2\u00b0F" },
    { "id": 3, "state": "off", "summary": "" },
    { "id": 4, "state": "off", "summary": "" }
  ],
  "mode": "auto",
  "name": "GrowHub_B2C3"
}
```

`recovery_mode: true` means the device is in WiFi Recovery Mode (scanning hourly for configured SSID after retry exhaustion). CC should surface this state.
`time_warning` is non-empty when the clock or configured time source needs user attention, including in manual mode. For example, if browser sync makes wall time valid while SNTP is still pending, CC should warn that the device may drift after long runs or power loss. It is retained in `schedule/state` and publishes immediately when it appears or clears, even if relay outputs do not change. CC should only use automation-paused wording when an active AUTO wall-clock schedule cannot run.
Firmware starts SNTP during boot/config apply and restarts it when WiFi STA receives an IP. The drift-only `time_sntp_unhealthy` warning is suppressed for the first hour after SNTP start/restart; after a successful sync, firmware treats SNTP as unhealthy if the last successful sync becomes stale after three SNTP poll intervals, which is about three hours with the current one-hour poll interval. `time_sync_required` remains immediate when no valid wall time blocks automation.
`sensor_warning` is non-empty when active AUTO temp/rH automation is paused because the required sensor data is invalid, unavailable, or stale. It is empty in manual mode.
If both warnings are non-empty, CC should render both in one compact warning area, ordered by severity rather than hiding one.
`warnings` mirrors warning state with stable codes for client logic. Entries have `code`, `message`, `severity`, and optional `outlets`; CC should not parse the human-readable strings to determine behavior. CC may replace `message` with product-specific copy derived from `code`, and can use numeric physical outlet IDs in `outlets` to mark affected outlet rows after resolving current labels or assignments from outlet state. `time_sntp_unhealthy` should be treated as device-wide even when active AUTO wall-clock schedules exist.
`outlet_status[].summary` is firmware-owned display text; CC should not parse it for condition state.

---

## Control Mode Behavior

| Mode | Relay control | CC schedule | Web UI schedule |
|------|---------------|-------------|-----------------|
| `auto` | Schedule engine; direct relay writes rejected | CC `grow` topic pushes override it | Editable; publishes mirrored state |
| `manual` | Web UI / `control/relay` topic | Ignored | Always editable |

**Mirrored control rule:** MQTT connectivity does not lock local controls.
Firmware web UI edits and Command Center `grow` writes both update the same
persisted active device schedule. The firmware publishes the result to
`schedule/state`; Command Center mirrors that state. Conflict policy is
last accepted write wins, with firmware-published state treated as the runtime
source of truth.

Clearing the active schedule in auto mode immediately turns scheduled outlets
OFF and publishes `schedule/state`. Clearing the active schedule in manual mode
removes the saved automation but leaves relay outputs unchanged.

Empty schedule writes are rejected and are not treated as clear commands. Use
the explicit clear action when the intended result is no saved schedule.

Changing an outlet assignment clears that outlet's schedule entry, does not
create a replacement/default schedule, and publishes `schedule/state`. The new
assignment remains without an active schedule entry until the user or Command
Center saves a schedule for it. In auto mode, that outlet turns OFF immediately;
in manual mode, relay outputs are left unchanged.

Manual relay overrides require manual mode. If Command Center needs to directly
set relay state, it must publish `"2"` to `control/mode` before publishing a
`control/relay` bitmask. Relay writes in auto mode are rejected and reported on
`control/error`.

Water Pump `Run Now` should use the firmware schedule-owned action, not
`control/relay` while the device is in auto mode. It is available only for an
active Water Pump interval schedule. Command Center triggers it by publishing
`{"action":"pump_run_now","outlet":4}` to `growhub/<MAC>/schedule/action`.
Command Center must publish this command with QoS `1` and retained `false`;
retained `Run Now` commands must not be used. Multiple Water Pump outlets may
have active interval schedules; interval state, due/blocked state, `Run Now`
eligibility, and status summary are tracked per outlet, and the `outlet` field
selects exactly one pump outlet. Pump outlets are not globally serialized;
overlapping automatic or `Run Now` runs may run simultaneously, and firmware
does not queue a pump run behind another active pump run. Accepted actions
start one immediate run, publish `schedule/state`, and leave auto mode and the
saved schedule unchanged. Rejected actions publish `schedule/error` with the fixed
reason enum: `pump_schedule_required`, `auto_mode_required`,
`pump_window_ineligible`, or `time_sync_required`. Command
Center should confirm success from `schedule/state` or failure from
`schedule/error`. If `pump_run_now` arrives while that pump is already running,
firmware treats it as an idempotent no-op success and does not extend the run,
restart the run, reset the interval timer, or publish `schedule/error`. The
local web UI should mirror this by showing `Running` or disabling `Run Now`
while active; if a request still reaches firmware, it follows the same no-op
success path. A successful `Run Now` counts as the interval run; the next
automatic run is scheduled from the `Run Now` start time, subject to the
allowed-hours window. On boot, schedule load, or accepted edit to a pump
interval condition, that pump interval waits one full `every_hrs` interval
before the next automatic run.
Command Center should use `Run Now` for intentional immediate watering. If the
interval has an allowed-hours window, pump runs may start only when the full
`run_mins` duration fits inside that window. Command Center should keep `Run
Now` visible but disabled outside that window, too close to the end of the
window, or when valid wall time is missing, and show either the next available
window time or that time sync is needed. A due pump run blocked by the
allowed-hours window remains due; the interval timer resets only when the pump
actually starts. In manual mode, Command Center should use direct relay controls
instead.

Switching from auto to manual preserves current relay outputs. Command Center
should use `"7"` when the intended action is all-off, and `"2"` when the
intended action is to take over from the current scheduled state.

Switching from manual to auto evaluates the active firmware schedule
immediately, updates relay outputs, and publishes `schedule/state`. Command
Center should not assume a 30-second grace period after publishing `"3"`.
Outlets without active schedule entries are turned OFF during AUTO evaluation.

The `"7"` control mode command is an all-off manual override. It turns all
relays off, switches the device to manual mode, persists manual mode, and leaves
the saved schedule intact until the user or Command Center switches back to
auto.

Manual relay output state is not restored after reboot. Relays initialize OFF.
If the persisted mode is auto, the schedule may turn outlets on after
evaluation. If the persisted mode is manual, outlets remain OFF until the user
or Command Center sends a direct relay command or switches back to auto.

---

## LED Status Patterns

The device shows the highest-priority active state.

| Priority | Pattern | Meaning |
|----------|---------|---------|
| 1 | 3 fast pulses + 1.8s pause | **WiFi Recovery Mode** — scanning for configured SSID hourly |
| 2 | 2 fast pulses + 1.8s pause | Active AUTO schedule needs valid wall time, but time has not been set |
| 3 | Fast blink (200ms on/off) | AP-only mode — no WiFi credentials or manually disconnected |
| 4 | Slow blink (1s on/1s off) | WiFi connected, MQTT down or disabled |
| 5 | Solid ON | WiFi connected + MQTT connected |

The time-needed pattern is used only when a configured AUTO schedule contains a wall-clock condition such as a light/fan time window or pump allowed window. Sensor-based conditions and pump intervals without an allowed window can still run without valid wall time.
Sensor-data warnings do not add a new LED pattern in v1; CC and the local web UI surface them through top-level warnings and outlet summaries.

---

## Button Behavior

| Hold duration | Action |
|--------------|--------|
| Short press (<500ms) | Unused |
| 3-second hold (3000–4999ms) | WiFi Recovery Mode toggle: in recovery → exit to AP-only (keep creds); in AP-only with creds → enter recovery |
| 5-second hold (≥5000ms) | Factory reset (erases NVS, reboots) |

---

## WiFi Recovery Mode

Triggered automatically after 10 failed connection retries when the configured SSID is not visible in scan. Behavior:
- Performs a WiFi scan every **1 hour**
- When configured SSID reappears in scan → reconnects automatically
- Cleared on successful reconnect
- User can exit via 3s button hold (stays AP-only, keeps creds)
- User can re-engage via 3s button hold from AP-only state

---

## Config Stored in NVS

Key NVS fields (namespace `"growhub"`):

| Key | Type | Description |
|-----|------|-------------|
| `sta_ssid` | string | Home WiFi SSID |
| `sta_pass` | string | Home WiFi password |
| `ap_ssid` | string | Device AP name |
| `mqtt_host` | string | CC broker IP |
| `mqtt_port` | u16 | CC broker port (default 1883) |
| `mqtt_dis` | u8 | Soft-disable flag (1=disabled, 0=enabled) |
| `relay_mode` | u8 | Persisted relay mode (0=AUTO, 1=MANUAL) — survives reboot |
| `relay_0`–`relay_3` | string | Outlet device names (slot 0=bit0=O2, 1=bit1=O3, 2=bit2=O4, 3=bit3=O1) |
| `sched_json` | blob | Persisted schedule JSON v3 |
| `sched_dis` | u8 | Local web UI schedule-disable mask (bit0=O1 through bit3=O4) |
| `timezone` | string | POSIX TZ string |
| `time_src` | u8 | Time source (0=SNTP, 1=manual browser-set time) |
| `sntp_primary` | string | Primary SNTP server hostname |
| `sntp_secondary` | string | Secondary SNTP server hostname |
| `pin_outlet1`–`pin_outlet4` | u8 | Outlet relay GPIOs (defaults 33, 25, 26, 27) |

---

## OTA Update

URL-based only (no binary upload). Device must be on home WiFi (APSTA mode).

```
# On Mac:
cd firmware && .venv/bin/pio run
python3 -m http.server 8080
ipconfig getifaddr en0        # e.g. <host-ip>

# On web UI (192.168.4.1 → Firmware Update):
http://<host-ip>:8080/.pio/build/growhub/firmware.bin
```

---

## Known CC-Firmware Gaps (to fix in CC polish phase)

1. **CC scheduling model** — CC must use firmware-side scheduling as the normal path by publishing v3 schedules to `growhub/<MAC>/grow`, switching devices to AUTO mode, and mirroring `growhub/<MAC>/schedule/state`. Server-side relay automation should not be the normal schedule execution path.
2. **CC local-edit display** — If firmware-local edits appear on `schedule/state`, CC should show them as the device's active schedule without silently mutating the original named schedule template.
3. **"Open Command Center" link** — The CC URL is not stored on the device; firmware UI cannot link to it yet
4. **Outlet profile sync** — Firmware outlet device types and CC outlet profiles are not mirrored yet; use a future retained state topic rather than MQTT connectivity lockouts.
5. **`relay_mode` semantics** — Firmware-side schedules require `"3"` (auto); schedules persist in firmware NVS after the `grow` payload is accepted. `control/relay` remains a manual-mode override path, and `"7"` is an all-off manual override that persists manual mode.
