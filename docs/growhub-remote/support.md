# Growhub Remote support

Growhub Remote is a free, independent LAN control app for NIWA Growhub/Growhub+
controllers already running Growhub Community Edition firmware. It does not work
with stock NIWA cloud firmware or install CE firmware for the first time.

Connect your iPhone or iPad to the same Wi-Fi LAN and allow Local Network access.
Refresh to discover controllers, or use + to enter a controller address. Guest
Wi-Fi, VLAN isolation and VPN routing can prevent discovery. For subnets larger
than /20 the app probes the local /24 neighborhood plus saved addresses; add
other controllers by address. IPv6-only discovery is not currently supported.

Released CE firmware may have a shared Auto/Manual mode for all four outlets.
Manual preserves the current outputs and pauses schedules. Use Resume schedules
to return to Auto. Independent outlet modes require compatible installed firmware.
Full controls provides native schedules, assignments, device/time, Wi-Fi, MQTT,
firmware updates and diagnostics. Earlier standard CE firmware is supported through
its existing local interface. Outlet names use the controller’s saved equipment
labels, shared by the app, Outlet assignments and the web interface. Editing a name
saves only its label and leaves equipment types and schedules unchanged. Pull to
refresh to read changes made elsewhere. The original GrowHub ID stays visible
under a custom controller name. A single remembered Growhub opens directly on
startup; Back returns to the list. Multiple remembered Growhubs open on the list.

The compact header keeps your Growhub name and original ID visible. The small
pencil beside the name renames the Growhub. Wi-Fi is green when connected, red
when disconnected and orange after a request error. Tap the adjacent info button
for the IP address, firmware version and signal details.

Temperature/humidity require a connected sensor. Invalid readings show Unavailable.
Light is a relative 0–255 sensor reading, not lux or a calibrated percentage.
The controller and app currently display live readings without stored sensor history.

Use Help and demo → Explore demo to try the native controls without hardware.
Demo controllers never send network requests or operate physical equipment.

For help, email support@deep6gear.com with your app/iOS and CE firmware versions,
the action you tried, and the error message. Do not send Wi-Fi passwords.

Published by Plongee6 LLC. Not affiliated with or endorsed by NIWA or the Growhub
CE maintainers. The CE firmware is a separate open-source project:
https://github.com/Shrug-Lord/growhub-ce-firmware
