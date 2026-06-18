# Security policy

## Supported versions

Only the latest tagged release on `main` receives security fixes. Prior versions are not patched. Upgrade if you're behind.

## Threat model

CE firmware is designed for a **trusted LAN**. The threat model assumes:

- The WiFi network the device joins is operated by the device's owner.
- The MQTT broker (if used) is reachable only on the LAN, or behind a VPN the owner controls.
- The user's laptop / phone / browser used to configure the device is trusted.

What CE firmware does **not** defend against:

- A hostile WiFi network. The device authenticates to WiFi but does not validate the network beyond that.
- A hostile MQTT broker. The device trusts whatever broker it's pointed at; messages on subscribed topics are acted on without further authentication.
- A hostile user on the same LAN. The device's web UI (port 80) has no authentication. Anyone who can reach `http://<device-ip>/` can change WiFi credentials, trigger OTA, or toggle outlets.
- A hostile firmware URL during OTA. The device fetches the URL the user supplies and writes the bytes to the OTA partition. The user is responsible for trusting the URL. CE firmware does not verify signatures.

If your deployment cannot meet the trusted-LAN assumption, do not run CE firmware on it.

## OTA URL trust

The OTA mechanism accepts a URL (via web UI, MQTT, or `/ota_upload` POST) and downloads its contents into the next OTA partition. There is no signature check, no version check, and no allowlist of trusted hosts.

**This is by design.** Adding signature verification would require shipping a public key in firmware and managing a private signing key — a substantial infrastructure burden for a small community project. We chose to put trust in the user instead: only flash from URLs you control or trust (your own GitHub Releases, a Pi on your LAN, etc.).

Rollback safety (per ADR-0002, where applicable) reduces the cost of a bad OTA: if the new image fails to boot cleanly, the device falls back to the previous slot.

## Reporting a vulnerability

If you find a security issue in CE firmware:

- For issues with **practical impact on devices in the field** (e.g., remote-controllable behavior beyond what the documented threat model allows): use [GitHub Security Advisories](https://github.com/Shrug-Lord/Growhub-CE-Firmware/security/advisories) to report privately. We'll triage and respond.
- For issues that match the documented threat model (e.g., "anyone on my LAN can toggle the outlets"): that's intentional. See "Threat model" above. Open a regular discussion if you want to talk about hardening options.

We don't run a bug bounty.
