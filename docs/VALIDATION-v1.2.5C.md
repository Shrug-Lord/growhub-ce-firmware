# Growhub CE 1.2.5C validation record

Hardware acceptance was confirmed by the controller owner on November 21, 2026.
The firmware contribution is validated on the available physical controller and
is ready for upstream review. Publication of an upstream release is separate.

## Physical controller checks completed

| Check | Observed result |
| --- | --- |
| Earlier CE compatibility | The native client read the real controller's 1.2.0C settings through the legacy form adapter. |
| CE-to-CE OTA | The 1.2.0C controller accepted the 1.2.5C application image and returned online with the expected version and identity. |
| Corrected image OTA/startup | The exact image below was uploaded after the HTTP key-matching correction, returned HTTP 200, restarted and served the new API. |
| Native reboot | A separate native-API reboot returned HTTP 202 and reconnected. All 91 exported settings matched before/after that reboot on the initial 1.2.5C API candidate. The corrected image also completed its OTA restart. |
| Settings/mode migration | Controller identity, existing settings and manual outlet ownership survived OTA. Manual outputs started OFF after restart as designed. |
| Independent modes | On the corrected image, the app/browser command changed only outlet 2 to AUTO; outlets 1, 3 and 4 stayed MANUAL. Switching outlet 2 back preserved the other modes again. |
| Physical operation | The hardware owner tested the controller with the iPhone app and confirmed hardware validation complete. |
| Sensor connection | The disconnected sensor reported invalid readings. After reconnection, the controller reported valid temperature, humidity and raw light readings without another firmware change. |

Tested corrected OTA application image, SHA-256:

```text
694dbff737559078034d2847c3f11c11a36c90081c711c7b02e9a8bdc4f20daa
```

## Automated checks completed

- Full ESP32 build and verified OTA/first-flash packaging, including embedded
  version, OTA partition size, package equality and checksums.
- Real relay/schedule implementation tests with ASan/UBSan: all physical outlet
  mappings, independent ownership, concurrent schedule/manual writes, schedule
  save/clear, pump ownership, warnings and mode restoration with outputs OFF.
- Settings snapshot tests with ASan/UBSan, shared C-to-Swift protocol fixtures,
  and production HTTP form-parser regression tests for exact key matching.
- 23 Swift core tests: native/legacy settings saves, identity protection,
  concurrent edits, form encoding, firmware image validation, remembered names,
  controller-owned labels, sensor validity and preservation of other settings.
  The optional real-controller read test passed on both legacy and native API
  firmware; portable runs skip that hardware-only test when no host is supplied.
- Native iPhone/iPad simulator journeys: controls and demo, names and original
  device identity, shared equipment labels, schedule edits, native advanced
  screens, direct startup and Back navigation, invalid sensors, light, the
  compact header, device-info popover and connected/offline/request-error states.

Simulator/demo checks operate no physical relays. Physical observations and the
owner's acceptance above are recorded separately so that the evidence for each
check remains clear. This record makes no claim of CO2 hardware support, sensor
calibration, stock-to-CE OTA, or testing every possible network/broker setup.
