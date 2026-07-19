#!/usr/bin/env bash
# Build release assets for first-time UART flashing without cloning/building.
#
# Normal local verified build:
#   scripts/build-verified-firmware.sh
#
# Lower-level packaging usage:
#   scripts/package-first-flash.sh
#   VERSION=v1.1.0C scripts/package-first-flash.sh
#   SKIP_BUILD=1 scripts/package-first-flash.sh

set -euo pipefail

SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
REPO_DIR="$(CDPATH='' cd -- "$SCRIPT_DIR/.." && pwd)"
FIRMWARE_DIR="$REPO_DIR/firmware"
VENV_DIR="$FIRMWARE_DIR/.venv"
PYTHON_BIN="$VENV_DIR/bin/python"
PIP_BIN="$VENV_DIR/bin/pip"
PIO_BIN="$VENV_DIR/bin/pio"
PLATFORMIO_CORE_DIR="${PLATFORMIO_CORE_DIR:-}"
if [ -z "$PLATFORMIO_CORE_DIR" ]; then
  if [ -n "${HOME:-}" ]; then
    PLATFORMIO_CORE_DIR="$HOME/.platformio"
  else
    PLATFORMIO_CORE_DIR="$FIRMWARE_DIR/.platformio-core"
  fi
fi
PIO_ENV="${PIO_ENV:-growhub}"
BUILD_DIR="$FIRMWARE_DIR/.pio/build/$PIO_ENV"

VERSION="${VERSION:-}"
SKIP_BUILD="${SKIP_BUILD:-0}"
DIST_ROOT="${DIST_ROOT:-$REPO_DIR/dist}"
ESPTOOL_REQUIREMENT="${ESPTOOL_REQUIREMENT:-esptool>=4.8,<5}"
ESPTOOL_RUNNER=""

BOOTLOADER_BIN="$BUILD_DIR/bootloader.bin"
PARTITIONS_BIN="$BUILD_DIR/partitions.bin"
OTADATA_BIN="$BUILD_DIR/ota_data_initial.bin"
FIRMWARE_BIN="$BUILD_DIR/firmware.bin"
RELEASE_FLASHER="$REPO_DIR/scripts/flash-growhub-ce.sh"

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

ensure_python() {
  if ! command -v python3 >/dev/null 2>&1; then
    die "python3 is required but was not found in PATH."
  fi
}

ensure_venv() {
  if [ ! -x "$PYTHON_BIN" ]; then
    log "Creating Python virtual environment in $VENV_DIR"
    python3 -m venv "$VENV_DIR"
  fi
}

ensure_pip() {
  if "$PYTHON_BIN" -m pip --version >/dev/null 2>&1; then
    return
  fi

  log "Bootstrapping pip in $VENV_DIR"
  if ! "$PYTHON_BIN" -m ensurepip --upgrade --default-pip; then
    die "pip is missing from $VENV_DIR and could not be bootstrapped with ensurepip."
  fi
  "$PYTHON_BIN" -m pip --version >/dev/null 2>&1 || \
    die "pip is still unavailable after ensurepip completed."
}

ensure_platformio() {
  if [ "$SKIP_BUILD" = "1" ]; then
    return
  fi

  if [ ! -x "$PIO_BIN" ]; then
    log "Installing PlatformIO into $VENV_DIR"
    "$PYTHON_BIN" -m pip install --upgrade pip
    "$PIP_BIN" install "platformio>=6,<7"
  fi
}

ensure_esptool() {
  local candidate

  if "$PYTHON_BIN" -m esptool version >/dev/null 2>&1; then
    ESPTOOL_RUNNER="module"
    return
  fi

  for candidate in \
    "$PLATFORMIO_CORE_DIR/packages/tool-esptoolpy/esptool.py" \
    "$HOME/.platformio/packages/tool-esptoolpy/esptool.py"; do
    if [ -f "$candidate" ] && "$PYTHON_BIN" "$candidate" version >/dev/null 2>&1; then
      ESPTOOL_RUNNER="$candidate"
      return
    fi
  done

  log "Installing esptool into $VENV_DIR"
  "$PYTHON_BIN" -m pip install --upgrade pip
  "$PIP_BIN" install "$ESPTOOL_REQUIREMENT"
  ESPTOOL_RUNNER="module"
}

run_esptool() {
  if [ "$ESPTOOL_RUNNER" = "module" ]; then
    "$PYTHON_BIN" -m esptool "$@"
    return
  fi

  "$PYTHON_BIN" "$ESPTOOL_RUNNER" "$@"
}

detect_version() {
  local raw sanitized

  if [ -z "$VERSION" ]; then
    raw="$(sed -nE 's/^[[:space:]]*-DGROWHUB_VERSION=\\?"?([^\\"]+)\\?"?/\1/p' \
      "$FIRMWARE_DIR/platformio.ini" | head -n 1)"
    [ -n "$raw" ] || die "Could not read GROWHUB_VERSION from firmware/platformio.ini"
    VERSION="v${raw#v}"
  fi

  sanitized="$(printf '%s' "$VERSION" | tr -c 'A-Za-z0-9._-' '-')"
  VERSION="${sanitized:-dev}"
}

build_firmware() {
  if [ "$SKIP_BUILD" = "1" ]; then
    log "Skipping PlatformIO build because SKIP_BUILD=1"
    return
  fi

  log "Building firmware with PlatformIO"
  (
    cd "$FIRMWARE_DIR"
    PLATFORMIO_CORE_DIR="$PLATFORMIO_CORE_DIR" "$PIO_BIN" run -e "$PIO_ENV"
  )
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

  "$PYTHON_BIN" - "$1" <<'PY'
import hashlib
import sys

with open(sys.argv[1], "rb") as fh:
    print(hashlib.file_digest(fh, "sha256").hexdigest())
PY
}

write_sha256sums() {
  local output_dir="$1"
  local sums_file="$output_dir/SHA256SUMS"
  local file base digest

  : > "$sums_file"
  for file in \
    "$output_dir/firmware.bin" \
    "$output_dir/bootloader.bin" \
    "$output_dir/partitions.bin" \
    "$output_dir/ota_data_initial.bin" \
    "$output_dir/merged-firmware.bin"; do
    base="$(basename "$file")"
    digest="$(sha256_file "$file")"
    printf '%s  %s\n' "$digest" "$base" >> "$sums_file"
  done
}

write_bundle_readme() {
  local output_file="$1"

  cat > "$output_file" <<'EOF'
Growhub CE First Flash
======================

This bundle flashes Growhub CE firmware to an original NIWA Growhub or a
Growhub+ over UART.

Growhub CE is provided as-is, without warranty. Opening the controller and
flashing third-party firmware can render the device unusable if wiring, power,
or flashing steps are wrong. You are responsible for deciding whether to install
it and for any damage, data loss, or device failure that may result.

Growhub+ UART labels:

  G O V T R G

Original Growhub UART labels:

  GND Boot 3.3V TX RX GND

Hardware wiring (the signal names are equivalent):

  USB-to-TTL TXD -> Growhub+ R / original Growhub RX
  USB-to-TTL RXD -> Growhub+ T / original Growhub TX
  USB-to-TTL GND -> Growhub+ G / original Growhub GND
  Leave USB-to-TTL 3.3V / VCC disconnected.
  Power the Growhub from its own power supply.

To flash:

  1. Bridge O to G on Growhub+, or Boot to GND on the original Growhub.
  2. Power-cycle the Growhub while the Boot signal is grounded.
  3. Run:

       ./flash-growhub-ce.sh

  4. Leave the Boot-to-ground bridge connected until flashing starts.
  5. The script backs up the current 4 MB flash before writing CE firmware.
  6. When the script finishes, remove the bridge and power-cycle normally.

Stock firmware backups are written to:

  stock-backups/

Keep backup files private. They may contain device-specific settings.

If the script chooses the wrong serial adapter, run it with PORT set:

  PORT=/dev/cu.usbserial-0001 ./flash-growhub-ce.sh
  PORT=/dev/ttyUSB0 ./flash-growhub-ce.sh

To skip the backup for advanced recovery work:

  STOCK_BACKUP=0 ./flash-growhub-ce.sh

After CE boots, connect to the growhub_<last4mac> WiFi AP and open:

  http://192.168.4.1
EOF
}

make_zip() {
  local release_dir="$1"
  local bundle_name="$2"
  local zip_path="$3"

  (
    cd "$release_dir"
    "$PYTHON_BIN" -m zipfile -c "$zip_path" "$bundle_name"
  )
}

create_release_assets() {
  local release_dir="$DIST_ROOT/growhub-ce-$VERSION"
  local bundle_name="growhub-ce-first-flash-$VERSION"
  local bundle_dir="$release_dir/$bundle_name"
  local zip_path="$release_dir/$bundle_name.zip"

  require_file "$BOOTLOADER_BIN"
  require_file "$PARTITIONS_BIN"
  require_file "$OTADATA_BIN"
  require_file "$FIRMWARE_BIN"
  require_file "$RELEASE_FLASHER"

  rm -rf "$release_dir"
  mkdir -p "$bundle_dir"

  cp "$FIRMWARE_BIN" "$release_dir/firmware.bin"
  cp "$BOOTLOADER_BIN" "$release_dir/bootloader.bin"
  cp "$PARTITIONS_BIN" "$release_dir/partitions.bin"
  cp "$OTADATA_BIN" "$release_dir/ota_data_initial.bin"

  log "Creating merged first-flash image"
  run_esptool \
    --chip esp32 \
    merge_bin \
    --output "$release_dir/merged-firmware.bin" \
    --flash_mode dio \
    --flash_freq 40m \
    --flash_size 4MB \
    0x1000 "$release_dir/bootloader.bin" \
    0x8000 "$release_dir/partitions.bin" \
    0xf000 "$release_dir/ota_data_initial.bin" \
    0x20000 "$release_dir/firmware.bin"

  write_sha256sums "$release_dir"

  cp "$RELEASE_FLASHER" "$bundle_dir/flash-growhub-ce.sh"
  chmod +x "$bundle_dir/flash-growhub-ce.sh"
  cp "$release_dir/merged-firmware.bin" "$bundle_dir/merged-firmware.bin"
  cp "$release_dir/SHA256SUMS" "$bundle_dir/SHA256SUMS"
  write_bundle_readme "$bundle_dir/README-FIRST-FLASH.txt"

  make_zip "$release_dir" "$bundle_name" "$zip_path"

  log
  log "Release assets written to:"
  log "  $release_dir"
  log
  log "First-flash ZIP:"
  log "  $zip_path"
}

main() {
  require_file "$FIRMWARE_DIR/platformio.ini"

  export PLATFORMIO_CORE_DIR

  ensure_python
  ensure_venv
  ensure_pip
  ensure_platformio
  detect_version
  build_firmware
  ensure_esptool
  create_release_assets
}

main "$@"
