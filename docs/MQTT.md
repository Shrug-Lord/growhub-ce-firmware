# Growhub CE Firmware MQTT Reference

MQTT is optional in Community Edition firmware. A single Growhub runs fully standalone with WiFi + the built-in web UI. MQTT exists to integrate with a local broker and, optionally, a separate fleet-management companion such as Growhub Command Center.

This document describes the public MQTT interface targeted for the first CE release: topic schema, payloads, schedule format, and the NVS keys that affect MQTT behavior.

## Transport and connection model

- Protocol: plain MQTT over TCP
- Broker URI format: `mqtt://<host>:<port>`
- TLS: not implemented in current firmware
- Username/password auth: not implemented in current firmware
- Default port: `1883`
- Client ID: device MAC string, uppercase hex without separators, for example `AABBCCDDEEFF`
- Keepalive: `30` seconds

Connection is attempted only when both are true:

- `mqtt_host` is configured
- `mqtt_dis` is `0` (not soft-disabled)

If `mqtt_host` is empty, the device behaves as standalone-only and skips MQTT initialization.

## Device identity and topic layout

All MQTT topics are namespaced by the device MAC:

```text
growhub/<MAC>/...
```

Example:

```text
growhub/AABBCCDDEEFF/sensor/live
```

The public CE contract uses these topic names:

- `growhub/<MAC>/sensor/live`
- `growhub/<MAC>/status`
- `growhub/<MAC>/schedule/state`
- `growhub/<MAC>/schedule/action`
- `growhub/<MAC>/schedule/error`
- `growhub/<MAC>/control/error`
- `growhub/<MAC>/control/mode`
- `growhub/<MAC>/control/relay`
- `growhub/<MAC>/config`
- `growhub/<MAC>/grow`
- `growhub/<MAC>/ota`

## Publish behavior

### `growhub/<MAC>/status`

Device presence topic.

- Payload when connected: `online`
- Last-will payload: `offline`
- QoS: `1`
- Retained: yes

### `growhub/<MAC>/schedule/state`

Retained active schedule mirror published by the firmware.

- Published when MQTT connects/reconnects
- Published after accepted `grow` schedule writes
- Published after local web UI schedule save/clear
- Published after relay mode changes from the web UI or MQTT
- Published when `time_warning` or `sensor_warning` appears or clears, even if relay outputs do not change
- QoS: `1`
- Retained: yes

Payload shape:

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

When no schedule is active, `active` is `false` and `schedule` is `null`.
`source` is informational and currently uses `local`, `mqtt`, or `reconnect`.
Consumers should treat the firmware-published state as the device's active
runtime state.

Time fields:

- `time_valid`: wall-clock conditions can run
- `time_source`: `sntp` or `manual`
- `sntp_status`: `disabled`, `pending`, or `synced`
- `time_warning`: empty string or a user-facing device time-health warning; may be non-empty in MANUAL mode
- `sensor_warning`: empty string or a user-facing warning when an active AUTO schedule depends on unavailable or stale temp/rH data; always empty in MANUAL mode
- `warnings`: machine-readable warning entries, published in display order

`time_warning` is general device time health. Clients should show sync or drift wording when the configured time source is unhealthy, and reserve automation-paused or waiting-for-time wording for active AUTO wall-clock schedules that cannot run.

Firmware starts SNTP during boot/config apply and restarts SNTP whenever the
WiFi station receives an IP address. Firmware suppresses the drift-only
`time_sntp_unhealthy` warning for the first hour after SNTP starts or restarts.
This avoids noisy warnings after reboot, firmware update, WiFi recovery, or SNTP
config changes while SNTP has not yet completed a sync in the current boot.
After a successful sync, firmware treats SNTP as unhealthy if the last successful
sync becomes stale after three SNTP poll intervals, which is about three hours
with the current one-hour poll interval.
Blocking `time_sync_required` still appears immediately when active AUTO
wall-clock automation has no valid wall time.

Because `schedule/state` is retained, `time_warning` and `sensor_warning` are retained with the current automation state. Newly connected clients should show non-empty retained warnings immediately and clear them when the retained state publishes the warning field as an empty string.

If both warnings are non-empty, clients should show both in one compact warning area rather than choosing a single highest-priority banner. Order warnings by severity: active AUTO wall-clock automation blocked first, active AUTO temp/rH automation paused second, and drift-only or sync-health time warnings after automation-blocking warnings.

Warning entries keep client logic stable without parsing user-facing text:

```json
{
  "code": "sensor_data_unavailable",
  "message": "Sensor data unavailable; temp/rH automation is paused",
  "severity": "warning",
  "outlets": [2, 3]
}
```

`outlets` is optional. It lists affected physical outlet IDs when a warning applies to specific scheduled outlets. Omitted or empty `outlets` means the warning is device-wide. Warning entries do not copy outlet labels or assignments; clients resolve the numeric outlet IDs against the current outlet assignment/config state when they need display names. `time_sync_required` includes affected outlets when wall-clock automation is actually blocked, and `sensor_data_unavailable` includes affected outlets when temp/rH automation is paused. `time_sntp_unhealthy` stays device-wide because it is a drift/sync risk, not a specific outlet block.

`message` is firmware-owned default display copy for the local web UI and simple clients. Clients that need stable behavior must use `code`, not parse `message`. Companion apps may render their own product-specific copy from `code` while preserving the warning meaning and severity.

Initial warning codes:

| Code | Severity | Meaning |
|---|---|---|
| `time_sync_required` | `blocking` | Active AUTO wall-clock automation cannot run until valid time is set |
| `sensor_data_unavailable` | `warning` | Active AUTO temp/rH automation is paused because required sensor data is invalid, unavailable, or stale |
| `time_sntp_unhealthy` | `warning` | Configured SNTP has not synced, or its last successful sync is stale while time is otherwise valid |

`outlet_status` always includes all four outlets. `summary` is empty for
manual mode, unassigned outlets, and outlets without an active schedule entry.
`summary` is firmware-owned display text, not a structured reason API. Clients
should display it as text and use `warnings[].code` plus `warnings[].outlets`
for stable warning logic.

### `growhub/<MAC>/control/error`

Published when a control command is rejected.

- QoS: `1`
- Retained: no

Example:

```json
{
  "command": "control/relay",
  "reason": "manual_mode_required"
}
```

`reason` is a fixed v1 enum. Clients should branch on `reason`, not `detail`.
Unknown future reasons should fall back to a generic rejected-command message.

| Reason | Meaning |
|---|---|
| `invalid_payload` | Payload is empty or cannot be parsed for the command |
| `invalid_mode` | `control/mode` payload is not one of `"2"`, `"3"`, or `"7"` |
| `invalid_relay_mask` | `control/relay` payload is not a decimal bitmask from `0` to `15` |
| `manual_mode_required` | Direct relay writes are rejected while the device is in AUTO |

### `growhub/<MAC>/schedule/error`

Published when a schedule payload or schedule-owned action is rejected.

- QoS: `1`
- Retained: no

Example:

```json
{
  "reason": "condition_not_allowed",
  "outlet": 2,
  "detail": "condition not allowed for outlet assignment"
}
```

`reason` is a fixed v1 enum. Clients should branch on `reason`, not `detail`.
Unknown future reasons should fall back to a generic rejected-schedule or
rejected-action message.

| Reason | Meaning |
|---|---|
| `invalid_payload` | Payload is malformed JSON or missing fields required for a schedule write/action |
| `unsupported_schedule_version` | Schedule `v` is not `3` |
| `empty_schedule` | Top-level `outlets` is empty; clear must use the explicit clear action |
| `invalid_outlet` | Outlet ID is missing or outside `1`-`4` |
| `missing_conditions` | Scheduled outlet has no conditions |
| `duplicate_condition` | Outlet has more than one condition of the same type |
| `invalid_condition` | Condition type or fields are unknown or malformed |
| `condition_not_allowed` | Condition is valid but not allowed for the outlet's current assignment |
| `always_on_exclusive` | `always_on` is combined with another condition |
| `invalid_time_window` | Time window is malformed, out of range, or has equal start/end |
| `invalid_band` | Temperature or humidity band is out of range or below the minimum gap |
| `invalid_interval` | Pump interval `run_mins`, `every_hrs`, or optional window is invalid |
| `unsupported_action` | Schedule action is unknown |
| `auto_mode_required` | Schedule action requires AUTO mode |
| `pump_schedule_required` | `pump_run_now` target has no active Water Pump interval schedule |
| `time_sync_required` | Windowed action cannot run until valid wall time exists |
| `pump_window_ineligible` | Pump run cannot fit in the current allowed-hours window |

### `growhub/<MAC>/sensor/live`

Live telemetry payload published by the main sensor loop when MQTT is connected.

- Publish cadence: `report_interval_s` seconds, default `6`
- QoS: `0`
- Retained: no

Payload shape:

```json
{
  "nId": "AABBCCDDEEFF",
  "name": "GrowHub_B2C3",
  "fw": "1.0.0C",
  "data": [
    {
      "l": 75,
      "h": 58.2,
      "t": 24.1,
      "a": "01000000",
      "ts": "2026-05-31 12:00:00:000Z"
    }
  ]
}
```

Field notes:

- `nId`: device MAC string
- `name`: device name from NVS
- `fw`: `GROWHUB_VERSION`
- `l`: light percentage from the SH_NP01 sensor board
- `h`: relative humidity
- `t`: temperature in Celsius on the wire
- `a`: actuator string in fixed bit order
- `ts`: UTC timestamp formatted as `YYYY-MM-DD HH:MM:SS:000Z`
- `c2`: included only when `co2_valid` is true

Actuator string format:

```text
[outlet2][outlet3][outlet4][outlet1][0][0][0][0]
```

Bit mapping inside `a`:

- position 0: Outlet 2 / relay bit 0
- position 1: Outlet 3 / relay bit 1
- position 2: Outlet 4 / relay bit 2
- position 3: Outlet 1 / relay bit 3
- positions 4-7: reserved, currently always `0000`

Examples:

- Outlet 2 on only: `"10000000"`
- all off: `"00000000"`
- all four relays on: `"11110000"`

## Subscribe behavior

The device subscribes with QoS `1` to the following topics after broker connect.

### `growhub/<MAC>/control/mode`

Payload is a one-character command string:

- `"2"`: switch to manual mode
- `"3"`: switch to auto mode
- `"7"`: turn all relays off and switch to manual mode

Notes:

- Relay mode is persisted to NVS under `relay_mode`
- `"2"` preserves current relay outputs while entering manual mode; use `"7"` for an explicit all-off stop
- `"3"` evaluates the active schedule immediately and publishes `schedule/state`; firmware does not wait for the next schedule tick after entering AUTO
- AUTO evaluation turns outlets without active schedule entries OFF
- `"7"` is a manual override: it sets relay state to all off, persists manual mode, and leaves the saved schedule intact
- Relay output bitmask is not persisted. On boot, relays start OFF; AUTO mode may turn them on after schedule evaluation, while MANUAL mode leaves them OFF until a direct relay command.

### `growhub/<MAC>/control/relay`

Payload is a decimal relay bitmask string in the range `0`-`15`.

Examples:

- `"0"`: all off
- `"1"`: Outlet 2 on
- `"8"`: Outlet 1 on
- `"15"`: all four outlets on

Bit mapping:

- bit 0: Outlet 2
- bit 1: Outlet 3
- bit 2: Outlet 4
- bit 3: Outlet 1

Direct relay writes are accepted only while the device is in manual mode. If a relay bitmask arrives while the device is in auto mode, firmware rejects the write, leaves relay state unchanged, and publishes a control error event.

### `growhub/<MAC>/schedule/action`

Payload is JSON for schedule-owned actions:

- QoS: `1`
- Retained: no

```json
{
  "action": "pump_run_now",
  "outlet": 4
}
```

Behavior:

- clients must publish action commands as non-retained messages
- `pump_run_now` is accepted only in AUTO mode for an outlet with an active Water Pump interval schedule
- multiple outlets may be assigned Water Pump and may each have an active interval schedule
- interval state, due/blocked state, `Run Now` eligibility, and status summary are tracked per outlet
- the `outlet` field selects exactly one pump outlet
- pump outlets are not globally serialized; overlapping automatic or `Run Now` runs may run simultaneously
- firmware does not queue a pump run behind another active pump run
- accepted actions start one immediate pump run and publish `schedule/state`
- duplicate `pump_run_now` actions for an already-running pump are idempotent no-op successes; they do not extend the run, restart the run, reset the interval timer, or publish `schedule/error`
- rejected actions publish `schedule/error` and leave relay state unchanged
- clients should confirm success from `schedule/state` or failure from `schedule/error`
- pump action rejection reasons use the fixed `schedule/error` enum: `pump_schedule_required`, `auto_mode_required`, `pump_window_ineligible`, or `time_sync_required`

### `growhub/<MAC>/config`

Payload is JSON. Current firmware recognizes these fields:

```json
{
  "tZ": "EST5EDT,M3.2.0,M11.1.0",
  "timeSrc": "sntp",
  "sntpPrimary": "pool.ntp.org",
  "sntpSecondary": "time.nist.gov",
  "tmpOff": 0,
  "rhOff": 0
}
```

Supported fields:

- `tZ`: POSIX timezone string
- `timeSrc`: `sntp` or `manual`
- `sntpPrimary`: primary SNTP server hostname
- `sntpSecondary`: secondary SNTP server hostname
- `tmpOff`: temperature calibration offset
- `rhOff`: humidity calibration offset

Behavior:

- Missing fields are left unchanged
- Time source, timezone, and SNTP server changes apply immediately
- Browser sync can set valid wall time without changing `timeSrc`
- Offsets are persisted as hundredths in NVS
- Device name, MQTT host, and relay names are not remotely configurable through this MQTT topic

### `growhub/<MAC>/grow`

Payload is a CE v3 outlet-condition schedule document.

On receipt, the schedule is:

1. validated against the current outlet assignments
2. loaded into the active schedule engine
3. persisted to NVS as `sched_json`
4. published back to `growhub/<MAC>/schedule/state`
5. restored again on boot if present

If validation fails, firmware leaves the previous active schedule unchanged and
publishes `growhub/<MAC>/schedule/error`.

### `growhub/<MAC>/ota`

Payload is a firmware URL string.

Behavior:

- The URL is copied into a 256-byte local buffer
- The current implementation passes the string directly to `ota_start_from_url()`
- No topic-level signature, auth, or allowlist is enforced by MQTT handling itself

This is intended for CE-to-CE OTA, not first flash from stock firmware.

## Schedule format

### V3 outlet-condition format

This is the public schedule format for the first CE release.

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

Fields:

- `id`: outlet number `1`-`4`
- `conditions`: one or more schedule conditions for that outlet

Condition fields:

| Type | Fields | Meaning |
|---|---|---|
| `always_on` | none | Keep outlet on whenever AUTO mode is active |
| `time_window` | `start`, `end` | On during a daily `HH:MM` window |
| `rh_low_band` | `low`, `high` | On below `low`, off at `high` |
| `rh_high_band` | `low`, `high` | On above `high`, off at `low` |
| `temp_low_band_c` | `low_c`, `high_c` | On below `low_c`, off at `high_c` |
| `temp_high_band_c` | `low_c`, `high_c` | On above `high_c`, off at `low_c` |
| `interval` | `run_mins`, `every_hrs`, optional `window` | Run for N minutes every N hours, optionally inside daily allowed hours |

Allowed conditions by outlet assignment:

| Outlet assignment | Allowed conditions |
|---|---|
| Light | `time_window` or mutually exclusive `always_on` |
| Fan | Any combination of `time_window`, `temp_high_band_c`, `rh_high_band`, or mutually exclusive `always_on` |
| Humidifier | `rh_low_band` |
| Dehumidifier | `rh_high_band` |
| Heater | `temp_low_band_c` |
| AC Controller | `temp_high_band_c` |
| Water Pump | one `interval` per Water Pump outlet |

Schedule editors should use this table to decide control visibility. Unsupported
condition controls are hidden rather than shown disabled. Newly assigned outlets
start with no selected schedule conditions; persisted schedule entries include
only the conditions the user chooses. A local UI pause can exclude an outlet from
evaluation without deleting its saved rules. Firmware still validates incoming
schedule payloads and rejects unsupported combinations.

Runtime behavior:

- The schedule task evaluates every `30` seconds
- Changing an outlet assignment clears that outlet's schedule entry without creating a replacement/default schedule; in AUTO, that outlet turns OFF immediately, while MANUAL leaves relay outputs unchanged
- The new outlet assignment remains without an active schedule entry until the user or Command Center saves a schedule for it
- Wall-clock conditions require valid wall time from SNTP or browser sync
- `time_warning` is not AUTO-only; it may be non-empty in MANUAL mode when the configured time source is unhealthy
- `time_warning` appearance or clearance publishes `schedule/state` immediately even when relay outputs are unchanged
- Sensor conditions fail inactive when the required sensor reading is invalid, unavailable, or older than 120 seconds
- When any active AUTO schedule depends on temp/rH and sensor data is invalid, unavailable, or stale, `schedule/state.sensor_warning` should tell clients to show a top-level warning such as `Sensor data unavailable; temp/rH automation is paused`
- `sensor_warning` is retained as part of `schedule/state` so clients see the current warning immediately after subscribe
- `sensor_warning` appearance or clearance publishes `schedule/state` immediately even when relay outputs are unchanged
- In MANUAL mode, `sensor_warning` stays empty; clients may still mark invalid sensor readings near telemetry, but should not say automation is paused
- Stale sensor data disables only the affected sensor condition; other valid active conditions, such as a fan `time_window`, can still authorize the outlet
- If stale sensor data removes the last active condition authorizing an outlet, AUTO turns that outlet OFF at the next 30-second schedule evaluation and publishes `schedule/state`
- When another condition keeps the outlet ON despite stale sensor data, `outlet_status[].summary` stays single-line, for example `ON - time active; sensor data unavailable`
- When a sensor-based condition is inactive because sensor data is unavailable or stale and no other condition authorizes the outlet, `outlet_status[].summary` should say `waiting for sensor data`
- On stale-sensor recovery, environmental conditions reset as inactive and evaluate from the recovered reading; a reading already beyond the ON threshold may activate at the next schedule evaluation, while a reading inside the configured band waits for a threshold crossing
- Pump intervals without a `window` use uptime and can run without valid wall time
- Multiple Water Pump outlets may have interval schedules. Each pump outlet tracks its own interval state, due/blocked state, `Run Now` eligibility, and `outlet_status[].summary`.
- Multiple pump outlets may run simultaneously when their independent interval state or `Run Now` actions overlap. Firmware does not provide a global pump queue or one-pump-at-a-time lock.
- When wall time is valid, an active pump summary includes the projected stop time, for example `ON - pump active; off after 15 min (10:15 PM)`.
- Time windows may cross midnight, are start-inclusive and end-exclusive, and reject equal `start` / `end`
- Fan turns on when any enabled condition is active and turns off only after all enabled conditions are inactive
- Saving a schedule in MANUAL validates and persists it without changing relay outputs
- Saving a schedule in AUTO validates, persists, evaluates immediately, updates relay outputs, and publishes state
- Water Pump `Run Now` is a schedule-owned action, not a `control/relay` write; in AUTO, it starts one immediate run for an active pump interval, publishes `schedule/state`, and leaves mode and schedule unchanged
- A successful pump `Run Now` resets the interval timer; the next automatic run is scheduled from the `Run Now` start time, subject to the allowed-hours window
- If `Run Now` arrives while that pump is already running, firmware treats it as an idempotent no-op success and does not extend the run or reset the interval timer
- The local web UI mirrors this idempotency by disabling or relabeling `Run Now` as `Running` while a pump run is active; submitted duplicate requests are no-op successes
- On boot, schedule load, or accepted edit to a pump interval condition, that pump interval waits one full `every_hrs` interval before the next automatic run; use `Run Now` for intentional immediate watering
- Pump `Run Now` obeys the optional interval `window`; outside the allowed-hours window, firmware must not start the pump and clients should show the next available window time
- The local web UI and remote clients keep `Run Now` visible but disabled when the allowed-hours window or missing valid wall time makes the action ineligible; the single-line status explains the next available window or that time sync is needed
- Pump runs with an allowed-hours `window` may start only when the full `run_mins` duration fits inside that window; otherwise the run waits for the next eligible window
- A due pump run blocked by the allowed-hours `window` remains due; the interval timer resets only when the pump actually starts

Validation rules:

- schedule version must be `3`
- top-level `outlets` must contain at least one schedule entry; empty schedules are rejected
- each scheduled outlet must have at least one condition
- an outlet may have at most one condition of each supported type
- `always_on` is mutually exclusive with all other conditions
- condition validity is based on the outlet's current assignment
- changing an outlet assignment clears that outlet's schedule entry and leaves the new assignment without an active schedule entry until the user or Command Center saves a schedule for it
- multiple outlets may use the Water Pump assignment; each may have one `interval` condition
- humidity values must be `10`-`95` with at least a `2` percent rH gap
- temperature values are stored in Celsius, must be `0`-`50`, and must have at least a one display-degree gap
- pump `run_mins` must be `1`-`240`
- pump `every_hrs` must be `1`-`168`
- if a pump interval has an allowed-hours `window`, the window duration must be at least `run_mins`
- invalid condition/assignment combinations are rejected

CE is pre-public-release, so v2 schedule compatibility is intentionally not
part of the public contract. Stored v2 schedules are rejected, logged, and
cleared on boot.

## Local-control interaction rules

MQTT connectivity does not lock local controls. The local web UI and Command
Center both write the same persisted active schedule, and the firmware publishes
the resulting active schedule state back to MQTT.

Conflict policy:

- last accepted schedule write wins
- local web UI saves and MQTT `grow` writes update the same active schedule
- accepted schedule writes publish `schedule/state`
- empty schedules are rejected and are not treated as clear commands
- changing an outlet assignment clears that outlet's schedule entry, does not create a replacement/default schedule, and publishes `schedule/state`
- clearing the schedule in auto mode turns scheduled outlets OFF and publishes `schedule/state`
- clearing the schedule in manual mode leaves relay outputs unchanged and publishes `schedule/state`
- rejected schedule writes publish `schedule/error` and leave the previous active schedule unchanged
- switching from auto to manual preserves current relay outputs
- switching from manual to auto evaluates the active schedule immediately and publishes `schedule/state`
- outlets without active schedule entries are turned OFF during AUTO evaluation
- MQTT `control/relay` writes are accepted only in manual mode; auto mode rejects them and publishes `control/error`
- Pump `Run Now` should not be implemented as an AUTO-mode `control/relay` write; it is a schedule-owned action for active Water Pump interval schedules
- manual relay output state is not restored after reboot; relays initialize OFF
- Command Center should mirror `schedule/state` instead of inferring ownership from MQTT connection state

## NVS keys relevant to MQTT and scheduling

All current config lives in the `growhub` namespace.

| Key | Type | Meaning |
|---|---|---|
| `mqtt_host` | string | Broker hostname or IP |
| `mqtt_port` | `u16` | Broker port |
| `mqtt_dis` | `u8` | Soft-disable flag, `1` = disabled |
| `report_s` | `u8` | Sensor publish interval in seconds |
| `relay_mode` | `u8` | `0` = auto, `1` = manual |
| `sched_json` | blob | Persisted schedule payload |
| `sched_dis` | `u8` | Local web UI schedule-pause mask; bit0 = Outlet 1 through bit3 = Outlet 4 |
| `dev_name` | string | Device name used in payloads |
| `timezone` | string | POSIX timezone string |
| `temp_unit` | `u8` | UI display unit; MQTT telemetry still sends Celsius |
| `time_src` | `u8` | `0` = SNTP, `1` = manual browser-set time |
| `sntp_primary` | string | Primary SNTP server hostname |
| `sntp_secondary` | string | Secondary SNTP server hostname |
| `temp_off` | `i16` | Temperature calibration offset times 100 |
| `rh_off` | `i16` | Humidity calibration offset times 100 |

Behavior details:

- Saving MQTT host/port through the web UI implicitly clears `mqtt_dis`
- Disconnecting Command Center from the web UI sets `mqtt_dis=1` but preserves `mqtt_host` and `mqtt_port`
- Received schedules persist across reboot because `sched_json` is saved on MQTT receipt
- SNTP server changes apply immediately and are published back through time health fields on `schedule/state`

## Practical assumptions for external tooling

- Treat MQTT as optional. A device may intentionally have no broker configured.
- Use the MAC-address topic path exactly as emitted by the device.
- Prefer CE v3 schedule payloads for new tooling.
- Do not assume TLS, auth, or server-side broker ACLs are present unless the operator adds them externally.
- Treat the MQTT OTA topic as a privileged control path on the trusted LAN.
