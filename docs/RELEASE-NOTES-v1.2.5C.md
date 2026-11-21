# Growhub CE 1.2.5C — proposed candidate

This candidate combines independent per-outlet Auto/Manual ownership with an
additive native-client API and the Growhub Remote iOS app. It has not been
published as a release. This image has completed CE-to-CE OTA and a separate
native-API reboot on a physical controller.

## Behavior

- HTTP form keys match exactly: `outlet_mode` is never interpreted as the bulk
  `mode` command. A regression test covers the app/browser request forms.
- Each outlet can keep its schedule or be taken over manually. Manual takeover
  preserves the current output; returning to Auto evaluates immediately.
- The local web and MQTT clients retain their existing contracts. Schedule v3,
  existing settings/NVS, relay numbering, partitions and CE-to-CE OTA are preserved.
- `GET /api/v1/settings`, `POST /api/v1/settings`, and `GET /api/v1/status` support
  native clients. Settings reads never expose saved passwords. Writes to the new
  endpoint require the exact `X-Growhub-ID` and return JSON acknowledgements.
- The iOS main controls show light on its raw 0–255 scale. Missing/invalid sensor
  readings show Unavailable rather than zero humidity or a converted 32°F.
- Status adds sensor-validity fields. The verified sensor driver still supports
  temperature, relative humidity and light; CO2 hardware remains unsupported.
- The iOS app provides native schedules, equipment assignments, device/time,
  Wi-Fi, MQTT, firmware installation and diagnostics screens. Earlier standard CE
  firmware is read through its legacy form interface and written through `/save`.
- Controller names are local to the app, with the original GrowHub identity always
  visible. Outlet names use the controller’s shared equipment labels; label edits
  preserve assignments and schedules. A single remembered controller opens
  directly on app startup. Earlier remembered-controller data still decodes.
- Factory reset through the new native path is blocked if it would remove a
  disabled operation-LED override used by a known faulted controller.

## Completed validation

The firmware compiles and passes verified artifact packaging, host-side native
API snapshot tests, and real relay/schedule C tests under ASan/UBSan. The iOS app's
core tests cover native and legacy saves, identity verification, concurrent edits,
name migration, form encoding, firmware file validation and schedule preservation.
Read-only compatibility was checked on a real controller running 1.2.0C and,
after OTA, 1.2.5C. The real update preserved the controller identity, settings and
manual ownership; a separate native-API reboot preserved all 91 exported settings.
Manual outputs started OFF as designed. iPhone and
iPad simulator journeys exercise the native screens and demo settings saves.

The hardware owner confirmed hands-on validation complete on November 21, 2026.
The corrected HTTP parser was tested on the real controller: selecting AUTO or
MANUAL changed only the targeted outlet and preserved the other three modes.
After reconnecting the sensor, valid temperature, humidity and light readings
were observed. See the [validation record](VALIDATION-v1.2.5C.md) for the tested
image checksum, direct observations and owner-confirmed hardware acceptance.

No stock NIWA cloud-to-CE OTA migration or CO2 driver is included.
