# Release update planning

Status: implemented locally; automated and targeted bench verification complete.
Full release publication gates remain in [UPDATE-VERIFICATION.md](UPDATE-VERIFICATION.md).

## Release scope

Target the unpublished CE firmware `1.2.0C` and Command Center `0.2.0`.
Command Center `v0.1.0` is already published; its dashboard and journal additions
also belong in the next version. These are planning targets, not release claims.

## Confirmed requirements

- When update checking is enabled, each system periodically checks its official
  GitHub Releases for a newer version and prompts the user to install it.
- A user-approved firmware update downloads and flashes the CE application image.
- A user-approved Command Center update uses its host update script.
- Firmware update prompts and per-device installation are available in Command
  Center device cards as well as each controller's own management page.
- Standalone firmware updates do not require Command Center.
- Periodic update checking defaults to off in both systems; users opt in.
- Every installation requires an explicit Update click. Remove Command Center's
  existing unattended-install option and do not carry an existing enabled
  unattended-install preference forward into automatic installation.
- Each controller owns its firmware-update check preference and check results.
  Its management page uses that state directly; Command Center mirrors the state
  and requests preference changes over MQTT. Both interfaces expose the same
  setting. Enabled controller checks continue when Command Center is offline.
- When enabled, each system checks after startup and every six hours, with a
  small randomized delay for background checks to spread GitHub requests.
- Both interfaces provide Check now. Manual checks remain available when
  periodic checking is disabled.
- Update prompts offer Update, Later, and Skip this version. Later postpones
  prompting for 24 hours; Skip this version suppresses prompts for that exact
  release while leaving it available in Settings. A newer release can prompt
  again. Firmware dismissal state is owned by the controller and synchronized
  between its management page and Command Center.
- Command Center 0.2.0 supports one-click self-updates on Linux/Raspberry Pi
  through its host service. Windows and macOS retain the existing command-line
  updater; native host services for those platforms are outside this scope.
  This host limitation does not restrict firmware updates from device cards.
- Both systems offer only newer published stable releases, excluding drafts and
  prereleases. Existing manual firmware upload remains available for development
  images. Installation targets the exact release shown to and approved by the
  user, rather than resolving a moving latest-release URL at installation time.
- Clicking Update opens a final confirmation showing the target version and
  restart impact before installation begins. Firmware updates briefly interrupt
  outlet control. Command Center updates temporarily interrupt the dashboard
  and logging while controllers continue operating independently.
- Failed installations stop and show recovery actions; they do not automatically
  retry. Preserve firmware's existing boot-health rollback where supported by
  the installed bootloader. Preserve Command Center's pre-update backup and show
  recovery instructions instead of automatically restoring older database data.

## Existing implementation to reuse

CE already supports URL and upload OTA with boot-health rollback support. The
existing OTA documentation excludes automatic checks; that scope must be revised
with implementation. Local, uncommitted HTTPS certificate changes need review
and verification before inclusion.

Command Center already has six-hour release checks, prompts, per-release
dismissal, an unattended-install preference, and a backup-first Compose updater.
Its UI-triggered host updater currently uses a Linux/systemd service. Reuse this
path and remove unattended installation as agreed above.

## Implementation plan

1. Define versioned firmware update state/actions for the local UI and MQTT,
   including preferences, release identity, dismissal, progress, and errors.
   Use the existing ESP-IDF/C firmware and Node/React Command Center stacks.
2. Implement firmware release checking, persistent preferences, exact-release
   HTTPS download and artifact verification, and update UI. Review and integrate
   the local HTTPS certificate changes as part of the official download path.
3. Adapt Command Center's existing self-update service to opt-in checks and
   explicit installation. Add the mirrored per-device firmware controls, migrate
   existing unattended preferences safely, and update package versions to 0.2.0.
4. Exercise failure paths and UI flows, then validate exact firmware and Command
   Center release candidates on hardware/host before freezing release artifacts.

## Implementation details to verify

- Bound GitHub response parsing, TLS memory use, request frequency, and retry
  backoff within ESP32 limits; confirm the final image fits the OTA slot.
- Keep release identity pinned across checking, confirmation, and installation;
  reject stale/duplicate actions and prevent concurrent OTA attempts.
- Show download/install/restart states. Declare success only after observing
  the expected installed version and healthy service/device state; distinguish
  loss of contact from a confirmed failure.
- Persist dismissals and handle unavailable wall time without repeated prompts.
- Preserve settings and journals across migrations. Do not automatically rerun
  failed install requests after a service restart.
- On unsupported hosts or without the host service, explain the command-line
  update path rather than offering a nonfunctional self-update button.

## Acceptance and verification

- Disabled checks make no periodic release requests; manual checks remain usable.
- Compare versions correctly and offer only eligible newer published releases.
- Install the exact release approved by the user, with HTTPS and artifact checks.
- Exercise GitHub outages, interrupted downloads, duplicate requests, and failures.
- Verify preserved device settings and Command Center journal/database data.
- Verify firmware slot size, OTA boot health, and recovery on hardware using the
  exact release candidate; verify Command Center backup and post-update readiness.
- Verify both firmware-update interfaces and the standalone path.
