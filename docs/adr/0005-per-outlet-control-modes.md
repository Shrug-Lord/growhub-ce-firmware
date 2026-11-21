# ADR 0005: Independent outlet control modes

**Status:** Proposed for upstream review; hardware validation completed November 21, 2026.

**Supersedes:** ADR 0004's device-wide-only ownership rules. Schedule v3 is unchanged.

## Behavior

Each physical outlet has its own persisted AUTO/MANUAL mode. AUTO evaluates that
outlet's saved rules; MANUAL accepts targeted ON/OFF commands and ignores its
saved rules. Changing one outlet's mode does not change the other three modes.
Taking over manually preserves the current output. Returning to AUTO evaluates
immediately; an AUTO outlet without an enabled schedule is OFF. Schedules remain
editable and saved while an outlet is MANUAL.

The relay layer serializes ownership changes and output writes. Schedule writes
merge only AUTO-owned bits under the same lock as manual writes, so a stale
schedule mask cannot overwrite a manual outlet. Per-outlet UI buttons send a
physical outlet ID and action, rather than a snapshot of all four outputs.

Mode is persisted; manual ON/OFF state is not. On boot all outputs start OFF,
then AUTO outlets are evaluated. A missing `relay_manual` NVS key derives all four
modes from the existing `relay_mode` key. For downgrade safety, the legacy key is
saved as MANUAL whenever any manual outlet exists: older firmware will leave all
outlets OFF on reboot until explicitly controlled, rather than schedule-drive a
manual outlet. Returning all outlets to AUTO updates that key to AUTO.

Manual takeover clears the outlet's pump-run and environmental hysteresis state.
The saved pump cadence is retained; AUTO can start a due run on return. Pump
Run Now is permitted only for the target outlet in AUTO. Automation-blocking
warnings and schedule summaries exclude MANUAL outlets.

## Compatibility

Existing whole-device mode commands still set all four outlets. All-off atomically
sets all four to MANUAL and OFF. Legacy full-relay-mask writes still require all
four outlets to be MANUAL. The binary `mode` field stays `manual` when all four
are manual, otherwise `auto`, for existing clients. New clients must read
`outlet_status[].mode` for ownership; `manual_mask` also exposes relay-bit ownership.
Older Command Center versions do not provide per-outlet mode controls and may
show a mixed controller as AUTO. Their bulk mode commands deliberately reset all
four modes. Companion UI changes should be implemented separately.

## Validation

`scripts/test-outlet-control.sh` runs the real relay and schedule implementations
with host substitutes for GPIO/NVS/time and with AddressSanitizer and UBSan.
It covers independent control, schedule save/clear, mapping for all four outlets,
mode restoration with outputs OFF, pump takeover/eligibility, warnings, legacy
commands, invalid IDs, all-off, and concurrent schedule/manual output writes.

CE-to-CE OTA, startup/mode migration, native reboot and isolated outlet-mode
commands were checked on a real controller. The hardware owner confirmed
hands-on validation complete. See the [validation record](../VALIDATION-v1.2.5C.md)
for exact observations, the tested image checksum and automated coverage.
