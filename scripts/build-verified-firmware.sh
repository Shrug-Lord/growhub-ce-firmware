#!/usr/bin/env bash
# Build firmware with the repo PlatformIO venv, then refresh first-flash assets
# from that exact build output.

set -euo pipefail

SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
REPO_DIR="$(CDPATH='' cd -- "$SCRIPT_DIR/.." && pwd)"
FIRMWARE_DIR="$REPO_DIR/firmware"
PIO_ENV="${PIO_ENV:-growhub}"
PIO_BIN="${PIO_BIN:-$FIRMWARE_DIR/.venv/bin/pio}"
DIST_ROOT="${DIST_ROOT:-$REPO_DIR/dist}"
BUILD_DIR="$FIRMWARE_DIR/.pio/build/$PIO_ENV"

log() {
  printf '%s\n' "$*"
}

die() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 1
}

require_file() {
  [ -f "$1" ] || die "Required file not found: $1"
}

read_firmware_version() {
  sed -nE 's/^[[:space:]]*-DGROWHUB_VERSION=\\?"?([^\\"]+)\\?"?/\1/p' "$FIRMWARE_DIR/platformio.ini" | head -n 1
}

sanitize_version() {
  printf '%s' "$1" | tr -c 'A-Za-z0-9._-' '-'
}

sha256_file() {
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
    return
  fi

  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
    return
  fi

  die "shasum or sha256sum is required to verify release artifacts."
}

main() {
  local raw_version version release_dir bundle_dir zip_path package_core_dir
  local build_firmware release_firmware

  require_file "$FIRMWARE_DIR/platformio.ini"
  [ -x "$PIO_BIN" ] || die "PlatformIO venv command not found: $PIO_BIN"

  raw_version="${VERSION:-}"
  if [ -z "$raw_version" ]; then
    raw_version="$(read_firmware_version)"
    [ -n "$raw_version" ] || die "Could not read GROWHUB_VERSION from firmware/platformio.ini"
    raw_version="v${raw_version#v}"
  fi
  version="$(sanitize_version "$raw_version")"
  [ -n "$version" ] || die "Release version resolved to an empty value."

  log "Building firmware with $PIO_BIN"
  (
    cd "$FIRMWARE_DIR"
    "$PIO_BIN" run -e "$PIO_ENV"
  )

  build_firmware="$BUILD_DIR/firmware.bin"
  require_file "$build_firmware"

  package_core_dir="${PLATFORMIO_CORE_DIR:-}"
  if [ -z "$package_core_dir" ] && [ -n "${HOME:-}" ]; then
    package_core_dir="$HOME/.platformio"
  fi

  log "Refreshing first-flash package for $version"
  if [ -n "$package_core_dir" ]; then
    SKIP_BUILD=1 VERSION="$version" DIST_ROOT="$DIST_ROOT" PIO_ENV="$PIO_ENV" PLATFORMIO_CORE_DIR="$package_core_dir" \
      "$REPO_DIR/scripts/package-first-flash.sh"
  else
    SKIP_BUILD=1 VERSION="$version" DIST_ROOT="$DIST_ROOT" PIO_ENV="$PIO_ENV" \
      "$REPO_DIR/scripts/package-first-flash.sh"
  fi

  release_dir="$DIST_ROOT/growhub-ce-$version"
  bundle_dir="$release_dir/growhub-ce-first-flash-$version"
  zip_path="$release_dir/growhub-ce-first-flash-$version.zip"
  release_firmware="$release_dir/firmware.bin"

  require_file "$release_firmware"
  require_file "$release_dir/merged-firmware.bin"
  require_file "$release_dir/SHA256SUMS"
  require_file "$bundle_dir/SHA256SUMS"
  require_file "$zip_path"

  if ! cmp -s "$build_firmware" "$release_firmware"; then
    die "Packaged firmware.bin does not match PlatformIO build output."
  fi

  if ! cmp -s "$release_dir/SHA256SUMS" "$bundle_dir/SHA256SUMS"; then
    die "Bundle SHA256SUMS does not match release SHA256SUMS."
  fi

  if command -v unzip >/dev/null 2>&1; then
    unzip -t "$zip_path" >/dev/null
  else
    log "unzip not found; skipping ZIP integrity check."
  fi

  log
  log "Verified firmware.bin:"
  log "  $build_firmware"
  log "  sha256: $(sha256_file "$build_firmware")"
  log
  log "First-flash package refreshed:"
  log "  $zip_path"
  log "  sha256: $(sha256_file "$zip_path")"
}

main "$@"
