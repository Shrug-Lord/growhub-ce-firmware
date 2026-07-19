# Command Center Integration Reference

*Last updated: 2026-07-14 (frozen Command Center MQTT baseline) | Firmware version: 1.1.0C*

Use this doc to brief the companion Command Center project without reading the full firmware repo.

Normal users do not need this page. Start with [INSTALL.md](INSTALL.md) instead.

---

## Hardware

- **Target:** Niwa Growhub and Growhub+ — ESP32-D0WDQ6 rev 1.0/1.1 class hardware
- **MAC format:** 12 uppercase hex characters; do not assume a universal OUI
- **Default AP:** `growhub_<last4mac>` — open; remains active by default, or
  becomes a five-minute connectivity fallback when the user disables the
  always-active preference

### Confirmed GPIO Pins

| Pin | Function |
|-----|----------|
| 33 | Outlet 1 (bit3) |
| 25 | Outlet 2 (bit0) |
| 26 | Outlet 3 (bit1) |
| 27 | Outlet 4 (bit2) |
| 16 | Sensor UART RX (SH_NP01) |
| 17 | Sensor UART TX (SH_NP01) |
| 0  | ROM-download Boot pad |
| 4  | Front setup button (active-low) |
| 12 | Blue/green operation LED (active-low) |
| 14 | Red malfunction LED (active-low) |

---

## Outlets & Relay Bitmask

**4 outlets total.** The bitmask is a 4-bit value (0–15):

| Bit | Outlet | Default assignment |
|-----|--------|---------------------|
| bit3 (value 8) | Outlet 1 | *(unassigned)* |
| bit0 (value 1) | Outlet 2 | *(unassigned)* |
| bit1 (value 2) | Outlet 3 | *(unassigned)* |
| bit2 (value 4) | Outlet 4 | *(unassigned)* |

Firmware stores outlet assignments separately from user-facing outlet labels.
Assignments default to unassigned (`None` on MQTT). Labels default to the
published fallback `Outlet N`. Available assignment values: `None`, `Light`,
`Fan`, `Humidifier`, `Dehumidifier`, `Water Pump`, `Heater`, `AC Controller`.

Command Center should treat CE firmware as the source of truth for outlet
assignments and labels. Subscribe to retained `growhub/<MAC>/outlets/state`
before preflighting schedule templates, and write assignment or label changes
through `growhub/<MAC>/outlets/config`.

Command Center's `1.1.0C` contract models all four physical outlets and uses
firmware-owned assignments and labels for schedule-template preflight.

---

## MQTT Interface

**Broker:** User-configured host:port. Soft-disable supported (flag in NVS -- broker config preserved, client won't connect).

All topics use MAC as the device identifier: `growhub/<MAC>/...`

### Device -> CC (publishes)

| Topic | Payload | Notes |
|-------|---------|-------|
| `growhub/<MAC>/sensor/live` | JSON (see below) | Published on schedule (default 6s) |
| `growhub/<MAC>/status` | `"online"` / `"offline"` | Retained, last-will = `"offline"` |
| `growhub/<MAC>/outlets/state` | JSON | Retained outlet assignment and label state from firmware |
| `growhub/<MAC>/outlets/error` | JSON | Rejected outlet config writes |
| `growhub/<MAC>/schedule/state` | JSON | Retained active schedule mirror from firmware |
| `growhub/<MAC>/schedule/error` | JSON | Rejected schedule writes/actions |
| `growhub/<MAC>/time/error` | JSON | Rejected time actions |
| `growhub/<MAC>/control/error` | JSON | Rejected control commands |

**Sensor:** Niwa SH_NP01 sensor board -- UART, 9600 baud, GPIO 16/17. The
verified hardware provides temperature, humidity, and light without CO2.
Firmware reserves an optional `c2` field for a future verified CO2-capable
variant, but clients must not require it.

**Sensor payload format:**
```json
{
  "nId": "AABBCCDDEEFF",
  "name": "GrowHub_B2C3",
  "fw": "1.1.0C",
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

**Outlet assignment and label state payload:**
```json
{
  "v": 1,
  "source": "local",
  "outlets": [
    { "id": 1, "assignment": "Light", "label": "Canopy Light" },
    { "id": 2, "assignment": "Fan", "label": "Exhaust Fan" },
    { "id": 3, "assignment": "Fan", "label": "Circulation Fan" },
    { "id": 4, "assignment": "Water Pump", "label": "Reservoir Pump" }
  ]
}
```
- Retained on `growhub/<MAC>/outlets/state`
- Published on MQTT reconnect, accepted `outlets/config` writes, and local firmware assignment or label changes
- `source` is informational: `local`, `mqtt`, or `reconnect`
- `outlets[].id` is the physical outlet ID `1`-`4`, not the relay bit slot
- `outlets[].assignment` is one of `None`, `Light`, `Fan`, `Humidifier`, `Dehumidifier`, `Water Pump`, `Heater`, `AC Controller`
- `outlets[].label` is the firmware-owned user-facing outlet label; empty stored labels publish as `Outlet N`
- Command Center should use retained `outlets/state` as the firmware-owned source of truth before showing schedule controls or loading schedule templates, and use labels to disambiguate duplicate assignments

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
- `source` is informational: `local`, `mqtt`, `time`, or `reconnect`
- `outlet_status` always includes all four outlets; summaries are empty in manual mode, for unassigned outlets, and for outlets with no active schedule entry
- `outlet_status[].summary` is firmware-owned display text, not a structured reason API. CC should display it as text and use `warnings[].code` plus `warnings[].outlets` for stable warning logic.
- Time health fields let CC warn when wall-clock schedules are paused or SNTP is unhealthy
- `time_warning` is general device time health and may be non-empty in manual mode. CC should use sync or drift wording for unhealthy time sources, and reserve automation-paused or waiting-for-time wording for active AUTO wall-clock schedules that cannot run.
- Because `schedule/state` is retained, CC should treat retained `time_warning` as current automation state on subscribe and clear the banner when the retained state publishes `time_warning: ""`.
- `sensor_warning` is non-empty when an active AUTO schedule depends on unavailable or stale temp/rH data; CC should show it as a top-level warning. It is empty in manual mode.
- Because `schedule/state` is retained, CC should treat retained `sensor_warning` as current automation state on subscribe and clear the banner when the retained state publishes `sensor_warning: ""`.
- If both warnings are non-empty, CC should show both in one compact warning area rather than choosing a single highest-priority banner. Order warnings by severity: active AUTO wall-clock automation blocked first, active AUTO temp/rH automation paused second, and drift-only or sync-health time warnings after automation-blocking warnings.
- `warnings` contains machine-readable warning entries with `code`, `message`, `severity`, and optional `outlets`, published in display order. CC should use `warnings[].code` for logic and `warnings[].outlets` to highlight affected outlets. Omitted or empty `outlets` means device-wide. Warning `outlets` are numeric physical outlet IDs only; CC should resolve labels or assignments from retained `outlets/state` rather than expecting copied display names in warning entries. `time_sync_required` and `sensor_data_unavailable` include affected outlets when automation is blocked or paused; `time_sntp_unhealthy` stays device-wide because it is a drift/sync risk, not a specific outlet block. `message` is firmware-owned default display copy for the local web UI and simple clients; CC may render its own product-specific copy from `code` while preserving the warning meaning and severity.
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

**Outlet assignment error payload:**
```json
{
  "reason": "invalid_assignment",
  "outlet": 4,
  "detail": "optional debugging text"
}
```
- Published on `growhub/<MAC>/outlets/error`
- Command Center should leave the previous mirrored `outlets/state` active when an outlet config write is rejected
- `reason` is a fixed v1 enum. CC should branch on `reason`, not `detail`; unknown future reasons should fall back to a generic rejected-outlet-config message.
- Outlet reasons: `invalid_payload`, `unsupported_outlet_config_version`, `missing_outlets`, `invalid_outlet`, `duplicate_outlet`, `invalid_assignment`, `invalid_label`, `write_failed`

**Time action error payload:**
```json
{
  "command": "time/action",
  "reason": "invalid_epoch"
}
```
- Published on `growhub/<MAC>/time/error`
- Command Center should leave the previous mirrored `schedule/state` active when a time action is rejected
- `reason` is a fixed v1 enum. CC should branch on `reason`; unknown future reasons should fall back to a generic rejected-time-action message.
- Time reasons: `invalid_payload`, `unsupported_time_action_version`, `unsupported_action`, `invalid_epoch`

### CC → Device (subscribes)

| Topic | Payload | Effect |
|-------|---------|--------|
| `growhub/<MAC>/control/mode` | `"2"` = manual, `"3"` = auto, `"7"` = all off + manual | Sets relay mode |
| `growhub/<MAC>/control/relay` | Decimal bitmask string `"0"`–`"15"` | Sets relay state (manual mode) |
| `growhub/<MAC>/schedule/action` | JSON `{"action":"pump_run_now","outlet":4}` | Runs schedule-owned actions |
| `growhub/<MAC>/time/action` | JSON `{"v":1,"action":"sync_epoch","epoch":1780000000}` | Sets current wall time without changing time config |
| `growhub/<MAC>/outlets/config` | Outlet assignment/label JSON v1 (see below) | Replaces all firmware outlet assignments and labels |
| `growhub/<MAC>/config` | JSON `{"tZ":"...", "timeSrc":"sntp", "sntpPrimary":"pool.ntp.org", "sntpSecondary":"time.nist.gov", "tmpOff":0, "rhOff":0}` | Updates time settings / temp/rH calibration offsets |
| `growhub/<MAC>/grow` | Schedule JSON v3 (see below) | Loads and persists schedule |
| `growhub/<MAC>/ota` | URL string | Triggers OTA update from URL |

---

## Outlet Assignment And Label Writes

Command Center assigns and labels outlets by publishing a full replacement document to
`growhub/<MAC>/outlets/config` with QoS `1` and retained `false`.

```json
{
  "v": 1,
  "outlets": [
    { "id": 1, "assignment": "Light", "label": "Canopy Light" },
    { "id": 2, "assignment": "Fan", "label": "Exhaust Fan" },
    { "id": 3, "assignment": "Fan", "label": "Circulation Fan" },
    { "id": 4, "assignment": "Water Pump", "label": "Reservoir Pump" }
  ]
}
```

Rules:

- Include exactly one entry for each physical outlet ID `1`-`4`
- Use only the stable assignment values listed in `outlets/state`
- `label` is optional for backward-compatible writes; omitted or empty labels publish back as `Outlet N`
- Labels are trimmed, limited to 32 bytes, and must not contain ASCII control characters
- Treat retained `outlets/state` as confirmation of the accepted assignment/label set
- Treat `outlets/error` as rejection and leave the previous mirrored assignment/label state unchanged
- Do not publish partial patches; the firmware API is full replacement

Accepted assignment and label changes persist to firmware NVS. Label-only
changes publish retained `outlets/state` and do not clear schedules, publish
`schedule/state`, evaluate AUTO mode, or affect relay state. When an assignment
changes, firmware clears that outlet's schedule entry, clears its local pause
bit, publishes `outlets/state` plus `schedule/state`, and leaves the new
assignment without a default schedule. In auto mode, the affected outlet turns
OFF immediately if it no longer has an active schedule entry. In manual mode,
relay outputs are left unchanged.

---

## Time Sync Action

Command Center can set the device's current wall time over MQTT by publishing a
non-retained message to `growhub/<MAC>/time/action` with QoS `1`.

```json
{
  "v": 1,
  "action": "sync_epoch",
  "epoch": 1780000000
}
```

Rules:

- `v` must be `1`
- `action` must be `sync_epoch`
- `epoch` must be an integer Unix epoch accepted by firmware's wall-time sanity check
- This action sets current wall time only; keep timezone, `timeSrc`, and SNTP server settings on `growhub/<MAC>/config`
- The action does not change configured `timeSrc`, relay mode, timezone, or SNTP servers
- On success, firmware publishes retained `schedule/state` with updated time health and source `time`
- If the device is in AUTO, firmware evaluates the active schedule immediately after setting wall time
- On rejection, firmware publishes `growhub/<MAC>/time/error` and leaves current wall time unchanged

Command Center should use this MQTT action for remote time recovery instead of
depending on HTTP `GET /savetime?epoch=N`.

---

## Schedule Format (v3 — outlet conditions)

Command Center should send this format on the `grow` topic when a schedule is loaded onto a device. The firmware persists the schedule to NVS and evaluates it in AUTO mode, so the grow continues if Command Center, MQTT, or the local network connection later goes offline.

Required load sequence:

1. Subscribe to retained `growhub/<MAC>/outlets/state`
2. Preflight the template against firmware-owned outlet assignments and use labels to disambiguate duplicate assignments
3. Publish the v3 schedule JSON to `growhub/<MAC>/grow`
4. Publish `"3"` to `growhub/<MAC>/control/mode` to put the device in AUTO mode
5. Subscribe to `growhub/<MAC>/schedule/state` and mirror the retained state back into CC's active schedule display
6. Treat `control/relay` as a manual-mode override path, not the normal schedule execution path

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

Command Center should resolve outlet assignments from retained `outlets/state`
and use this table to decide schedule-control visibility. It should use labels
from the same state to distinguish duplicate assignments, such as two Fans or
two Water Pumps. Unsupported condition controls are hidden rather than shown
disabled. Editors should present newly assigned outlets with no selected
conditions; persisted schedule entries include only the conditions the user
chooses. Firmware still validates incoming schedule payloads against current
firmware-owned assignments and rejects unsupported combinations; labels are not
schedule validation inputs. The local web UI can pause a saved outlet rule with
`sched_dis` without deleting its persisted schedule conditions.

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
- Condition validity is based on the outlet's current firmware-owned assignment from `outlets/state`.
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
| GET | `/savetime?epoch=N` | Set device time from browser; Command Center should use MQTT `time/action` |

### `/status` JSON Response

```json
{
  "mac": "AABBCCDDEEFF",
  "fw": "1.1.0C",
  "wifi": true,
  "mqtt": true,
  "recovery_mode": false,
  "keep_ap_active": false,
  "ap_active": false,
  "ap_reason": "off",
  "ap_fallback_seconds": 0,
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

`ap_reason` is one of `off`, `provisioning`, `preference`, `fallback`, or
`manual_override`. While WiFi is disconnected and the AP is waiting on its
five-minute fallback delay, `ap_active` is `false` and
`ap_fallback_seconds` counts down. The legacy `recovery_mode` field remains
`true` for automatic fallback and manual override states.
`time_warning` is non-empty when the clock or configured time source needs user attention, including in manual mode. For example, if browser sync or MQTT `time/action` makes wall time valid while SNTP is still pending, CC should warn that the device may drift after long runs or power loss. It is retained in `schedule/state` and publishes immediately when it appears or clears, even if relay outputs do not change. CC should only use automation-paused wording when an active AUTO wall-clock schedule cannot run.
Firmware starts SNTP during boot/config apply and restarts it when WiFi STA receives an IP. The drift-only `time_sntp_unhealthy` warning is suppressed for the first hour after SNTP start/restart; after a successful sync, firmware treats SNTP as unhealthy if the last successful sync becomes stale after three SNTP poll intervals, which is about three hours with the current one-hour poll interval. `time_sync_required` remains immediate when no valid wall time blocks automation.
`sensor_warning` is non-empty when active AUTO temp/rH automation is paused because the required sensor data is invalid, unavailable, or stale. It is empty in manual mode.
If both warnings are non-empty, CC should render both in one compact warning area, ordered by severity rather than hiding one.
`warnings` mirrors warning state with stable codes for client logic. Entries have `code`, `message`, `severity`, and optional `outlets`; CC should not parse the human-readable strings to determine behavior. CC may replace `message` with product-specific copy derived from `code`, and can use numeric physical outlet IDs in `outlets` to mark affected outlet rows after resolving current labels or assignments from retained `outlets/state`. `time_sntp_unhealthy` should be treated as device-wide even when active AUTO wall-clock schedules exist.
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
create a replacement/default schedule, and publishes `outlets/state` plus
`schedule/state`. The new assignment remains without an active schedule entry
until the user or Command Center saves a schedule for it. In auto mode, that
outlet turns OFF immediately; in manual mode, relay outputs are left unchanged.

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

The blue/green operation LED and red malfunction LED report independently.

| LED | Pattern | Meaning |
|-----|---------|---------|
| Operation | 3 fast pulses + 1.8s pause | Setup AP active through automatic fallback or the physical-button override |
| Operation | Fast blink (200ms on/off) | WiFi disconnected; setup AP fallback timeout is running |
| Operation | Slow blink (1s on/1s off) | WiFi connected; configured/enabled MQTT broker is disconnected |
| Operation | Solid ON | WiFi connected; MQTT is connected or intentionally not enabled |
| Red malfunction | 2 fast pulses + 1.8s pause | Active AUTO schedule needs valid wall time, but time has not been set |
| Red malfunction | OFF | No blocking wall-time warning |

The time-needed pattern is used only when a configured AUTO schedule contains a wall-clock condition such as a light/fan time window or pump allowed window. Sensor-based conditions and pump intervals without an allowed window can still run without valid wall time.
Sensor-data warnings do not add a new LED pattern in v1; CC and the local web UI surface them through top-level warnings and outlet summaries.

The operation LED may be disabled per device from the local firmware's
hardware-override page when a suspected GPIO or LED-circuit fault could
interfere with normal device startup or operation.
This does not change firmware status, MQTT health, or the red malfunction LED;
it only leaves GPIO 12 inactive. Command Center must not treat operation-LED
visibility as a device-health signal.

The operation-LED override remains local to the firmware web interface in
`1.1.0C`. The expanded local diagnostics page and sanitized JSON support export
planned for `1.2.0C` do not add MQTT topics or fields. Command Center must not
depend on either surface as part of the frozen `1.1.0C` contract.

---

## Button Behavior

| Hold duration | Action |
|--------------|--------|
| Short hold (<3000ms) | Unused |
| 3-second hold (3000–4999ms) | Toggle the session-only setup AP override; the override clears on reboot |
| 5-second hold (≥5000ms) | Factory reset (erases NVS, reboots) |

---

## Setup AP fallback

When `keep_ap=0`, configured devices start in station-only mode. Five
continuous minutes without a station IP activates the setup AP while station
recovery continues once per minute. A successful connection disables the AP
immediately. A three-second button hold enables a session-only override even
while station WiFi is healthy; another hold or reboot clears the override.

---

## Config Stored in NVS

Key NVS fields (namespace `"growhub"`):

| Key | Type | Description |
|-----|------|-------------|
| `sta_ssid` | string | Home WiFi SSID |
| `sta_pass` | string | Home WiFi password |
| `ap_ssid` | string | Device AP name |
| `keep_ap` | u8 | `1` keeps the setup AP active; `0` uses five-minute fallback |
| `mqtt_host` | string | CC broker IP |
| `mqtt_port` | u16 | CC broker port (default 1883) |
| `mqtt_dis` | u8 | Soft-disable flag (1=disabled, 0=enabled) |
| `relay_mode` | u8 | Persisted relay mode (0=AUTO, 1=MANUAL) — survives reboot |
| `relay_0`–`relay_3` | string | Outlet assignments (slot 0=bit0=O2, 1=bit1=O3, 2=bit2=O4, 3=bit3=O1) |
| `label_0`–`label_3` | string | Outlet labels using the same slot mapping as `relay_0`–`relay_3`; empty means publish fallback `Outlet N` |
| `sched_json` | blob | Persisted schedule JSON v3 |
| `sched_dis` | u8 | Local web UI schedule-disable mask (bit0=O1 through bit3=O4) |
| `timezone` | string | POSIX TZ string |
| `time_src` | u8 | Time source (0=SNTP, 1=manual external-set time) |
| `sntp_primary` | string | Primary SNTP server hostname |
| `sntp_secondary` | string | Secondary SNTP server hostname |
| `pin_outlet1`–`pin_outlet4` | u8 | Outlet relay GPIOs (defaults 33, 25, 26, 27) |

The outlet assignment/label sync and MQTT `time/action` are additive public
contracts included in the frozen `1.1.0C` minor-release scope.

---

## OTA Update

Command Center-triggered OTA is URL-based. The device must be able to route to
the firmware host over its active station connection; it may be running
STA-only or AP+STA. The local device web UI separately supports direct
`firmware.bin` upload.

```
# On Mac:
cd firmware && .venv/bin/pio run
python3 -m http.server 8080
ipconfig getifaddr en0        # e.g. <host-ip>

# On the device web UI (LAN IP or 192.168.4.1 → Firmware Update):
http://<host-ip>:8080/.pio/build/growhub/firmware.bin
```

---

## Command Center Release Status

The companion implementation uses firmware-owned schedules, retained outlet
and schedule mirrors, typed confirmed actions, local-edit drift handling, and
the documented AUTO/MANUAL semantics. Its first release remains blocked on the
`CE-1.1.0C` hardware-contract checklist and host-deployment evidence.

The runnable checklist is maintained in the companion repository at
`docs/release-evidence/CE-1.1.0C.md`. Complete it only after both repositories
have exact commits: build and hash the firmware once, install that same
`firmware.bin` on the test controllers, deploy Command Center from its recorded
commit, exercise every checklist item, and save sanitized representative MQTT
request/state/error payloads. Record the firmware commit and SHA-256, Command
Center commit, pinned broker image/version, device variants, tester, and date.
The companion `RELEASE_TAG=v0.1.0 npm run release:validate` command must pass
with no unchecked evidence items before the release gate is considered closed.

The firmware does not store the Command Center URL, so the local device UI does
not currently offer an **Open Command Center** link.
