# Growhub Remote for iPhone and iPad

Free, independent native LAN companion for Growhub Community Edition firmware.
No account, cloud service, subscription or MQTT broker is needed by the app.

- Discovers and remembers multiple controllers by MAC identity.
- Native outlet switches, local controller names and a stable GrowHub ID subtitle.
- Compact three-reading card, subtle name-adjacent rename pencil and Wi-Fi status
  beside an info popover for the address, firmware and signal. Request errors are
  tracked per controller.
- Outlet names use the controller’s equipment label throughout native controls;
  edits save only that shared label. Cached names are refreshed from the controller.
- Opens a single remembered Growhub directly at startup, before a subnet scan;
  Back returns to the list and multiple remembered controllers keep the list.
- Native schedules, equipment assignments, device/time, Wi-Fi, MQTT, firmware
  installation and diagnostic/hardware-override screens; no embedded web page.
- Detects the additive settings API. Earlier standard CE releases are supported
  through a parser for their existing form interface, without executing scripts.
- Uses per-outlet modes only when reported by firmware. Older global-mode units
  ask before pausing all schedules. Settings saves check for concurrent edits and
  read back saved values. Changing an equipment assignment clears its schedule.
- Temperature/humidity validity is respected; invalid readings show Unavailable.
  Light appears on the main controls as the raw 0–255 reading, without invented units.
- A local demo exercises native controls without operating physical equipment.

Requires iOS 17 or later, compatible CE firmware and reachable local Wi-Fi.
Factory NIWA cloud firmware is not supported. Local Network permission is needed.
Full-subnet IPv4 discovery is bounded; Add address supports other reachable units.
Firmware 1.2.5C and iOS app 1.0.0 use separate version numbers.

## Build

Open GrowhubRemote.xcodeproj in Xcode. Select your own signing team and a unique
bundle identifier for a physical iPhone. The source project includes no developer
credentials. Its project file is generated from project.yml using XcodeGen.

Run `swift test` here for core protocol, compatibility, identity and persistence
checks. The Xcode scheme includes demo UI journeys for iPhone/iPad. Optional
read-only hardware validation runs when GROWHUB_TEST_HOST is set to a controller
address; normal tests do not contact hardware. CE-to-CE OTA, native API reads and a separate native reboot were verified on a
physical controller; physical outlet switching is checked by the hardware owner.

## Privacy and licensing

Controller identities/addresses, local controller names and cached equipment
labels stay in UserDefaults on the phone. Equipment label edits are saved directly
on the controller and shared with its other local clients. Status/configuration are held in memory. Wi-Fi passwords are not returned
by the API or retained as app settings. There are no analytics/tracking SDKs.
See [privacy](../../docs/growhub-remote/privacy.md) and
[support](../../docs/growhub-remote/support.md). Earlier binary builds embedded
the controller page; the current source uses native screens throughout.

The app and original icon are contributed under this repository's MIT license.
SwiftSoup (HTML parsing for older CE) is MIT licensed; its notices are bundled in
App/Resources/SwiftSoup-LICENSE.txt and shown in Help → Open-source acknowledgements.
Growhub Remote is independently published by Plongee6 LLC and is not affiliated
with or endorsed by NIWA or the CE firmware maintainers.
