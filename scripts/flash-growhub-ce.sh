#!/usr/bin/env bash
# Flash a released Growhub CE first-flash bundle over UART.
#
# This script is meant to ship inside growhub-ce-first-flash-<version>.zip.
# It does not require the firmware source tree or PlatformIO.
#
# Usage:
#   ./flash-growhub-ce.sh
#   PORT=/dev/cu.usbserial-0001 ./flash-growhub-ce.sh
#   BAUD=460800 ./flash-growhub-ce.sh
#   YES=1 ./flash-growhub-ce.sh
#   STOCK_BACKUP=0 ./flash-growhub-ce.sh

set -euo pipefail

SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
VENV_DIR="$SCRIPT_DIR/.venv"
PYTHON_BIN="$VENV_DIR/bin/python"
PIP_BIN="$VENV_DIR/bin/pip"
MERGED_BIN="$SCRIPT_DIR/merged-firmware.bin"
CHECKSUMS_FILE="$SCRIPT_DIR/SHA256SUMS"
DEFAULT_BACKUP_DIR="$SCRIPT_DIR/stock-backups"

PORT="${PORT:-}"
BAUD="${BAUD:-230400}"
YES="${YES:-0}"
DRY_RUN="${DRY_RUN:-0}"
STOCK_BACKUP="${STOCK_BACKUP:-1}"
BACKUP_DIR="${BACKUP_DIR:-$DEFAULT_BACKUP_DIR}"
ESPTOOL_REQUIREMENT="${ESPTOOL_REQUIREMENT:-esptool>=4.8,<5}"
DETECTED_MAC=""

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/growhub-release-flash.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT

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
    die "python3 is required but was not found. Install Python 3, then run this script again."
  fi
}

ensure_venv() {
  if [ ! -x "$PYTHON_BIN" ]; then
    log "Creating local Python environment in $VENV_DIR"
    python3 -m venv "$VENV_DIR"
  fi
}

ensure_esptool() {
  if "$PYTHON_BIN" -m esptool version >/dev/null 2>&1; then
    return
  fi

  log "Installing esptool into $VENV_DIR"
  "$PYTHON_BIN" -m pip install --upgrade pip
  "$PIP_BIN" install "$ESPTOOL_REQUIREMENT"
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

file_size() {
  case "$(uname -s)" in
    Darwin|FreeBSD|OpenBSD|NetBSD)
      stat -f%z "$1"
      ;;
    *)
      stat -c%s "$1"
      ;;
  esac
}

verify_checksum() {
  local expected actual

  if [ ! -f "$CHECKSUMS_FILE" ]; then
    log "No SHA256SUMS file found; skipping checksum verification."
    return
  fi

  expected="$(awk '$2 == "merged-firmware.bin" {print $1}' "$CHECKSUMS_FILE")"
  if [ -z "$expected" ]; then
    log "SHA256SUMS does not include merged-firmware.bin; skipping checksum verification."
    return
  fi

  actual="$(sha256_file "$MERGED_BIN")"
  if [ "$actual" != "$expected" ]; then
    die "Checksum mismatch for merged-firmware.bin. Download the release ZIP again."
  fi

  log "Checksum verified for merged-firmware.bin"
}

collect_ports() {
  "$PYTHON_BIN" - <<'PY'
from serial.tools import list_ports

IGNORE_TERMS = ("bluetooth", "debug-console")
BOOST_TERMS = ("cp210", "usb", "uart", "serial", "ftdi", "ch340", "silicon labs", "ttl")

candidates = []
for port in list_ports.comports():
    device = port.device or ""
    description = port.description or "n/a"
    hwid = port.hwid or "n/a"
    combined = f"{device} {description} {hwid}".lower()
    if not device:
        continue
    if any(term in combined for term in IGNORE_TERMS):
        continue

    score = 0
    if port.vid is not None and port.pid is not None:
        score += 5
    for term in BOOST_TERMS:
        if term in combined:
            score += 1

    candidates.append((score, device, description, hwid))

candidates.sort(key=lambda item: (-item[0], item[1]))
for score, device, description, hwid in candidates:
    print(f"{device}\t{description}\t{hwid}\t{score}")
PY
}

choose_port_interactively() {
  local -a ports descriptions
  local device description count choice i

  while true; do
    ports=()
    descriptions=()

    while IFS=$'\t' read -r device description _ _; do
      [ -n "${device:-}" ] || continue
      ports+=("$device")
      descriptions+=("${description:-n/a}")
    done <<EOF
$(collect_ports)
EOF

    count="${#ports[@]}"
    if [ "$count" -eq 0 ]; then
      log "Waiting for a USB-to-UART adapter. Plug it in and leave the Growhub powered."
      sleep 2
      continue
    fi

    if [ "$count" -eq 1 ]; then
      PORT="${ports[0]}"
      log "Detected USB serial adapter: $PORT (${descriptions[0]})"
      return
    fi

    log "Multiple serial adapters were found:"
    i=1
    while [ "$i" -le "$count" ]; do
      log "  $i) ${ports[$((i - 1))]} - ${descriptions[$((i - 1))]}"
      i=$((i + 1))
    done
    printf 'Choose the adapter connected to the Growhub [1-%s]: ' "$count"
    read -r choice
    case "$choice" in
      ''|*[!0-9]*)
        log "Please enter a number from 1 to $count."
        ;;
      *)
        if [ "$choice" -ge 1 ] && [ "$choice" -le "$count" ]; then
          PORT="${ports[$((choice - 1))]}"
          log "Using serial adapter: $PORT"
          return
        fi
        log "Please enter a number from 1 to $count."
        ;;
    esac
  done
}

ensure_port() {
  if [ -n "$PORT" ]; then
    [ -e "$PORT" ] || die "UART device '$PORT' not found. Check the adapter or set PORT=..."
    log "Using serial adapter: $PORT"
    return
  fi

  choose_port_interactively
}

confirm_overwrite() {
  if [ "$YES" = "1" ]; then
    return
  fi

  log
  log "This will overwrite the firmware currently on the Growhub."
  if [ "$STOCK_BACKUP" = "0" ]; then
    log "Stock firmware backup is disabled because STOCK_BACKUP=0 is set."
  else
    log "The script will dump a 4 MB backup before writing CE firmware."
  fi
  printf 'Press Enter to continue or Ctrl+C to cancel: '
  read -r _
}

remember_detected_mac() {
  local probe_log="$1"
  local mac_value

  mac_value="$(sed -nE 's/.*MAC: *([0-9A-Fa-f:]+).*/\1/p' "$probe_log" | head -n 1 | tr -d ':' | tr '[:lower:]' '[:upper:]')"
  if [ -n "$mac_value" ]; then
    DETECTED_MAC="$mac_value"
  fi
}

wait_for_bootloader() {
  local attempt=0
  local probe_log="$TMP_DIR/esptool-probe.log"

  log
  log "Wire the adapter like this:"
  log "  TXD -> R"
  log "  RXD -> T"
  log "  GND -> G"
  log "  Leave 3.3V / VCC disconnected"
  log
  log "To enter download mode:"
  log "  1. Bridge O to G"
  log "  2. Power-cycle the Growhub while the bridge is held"
  log "  3. Leave the bridge in place until flashing starts"
  log
  log "Waiting for the ESP32 bootloader on $PORT ..."

  while true; do
    if [ ! -e "$PORT" ]; then
      attempt=$((attempt + 1))
      if [ $((attempt % 5)) -eq 1 ]; then
        log "  $PORT is not present yet. Waiting for the adapter to reconnect."
      fi
      sleep 2
      continue
    fi

    if "$PYTHON_BIN" -m esptool \
      --chip esp32 \
      --port "$PORT" \
      --baud 115200 \
      --before no_reset \
      --after no_reset \
      --connect-attempts 1 \
      read_mac >"$probe_log" 2>&1; then
      log "ESP32 bootloader detected."
      sed -n '1,12p' "$probe_log"
      remember_detected_mac "$probe_log"
      return
    fi

    attempt=$((attempt + 1))
    if [ $((attempt % 5)) -eq 1 ]; then
      log "  Not ready yet. Keep O bridged to G and power-cycle again if needed."
      log "  Close any serial monitor or other app that may have the port open."
    fi
    sleep 2
  done
}

dump_stock_firmware() {
  local timestamp mac_label backup_path digest size

  if [ "$STOCK_BACKUP" = "0" ]; then
    log
    log "Skipping stock firmware backup because STOCK_BACKUP=0 is set."
    return
  fi

  timestamp="$(date -u '+%Y%m%dT%H%M%SZ')"
  mac_label="${DETECTED_MAC:-unknown-mac}"
  backup_path="$BACKUP_DIR/growhub-stock-$mac_label-$timestamp.bin"

  mkdir -p "$BACKUP_DIR"

  log
  log "Backing up current 4 MB flash before writing CE firmware."
  log "Backup file: $backup_path"
  log "This can take a few minutes."

  "$PYTHON_BIN" -m esptool \
    --chip esp32 \
    --port "$PORT" \
    --baud "$BAUD" \
    --before no_reset \
    --after no_reset \
    read_flash \
    0x0 0x400000 "$backup_path"

  size="$(file_size "$backup_path")"
  if [ "$size" != "4194304" ]; then
    die "Backup size was $size bytes, expected 4194304. CE firmware was not flashed."
  fi

  digest="$(sha256_file "$backup_path")"
  printf '%s  %s\n' "$digest" "$(basename "$backup_path")" > "$backup_path.sha256"

  log "Stock firmware backup complete."
  log "Backup SHA256: $digest"
  log "Checksum file: $backup_path.sha256"
  log "Keep this backup private; it may contain device-specific settings."
}

flash_firmware() {
  log
  log "Flashing Growhub CE firmware to $PORT at $BAUD baud"
  "$PYTHON_BIN" -m esptool \
    --chip esp32 \
    --port "$PORT" \
    --baud "$BAUD" \
    --before no_reset \
    --after no_reset \
    write_flash \
    --flash_mode dio \
    --flash_freq 40m \
    --flash_size 4MB \
    0x0 "$MERGED_BIN"
}

handle_dry_run() {
  if [ "$DRY_RUN" != "1" ]; then
    return
  fi

  log
  log "Dry run complete."
  log "Bundle directory: $SCRIPT_DIR"
  log "Firmware image: $MERGED_BIN"
  log "Resolved port: $PORT"
  if [ "$STOCK_BACKUP" = "0" ]; then
    log "Stock backup: disabled"
  else
    log "Stock backup directory: $BACKUP_DIR"
  fi
  exit 0
}

print_success() {
  log
  log "Flash complete."
  log "Remove the O-to-G bridge, then power-cycle the Growhub normally."
  log "After CE boots, connect to the growhub_<last4mac> WiFi AP and open http://192.168.4.1"
}

main() {
  require_file "$MERGED_BIN"

  ensure_python
  ensure_venv
  ensure_esptool
  verify_checksum
  ensure_port
  handle_dry_run
  confirm_overwrite
  wait_for_bootloader
  dump_stock_firmware
  flash_firmware
  print_success
}

main "$@"
