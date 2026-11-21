# Local HTTP API

Development firmware 1.2.5C adds a versioned native-client settings interface.
The existing web UI, `/status`, `/save`, `/scan`, `/savetime`, `/updates`,
`/ota_status`, `/ota_upload`, MQTT topics, NVS keys and partition layout remain
compatible. These LAN endpoints use the existing trusted-network model; the
identity header described below is not authentication.

## Read settings

`GET /api/v1/settings` returns `application/json`, with `Cache-Control: no-store`:

```json
{
  "api": 1,
  "mac": "AABBCCDDEEFF",
  "fw": "1.2.5C",
  "fields": {
    "dev_name": "Garden",
    "timezone": "UTC0",
    "temp_unit": "0",
    "time_src": "0",
    "outlet_1": "Light",
    "label_1": "Grow lights",
    "sched_v3": "1",
    "s1_disabled": "0",
    "s1_time_en": "1",
    "s1_start": "06:00",
    "s1_end": "22:00"
  },
  "choices": {
    "timezone": [{"value": "UTC0", "label": "UTC"}],
    "outlet_type": [{"value": "Light", "label": "Light"}]
  },
  "capabilities": {
    "outlet_modes": true,
    "schedule_v3": true,
    "temperature": true,
    "humidity": true,
    "light": true,
    "co2": false,
    "file_ota": true,
    "release_updates": true
  }
}
```

The actual response includes four outlet configurations, current schedule values,
Wi-Fi SSID, MQTT host/port, setup-AP preference, time servers and operation-LED
override/pin information. Wi-Fi and setup-AP passwords are never returned.
Field values are strings so they can round-trip through the established form
protocol. Boolean schedule controls use `"1"` when selected and `""` otherwise;
`sN_disabled`, configuration booleans and enum values use `"0"`/`"1"`.
Temperature thresholds are expressed in the selected display unit. Equipment
labels and types use physical outlet numbers, independent of relay-bit order.
Ignore unknown fields and choices in schema 1. Reject unsupported API versions.

Capabilities describe implemented drivers/features, not whether a current sensor
reading is valid. CO2 remains unsupported by the verified driver. `/status` adds
`co2_valid` and `temp_valid`; clients must not display an unavailable CO2 sensor as
zero ppm. No CO2 hardware driver was added by this API work.

`GET /api/v1/status` is an alias for the existing `/status` JSON response.

## Write settings and issue actions

`POST /api/v1/settings` accepts the same bounded
`application/x-www-form-urlencoded` field names as `/save`, not a JSON body.
Provide `X-Growhub-ID: AABBCCDDEEFF` with the exact uppercase controller identity.
A missing or mismatched identity returns HTTP 400 before applying any change.

The request uses the existing setter/validation path. Native success returns
`{"api":1,"accepted":true,"restarting":false}`. Restarting operations return
HTTP 202 with `restarting:true` before restart. Invalid schedules return HTTP 400
instead of the legacy HTML redirect. The legacy web behavior is preserved.

Examples of existing request groups:

| Group | Parameters |
| --- | --- |
| Outlet assignment/label | `outlet_N`, `label_N`, N = 1..4 |
| Controller | `dev_name`, `timezone`, `temp_unit`, `time_src`, time server fields |
| Schedules | `sched_v3=1` and **all four outlets'** selected conditions |
| Pause a saved schedule | `sN_disabled=1` within a complete schedule save |
| Clear schedules | `sched_clear=1` |
| Pump run now | `pump_run_now=N` |
| Wi-Fi | `ssid`, `password`; send only when changing credentials |
| Setup access point | `keep_ap=0` or `1` |
| MQTT | `mqtt_host`, `mqtt_port`, `cc_disconnect=1`, `cc_reconnect=1` |
| Hardware override | `operation_led=0` or `1`; restarts |
| Maintenance | `reboot=1`, `reset=1`, `ota_url=<firmware app image URL>` |
| Per-outlet control | `control_outlet=N` with `outlet_mode` or `outlet_state` |

An assignment change intentionally clears that outlet's saved schedule, matching
the web UI. Schedule saves replace the whole schedule: clients must preserve
other outlets, omit unchecked condition controls, and check for concurrent edits.
Manual-owned outputs remain protected by the independent outlet-mode engine.
Factory reset through the new API is rejected while the operation-LED override is
disabled, because reset would erase the protection needed by a known faulted unit.

An accepted request is not confirmation that the intended setting persisted.
Read settings/status again and compare the result. After Wi-Fi changes, reboot
or OTA, reconnect by MAC identity and verify the new firmware/version and state.
Do not retry a state-changing request automatically after a timeout.

File OTA remains raw `POST /ota_upload` with the CE `firmware.bin` app image.
Release actions remain JSON `POST /updates`; see [OTA](OTA.md) and [MQTT](MQTT.md).
These endpoints retain their established contracts.

## Older standard CE firmware

Do not infer capability solely from a version string. Probe the settings endpoint
read-only. An older CE firmware's 404/405/501 response means the app should use
the legacy adapter: parse known input/select fields from `GET /`, optionally read
the operation-LED setting from `GET /diagnostics`, then render native controls.
Never execute scripts or infer a field that was absent. Writes continue through
the existing `/save` form protocol and must be read back and verified.

The v1.0.0C, v1.1.0C and v1.2.0C source baselines use the same v3 schedule fields.
Existing global-mode controllers retain their shared Auto/Manual control. A
per-outlet mode must be shown only when `/status` reports each outlet's mode.
This compatibility applies to standard **CE** firmware, not factory NIWA cloud
firmware, which has no compatible local controller interface.

## Validation

`scripts/test-settings-api.sh` checks real C snapshot serialization, outlet/slot
mapping, units, escaping, schedule windows and password exclusion under
ASan/UBSan. `scripts/test-outlet-control.sh` covers relay/schedule ownership.
PlatformIO and the verified artifact-packaging build must pass as well.
New firmware still requires physical NVS migration, relay/reboot and OTA acceptance
before a public firmware release. Native app compatibility with installed 1.2.0C
has been checked by read-only local status/settings requests.
