#!/usr/bin/env bash
# Build Growhub CE firmware and copy firmware.bin to the directory where this
# script was invoked from.

set -euo pipefail

SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
START_DIR="$PWD"
PIO_ENV="${PIO_ENV:-growhub}"
OUT_NAME="${OUT_NAME:-firmware.bin}"
OUT_DIR="${OUT_DIR:-$START_DIR}"

log() {
  printf '%s\n' "$*"
}

die() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 1
}

find_repo_dir() {
  local candidate

  if [ -n "${GROWHUB_REPO_DIR:-}" ]; then
    candidate="$GROWHUB_REPO_DIR"
    [ -f "$candidate/firmware/platformio.ini" ] && {
      printf '%s\n' "$candidate"
      return
    }
    die "GROWHUB_REPO_DIR does not point at a Growhub CE repo: $candidate"
  fi

  for candidate in \
    "$SCRIPT_DIR" \
    "$SCRIPT_DIR/.." \
    "$SCRIPT_DIR/Growhub-CE-Firmware" \
    "$SCRIPT_DIR/../Growhub-CE-Firmware"; do
    if [ -f "$candidate/firmware/platformio.ini" ]; then
      CDPATH='' cd -- "$candidate" && pwd
      return
    fi
  done

  die "Could not find Growhub-CE-Firmware near $SCRIPT_DIR"
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

  log "sha256 unavailable; skipping checksum."
}

main() {
  local repo_dir firmware_dir pio_bin pio_core_dir build_dir source_bin target_bin checksum

  repo_dir="$(find_repo_dir)"
  firmware_dir="$repo_dir/firmware"
  pio_bin="${PIO_BIN:-$firmware_dir/.venv/bin/pio}"
  pio_core_dir="${PLATFORMIO_CORE_DIR:-}"
  build_dir="$firmware_dir/.pio/build/$PIO_ENV"
  source_bin="$build_dir/firmware.bin"
  target_bin="$OUT_DIR/$OUT_NAME"

  [ -x "$pio_bin" ] || die "PlatformIO command not found or not executable: $pio_bin"
  mkdir -p "$OUT_DIR"

  log "Building Growhub CE firmware"
  log "  repo: $repo_dir"
  log "  env:  $PIO_ENV"
  (
    cd "$firmware_dir"
    if [ -n "$pio_core_dir" ]; then
      PLATFORMIO_CORE_DIR="$pio_core_dir" "$pio_bin" run -e "$PIO_ENV"
    else
      "$pio_bin" run -e "$PIO_ENV"
    fi
  )

  [ -f "$source_bin" ] || die "Build succeeded but firmware.bin was not found: $source_bin"

  cp -f "$source_bin" "$target_bin"

  log
  log "Copied firmware:"
  log "  $target_bin"
  checksum="$(sha256_file "$target_bin" || true)"
  if [ -n "$checksum" ]; then
    log "  sha256: $checksum"
  fi
}

main "$@"
