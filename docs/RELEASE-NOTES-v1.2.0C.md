# Growhub CE v1.2.0C Development Notes

Status: development version; release artifacts are not yet frozen or published.

## Automatic device management address

Firmware now publishes its current Wi-Fi station IPv4 address and HTTP port on
`growhub/<MAC>/network/state`, using a versioned JSON payload, QoS 1, and MQTT retain.
Command Center can follow DHCP changes automatically and open the device's own
management page without storing a manual IP address.

The address is checked approximately once per second, independently of the sensor
reporting interval, and is republished on MQTT connection/reconnection. An address
change while MQTT still considers the old socket connected requests an asynchronous
disconnect so automatic reconnection can replace that socket. The device's MAC
identity, outlet configuration, schedules, and grow history remain independent of
its address.

This is a backward-compatible MQTT addition, so the development firmware advances
from `1.1.0C` to `1.2.0C` under the [version policy](RELEASES.md#version-policy).
Both the displayed firmware version and the ESP-IDF application descriptor are
set to `1.2.0C`. Command Center is versioned separately.

## Opt-in release updates

Adds periodic stable-release checks, manual Check now, release prompts, Later,
Skip this version, and explicitly confirmed installation in the standalone page
and Command Center 0.2.0 device cards. Checking defaults off; installation is never
unattended. Official downloads verify HTTPS, SHA-256, image size, and embedded
version before changing the boot partition. See [OTA reference](OTA.md).

## Compatibility

- Existing CE 1.1.0C topics, schedules, and control behavior remain supported.
- Older Command Center versions can ignore the additional topic.
- Updated Command Center treats network state as optional; older firmware remains
  usable without a management link.
- Presence remains separate from reachability and the last reported address.
- No new Wi-Fi, broker, or device-specific defaults are introduced.

See [MQTT reference](MQTT.md#optional-management-address-120c) for the payload.

## Prior management-address evidence

The address-reporting implementation was tested on a selected bench controller:
initial retained reporting, an actual DHCP change after reboot/reconnect, the new
management page, stable MAC identity and outlet state, and duplicate-free retained
replay through the Command Center mirror. The companion application's isolated
tests also verified preserved journal history and an automatically refreshed link.
That hardware run used a development image still labeled `1.1.0C`; it is feature
evidence, not validation of a frozen `v1.2.0C` release artifact. In-place DHCP lease
replacement without a reconnect was not forced on hardware.

Before a tagged release, retain the Linux CI candidate, validate that exact image
on hardware, and freeze its hashes in `release-manifests/v1.2.0C.sha256`, following
the [release process](RELEASES.md). The published `v1.1.0C` tag and artifact hashes
remain immutable.

## Current update verification and release status

The 1.2.0C development update candidate was built, packaged, and installed on the
selected bench device. Live GitHub discovery and bidirectional MQTT preference
synchronization passed. See [UPDATE-VERIFICATION.md](UPDATE-VERIFICATION.md) for
exact binary evidence and remaining release gates. This is not yet a published
or frozen release.
