# CONTEXT

Glossary of project-specific terms. No implementation details — those live in source, `docs/`, or research artifacts.

## stock firmware

The original NIWA firmware that ships on every Growhub from the factory. Authenticates to AWS IoT (`aljqvjvymwg81-ats.iot.eu-west-2.amazonaws.com`) via mutual TLS using a per-device client certificate burned into the image, and downloads OTAs from a hardcoded Amazon CloudFront URL. Functionally dead since the Niwa cloud was shut down. The first-flash constraint analysis is captured in `docs/adr/0003-uart-required-first-flash.md`.

## CE firmware

The Community Edition firmware in this repo. A clean-room replacement for [[stock firmware]] targeting the same hardware. **Operates fully standalone**: with WiFi configured via captive portal, a user can run their controller without any external service. Integration with an MQTT broker is *optional* and enables extended capabilities — see [[Growhub Command Center]]. Bound to fit within a 1.2 MB OTA slot.

## Growhub Command Center

A separate companion repository (`github.com/Shrug-Lord/Growhub-Command-Center`, not yet shipped) that consumes the MQTT data published by [[CE firmware]] devices. Provides logs, historic tracking, a polished UI, and a single pane for managing multiple [[device]]s. Not required to operate a single controller — [[CE firmware]] is fully functional standalone. Its initial tested firmware baseline is CE `1.1.0C`, with runtime workflows gated by validated MQTT capabilities and payload contract versions rather than the display-version string alone.

## first flash

The initial install of [[CE firmware]] onto a device currently running [[stock firmware]]. **Requires physical UART access** — cannot be performed over the air. This is a hard constraint imposed by stock firmware's design (cert pinning + notify-ota gating + AWS IoT MQTT dependency), not a project choice. See `docs/adr/0003-uart-required-first-flash.md` for the analysis.

## OTA update

A firmware update delivered over the air to a device already running [[CE firmware]]. Distinct from a [[first flash]]: uses CE firmware's own update mechanism (URL paste, file upload via `/ota_upload`, or MQTT trigger on `growhub/<MAC>/ota`). After [[first flash]], OTA updates do not require UART again.

## device

A single NIWA Growhub or Growhub+ unit. Identified by its WiFi MAC address as 12 uppercase hex characters; do not assume one OUI across product variants. The repo currently tracks three physical bench units for development: two Growhub+ units and one original Growhub. In the field, every CE-firmware device is independent.

## setup access point

The WiFi network broadcast directly by a [[device]] for local provisioning and recovery. Distinct from the home WiFi network that the device joins for normal standalone operation.

## outlet

One of the four switched AC outlets on the Growhub's rear, controlled by a relay GPIO on the ESP32. Numbered 1-4. The community convention in code, docs, and UI is **outlet**, not "socket". User-facing outlet labels are separate from outlet assignments.

## outlet assignment

The user-selected equipment type plugged into an [[outlet]], such as `Light`, `Fan`, `Humidifier`, `Dehumidifier`, `Water Pump`, `Heater`, or `AC Controller`. Scheduling options and schedule validation are based on outlet assignments, not outlet labels or "connected devices"; [[device]] is reserved for a Growhub unit.

## outlet label

The user-facing role or name for a physical [[outlet]], such as `Exhaust Fan`, `Circulation Fan`, `Reservoir Pump`, or `Canopy Light`. Labels help Command Center disambiguate duplicate assignments when compiling portable schedule templates. Labels do not control firmware schedule validation.

## schedule condition

A rule attached to an [[outlet]] schedule, such as a time window, temperature threshold, humidity threshold, or interval cycle. An outlet assignment may expose one or more schedule conditions, and the user chooses which ones are enabled.

## manual override

Direct user or API control of outlet relays while scheduling is paused. Manual override is deliberate, keeps scheduling paused until AUTO mode is selected again, and does not delete the saved schedule.

## valid wall time

The controller's current clock time is trustworthy enough to evaluate wall-clock schedule conditions such as time windows. Sensor-based conditions and pure uptime-based intervals can continue without valid wall time, but wall-clock conditions stay inactive until time is available from SNTP, browser sync, or the MQTT `time/action` sync. A missing valid wall time is user-visible only when an active AUTO schedule contains a wall-clock condition.

## SNTP server

A user-configurable network time source used when the controller is set to SNTP time mode. CE firmware defaults to `pool.ntp.org` and `time.nist.gov`, but users may change those hosts for local NTP servers or firewall allowlisting.

## relay

The physical electromechanical relay (and its GPIO) backing a single [[outlet]]. The default mapping is `outlet 1 -> GPIO 33`, `outlet 2 -> GPIO 25`, `outlet 3 -> GPIO 26`, `outlet 4 -> GPIO 27`. "Relay" refers to the implementation; "outlet" is what the user sees.

## ota_0 / ota_1 / factory partition

The three app-image slots in the ESP32 flash partition table. `factory` (1.5 MB) is what ships pre-loaded; `ota_0` and `ota_1` (1.2 MB each) are the dual OTA slots [[CE firmware]] runs from and updates between. See `docs/HARDWARE.md` for the current partition table.
