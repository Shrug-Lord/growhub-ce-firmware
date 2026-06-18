# ADR 0004: V3 outlet-condition scheduling and time health

**Status:** Accepted
**Date:** 2026-06-14
**Supersedes:** N/A
**Related:** ADR-0001 (GitHub Releases as canonical hosting), ADR-0002 (OTA rollback), ADR-0003 (UART-required first flash)

## Context

Growhub CE scheduling was originally represented as one mode per outlet, such as `timer`, `rh`, `temp`, or `interval`. That shape cannot express common grow-control behavior like a fan that should turn on for a time window, high temperature, or high humidity, while also explaining which condition is currently controlling the outlet.

The project is still pre-public-release, so schedule compatibility with the existing v2 payload is not required.

## Decision

Replace the v2 one-mode-per-outlet schedule format with a v3 outlet-condition schedule model. Each schedule entry references an outlet by id and contains enabled schedule conditions; the outlet assignment remains device config and is not duplicated inside the schedule payload.

The v3 condition types are:

- `always_on`
- `time_window`
- `rh_low_band`
- `rh_high_band`
- `temp_low_band_c`
- `temp_high_band_c`
- `interval`

Environmental control uses explicit bands instead of hidden fixed hysteresis:

- low-band conditions turn on below `low` / `low_c` and off at `high` / `high_c`
- high-band conditions turn on above `high` / `high_c` and off at `low` / `low_c`
- humidity bands require at least a 2% rH gap
- temperature bands are stored in Celsius and require at least a 1 display-degree gap

V1 outlet-assignment behavior is:

- `Light`: one `time_window`, or mutually exclusive `always_on`
- `Fan`: optional `time_window`, optional `temp_high_band_c`, optional `rh_high_band`, or mutually exclusive `always_on`; any enabled condition can turn the fan on, and all enabled conditions must be inactive before the fan turns off
- `Humidifier`: one `rh_low_band`
- `Dehumidifier`: one `rh_high_band`
- `Heater`: one `temp_low_band_c`
- `AC Controller`: one `temp_high_band_c`
- `Water Pump`: one `interval` per Water Pump outlet, with optional allowed-hours `window`

For v1, an outlet may have at most one condition of each supported type. The UI must require at least one condition for a scheduled outlet. Schedule editors show only condition controls valid for the outlet's current assignment; unsupported controls are hidden rather than shown disabled. Newly assigned outlets start with no selected schedule conditions; users choose the controls they want and save the schedule. Once an outlet has saved rules, users can disable the outlet card to pause automation for that outlet without deleting the saved rules. Firmware still rejects an outlet schedule entry with an empty condition list or an invalid condition for the outlet assignment, leaving the previous active schedule unchanged.

When an outlet assignment changes, firmware clears that outlet's schedule entry because schedule conditions are valid only for the current assignment. Firmware does not auto-create a replacement active schedule for the new assignment; the local web UI shows the new assigned outlet with no selected conditions, and it is not active until the user saves conditions for it. In AUTO, that outlet is turned OFF immediately and schedule state is published. In MANUAL, relay outputs are left unchanged and schedule state is published.

The local schedule editor remains visible and editable in both MANUAL and AUTO mode. Saving a schedule in MANUAL mode validates and persists it but does not change relay outputs. Saving a schedule in AUTO mode validates, persists, loads, evaluates immediately, updates relay outputs, and publishes schedule state. This lets users prepare automation safely while manually controlling outlets, while still allowing live schedule edits when the device is already running in AUTO.

Clearing the active schedule in AUTO immediately turns scheduled outlets OFF, clears the persisted schedule, and publishes schedule state. Clearing the active schedule in MANUAL clears the persisted schedule but leaves relay outputs unchanged.

Switching from MANUAL to AUTO evaluates the active schedule immediately, updates relay outputs, and publishes schedule state. Firmware does not wait for the next 30-second schedule tick before applying AUTO ownership. Outlets without an active schedule entry are turned OFF during AUTO evaluation so manual ON states cannot survive indefinitely in AUTO.

Switching from AUTO to MANUAL preserves current relay outputs. This is a non-destructive takeover path; users and clients use the all-off control command when they want an explicit stop.

Direct relay writes are accepted only in MANUAL mode. In AUTO mode, the schedule engine owns relay outputs; direct relay writes are rejected, relay state remains unchanged, and firmware publishes a control error event. Clients that need a manual override must switch to MANUAL first, then send the relay bitmask.

Water Pump `Run Now` is a schedule-owned action, not a direct relay write. V1 allows multiple outlets to be assigned as Water Pump, and each Water Pump outlet may have its own active interval schedule. Interval state, due/blocked state, `Run Now` eligibility, and status summary are tracked per outlet. Pump outlets are not globally serialized: if two pump outlets are due or manually triggered at overlapping times, both may run simultaneously, subject only to each outlet's own schedule, allowed-hours window, and relay state. Firmware does not queue pump runs behind another active pump run. It is allowed in AUTO mode only when the target outlet has an active Water Pump interval schedule. The action starts one pump run immediately, publishes schedule state, leaves AUTO mode and the saved interval schedule unchanged, then returns to the normal interval cadence. Remote clients trigger it by publishing `{"action":"pump_run_now","outlet":4}` to `growhub/<MAC>/schedule/action` with QoS `1` and retained `false`; the `outlet` field selects exactly one pump outlet. Clients confirm success through `schedule/state` and failure through `schedule/error`. Firmware rejects the action on `schedule/error` with `pump_schedule_required` if the outlet is not an active Water Pump interval, `auto_mode_required` if the device is not in AUTO, `pump_window_ineligible` if the allowed-hours window is not eligible, or `time_sync_required` if valid wall time is unavailable for a windowed pump. If `pump_run_now` arrives while that pump run is already active, firmware treats it as an idempotent no-op success: it does not extend or restart the run, does not reset the interval timer, and does not publish `schedule/error`. The local web UI mirrors this behavior: while the pump is running, the `Run Now` control is shown as `Running` or disabled, and any submitted request is treated as the same idempotent no-op success. A successful `Run Now` counts as that outlet's interval run; the next automatic run for that outlet is scheduled from the `Run Now` start time, subject to the allowed-hours window. On boot, schedule load, or accepted edit to a pump interval condition, that pump interval waits one full `every_hrs` interval before its next automatic run; users can press `Run Now` for intentional immediate watering. If the interval has an allowed-hours `window`, pump runs are eligible only when the full `run_mins` duration fits inside that window; outside that window, or too close to the end of the window, firmware must not start the pump and clients should show the next available window time. The local web UI and remote clients keep `Run Now` visible but disabled while a run is ineligible because of the allowed-hours window or missing valid wall time; the single-line status explains the next available window or that time sync is needed. A due pump run blocked by the allowed-hours window remains due; the interval timer resets only when the pump actually starts. In MANUAL mode, users and clients should use direct relay control instead; clients should hide or disable `Run Now`.

The all-off control command is a manual override. It turns all relays off, switches the device to MANUAL mode, persists MANUAL mode, and leaves the saved schedule intact. The saved schedule does not resume until the user or client explicitly switches back to AUTO.

Manual relay state is not persisted. On every boot, relay outputs initialize OFF. If the persisted mode is AUTO, the schedule may turn outlets on after evaluation. If the persisted mode is MANUAL, outlets remain OFF until the user or client sends a direct relay command or switches back to AUTO.

## Implementation contract

V3 schedule JSON has this shape:

```json
{
  "v": 3,
  "outlets": [
    {
      "id": 2,
      "conditions": [
        { "type": "time_window", "start": "08:00", "end": "20:00" },
        { "type": "temp_high_band_c", "low_c": 24.0, "high_c": 27.0 },
        { "type": "rh_high_band", "low": 55, "high": 65 }
      ]
    }
  ]
}
```

Condition fields are:

- `always_on`: no fields; mutually exclusive with all other conditions
- `time_window`: `start`, `end` as `HH:MM`
- `rh_low_band` / `rh_high_band`: `low`, `high`
- `temp_low_band_c` / `temp_high_band_c`: `low_c`, `high_c`
- `interval`: `run_mins`, `every_hrs`, optional `window` with `start`, `end`

Validation rules are:

- schedule version must be `3`
- top-level `outlets` must contain at least one schedule entry; empty schedules are rejected
- each scheduled outlet must have at least one condition
- an outlet may have at most one condition of each supported type
- a condition must be valid for that outlet's current outlet assignment
- changing an outlet assignment clears that outlet's schedule entry without creating a replacement/default schedule
- humidity values must be between `10` and `95`, with at least a `2` percent rH gap
- temperature values must be between `0` and `50` Celsius, with at least a one display-degree gap in the user's selected unit
- pump `run_mins` must be between `1` and `240`
- pump `every_hrs` must be between `1` and `168`
- if a pump interval has an allowed-hours `window`, the window duration must be at least `run_mins`
- time windows may cross midnight, are start-inclusive and end-exclusive, and reject equal `start` / `end` values

Rejected schedules leave the previous active schedule unchanged and publish a schedule error event.

Empty schedules are not treated as a clear command. Clearing the schedule is an explicit action so malformed or accidental empty payloads cannot delete automation.

Because CE is pre-public-release, firmware does not keep a compatibility path for v2 schedules. If a stored v2 schedule is found during boot, firmware rejects it, logs the validation reason, and clears it.

Environmental conditions fail inactive when the required sensor reading is invalid, unavailable, or stale. A temp/rH reading becomes stale after 120 seconds without a successful sensor poll; with the current 3-second UART poll interval, that allows about 40 missed polls before automation stops trusting the reading. Stale sensor data disables only the affected sensor condition; other valid active conditions can still authorize the outlet. For example, a fan inside an active time window remains ON when temp/rH data is stale, with a single-line status such as `ON - time active; sensor data unavailable`. When stale sensor data removes the last active condition authorizing an outlet, AUTO turns that outlet OFF at the next 30-second schedule evaluation and publishes schedule state. When a sensor-based condition is inactive because sensor data is unavailable or stale and no other condition authorizes the outlet, the single-line status should say `waiting for sensor data`. On schedule load, boot, or stale-sensor recovery, environmental conditions reset as inactive and evaluate from the recovered/current reading. If that reading is already beyond the condition's ON threshold, the condition may activate at the next schedule evaluation; if the reading is merely inside the configured band, it waits for a threshold crossing. Band-active state is not persisted.

The schedule task evaluates every 30 seconds. Schedule state is published on meaningful changes: schedule load/clear, mode change, relay state changes caused by scheduling, status summary changes, `time_warning` or `sensor_warning` appearance or clearance, and MQTT reconnect. Warning changes publish retained `schedule/state` immediately even when relay outputs are unchanged.

## Status summaries

Firmware is the source of truth for the single-line outlet status summary used by the local web UI and MQTT. Manual mode uses an empty summary because the user directly controls the outlet. Auto mode summaries explain why the outlet is on or what it is waiting for. When wall time is valid, an active pump summary includes the projected stop time, for example `ON - pump active; off after 15 min (10:15 PM)`. The summary is firmware-owned display text, not a structured reason API; clients should display it as text and use the machine-readable `warnings` array for stable warning behavior.

`/status` and MQTT `schedule/state` should include all four outlet statuses with stable fields:

```json
{
  "id": 2,
  "state": "on",
  "summary": "temp + rH active; off after temp < 78°F and rH < 62%"
}
```

The v1 MQTT status remains string-only rather than publishing structured per-condition state.

When any active AUTO schedule depends on temp/rH and the required sensor data is invalid, unavailable, or stale, firmware exposes `sensor_warning` on `/status` and MQTT `schedule/state`. The local web UI and Command Center should show a top-level banner such as `Sensor data unavailable; temp/rH automation is paused`. Outlet summaries still provide per-outlet detail. Because MQTT `schedule/state` is retained, `sensor_warning` is retained with the current automation state so newly connected clients see the warning immediately. `sensor_warning` is empty in MANUAL mode because automation is not currently controlling outlets; the UI may still mark sensor readings invalid near the telemetry display. The warning clears when sensor data recovers, the device leaves AUTO, or no active AUTO schedule depends on temp/rH; firmware publishes the retained `schedule/state` with an empty `sensor_warning` when it clears.

If both `time_warning` and `sensor_warning` are non-empty, the local web UI and Command Center should show both in one compact warning area rather than collapsing to a single highest-priority banner. Order warnings by severity: active AUTO wall-clock automation blocked first, active AUTO temp/rH automation paused second, and drift-only or sync-health time warnings after automation-blocking warnings.

`/status` and MQTT `schedule/state` keep `time_warning` and `sensor_warning` string fields for simple display, and also include a machine-readable `warnings` array for stable client behavior. Each warning entry has `code`, `message`, `severity`, and optional `outlets` fields, and firmware publishes the array in display order. `outlets` lists affected physical outlet IDs when the warning applies to specific scheduled outlets; omitted or empty `outlets` means device-wide. Warning entries do not copy outlet labels or assignments. Clients that need display names resolve the numeric outlet IDs against the current outlet assignment/config state. `time_sync_required` includes affected outlets when wall-clock automation is actually blocked, and `sensor_data_unavailable` includes affected outlets when temp/rH automation is paused. `time_sntp_unhealthy` remains device-wide because it represents time-source drift/sync risk, not a specific outlet block. `message` is firmware-owned default display copy for the local web UI and simple clients; clients that need stable behavior must use `code`, not parse `message`. Command Center may render its own product-specific copy from `code` while preserving the warning meaning and severity. Initial warning codes are `time_sync_required` for active AUTO wall-clock automation blocked by missing valid time, `sensor_data_unavailable` for active AUTO temp/rH automation paused by invalid/unavailable/stale sensor data, and `time_sntp_unhealthy` for configured SNTP that has not synced or whose last successful sync is stale while time is otherwise valid. Warning clearance publishes an empty string field and removes the matching entry from `warnings`.

Rejected schedules publish an error event on:

```text
growhub/<MAC>/schedule/error
```

Schedule and control error payloads use fixed v1 `reason` enums. Clients should branch on `reason` for behavior and display, and treat `detail` as optional debug text that may change. Unknown future reasons should fall back to a generic rejected-command, rejected-schedule, or rejected-action message.

Control error reasons are `invalid_payload`, `invalid_mode`, `invalid_relay_mask`, and `manual_mode_required`.

Schedule error reasons are `invalid_payload`, `unsupported_schedule_version`, `empty_schedule`, `invalid_outlet`, `missing_conditions`, `duplicate_condition`, `invalid_condition`, `condition_not_allowed`, `always_on_exclusive`, `invalid_time_window`, `invalid_band`, `invalid_interval`, `unsupported_action`, `auto_mode_required`, `pump_schedule_required`, `time_sync_required`, and `pump_window_ineligible`.

## Time health

The ESP32 has no battery-backed wall clock. Wall-clock schedule conditions stay inactive until valid wall time exists, but sensor-based conditions and pure uptime-based intervals can continue.

Valid wall time may come from SNTP or browser sync. Browser sync sets the current clock but does not change the configured time source.

Firmware exposes time health separately from schedule state:

- `time_valid`: whether wall-clock conditions can run
- `time_source`: `sntp` or `manual`
- `sntp_status`: `disabled`, `pending`, or `synced`
- `time_warning`: human-readable device time-health warning or empty string

`time_warning` is general device time health, not AUTO-only. It may be non-empty in MANUAL mode when the configured time source is unhealthy, because future schedules and multi-device coordination can drift. Warning text should describe sync or drift risk unless an active AUTO wall-clock condition is actually blocked; only then should it say automation is paused or waiting for time sync.

`schedule/state` includes these fields so Command Center can coordinate multiple Growhub devices and detect devices whose time source is unhealthy. Because MQTT `schedule/state` is retained, `time_warning` is retained with the current automation state so newly connected clients see time-source warnings immediately. Warning clearance publishes the retained `schedule/state` with an empty `time_warning`. `sensor/live` remains Niwa-compatible and does not gain schedule-time fields.

SNTP mode has two configurable servers. Defaults are:

- `pool.ntp.org`
- `time.nist.gov`

The local web UI and MQTT config can update the SNTP server hostnames. Firmware starts SNTP during boot/config apply and restarts SNTP whenever the WiFi station receives an IP address, so reboot, OTA update, and WiFi recovery all trigger a fresh sync attempt as soon as the network is available. If SNTP is selected but the clock was set from browser sync and SNTP has not synced, the UI warns that time is currently valid but may drift after long runs or power loss. Firmware also tracks the last successful SNTP sync and treats SNTP as unhealthy if that sync becomes stale after three SNTP poll intervals, which is about three hours with the current one-hour poll interval. To avoid false warnings immediately after a reboot, OTA update, WiFi recovery, or SNTP config change, firmware suppresses the drift-only `time_sntp_unhealthy` warning for the first hour after SNTP starts or restarts. Blocking `time_sync_required` still appears immediately when an active AUTO wall-clock schedule has no valid wall time.

The status LED uses a distinct time-needed pattern only when an active AUTO schedule contains a wall-clock condition and wall time is invalid. SNTP pending after browser sync is a UI/status warning, not an LED time-needed state. Sensor-data warnings do not add a new LED pattern in v1; they are surfaced through UI/status warnings and outlet summaries.

## Rationale

The one-mode v2 model made simple schedules easy but could not represent real fan control or clear user-facing explanations. A condition-list model maps directly to the grow-control questions users ask: which condition is active, what will clear it, and what is the outlet waiting for?

Explicit bands are more understandable than hidden hysteresis and make status text precise. Keeping outlet assignment outside the schedule avoids conflicts when users change what is plugged into an outlet. Keeping v3 free of v2 compatibility branches is acceptable before public release and keeps the firmware parser simpler.

Time health is separated from valid wall time because browser sync can make schedules runnable while SNTP remains unhealthy. That distinction matters for multi-device setups where Command Center may need several Growhubs to stay aligned.

## Implementation sequence

Implement this as staged milestones rather than one large scheduling rewrite:

1. **V3 model, parser, persistence, and validation** — define the v3 in-memory model, parse/persist `v:3` schedules, reject invalid payloads with fixed `schedule/error.reason` values, clear pre-public v2 saved schedules without compatibility branches, and keep previous active schedules unchanged on rejected writes.
2. **State and error publishing contract** — publish retained `schedule/state` with mode, source, schedule mirror, outlet statuses, time health, warning strings, and machine-readable `warnings`; publish `control/error` and `schedule/error` for rejected commands/actions.
3. **Evaluation engine** — evaluate `always_on`, wall-clock windows, environmental bands, fan multi-condition OR-on/all-clear-off behavior, pump intervals, per-outlet pump `Run Now`, stale sensor handling, wall-time blocking, mode transitions, assignment-change clearing, and all-off/manual semantics.
4. **Local web UI** — replace the one-mode schedule form with assignment-specific condition controls, hide unsupported controls, show newly assigned outlets with no selected conditions, expose Disable/Enable only for saved outlet rules, use user-facing `Time control`, `Humidity control`, `Temperature control`, and `Watering control` labels with condition details hidden until enabled, show single-line outlet summaries, surface time/sensor warnings, expose browser sync/SNTP settings, and wire pump `Run Now`.
5. **MQTT and Command Center polish** — verify MQTT examples against firmware output, keep Command Center aligned to retained `schedule/state`, and use stable warning/error codes instead of parsing display strings.

## Consequences

**Positive:**

- Fan control can respond to time, temperature, and humidity simultaneously.
- Status text can explain why each AUTO outlet is on or waiting.
- Environmental controls use user-visible bands instead of hidden fixed hysteresis.
- Command Center can mirror firmware scheduling without duplicating control logic.
- Users can configure SNTP servers for local NTP or firewall allowlisting.

**Negative:**

- Schedule parsing and status generation become more complex.
- Existing bench-device saved schedules are cleared or must be recreated after upgrading to the v3 model.
- The local web UI has more validation and control states than the previous one-mode form.
