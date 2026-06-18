#!/usr/bin/env bash
# Restore the original NIWA stock firmware over UART.
#
# Requires you to already have niwa-firmware.bin — a 4 MB dump of the stock
# firmware, obtained by running `esptool read-flash 0x0 0x400000 niwa-firmware.bin`
# on your own device while it is still running stock firmware.
#
# Usage:
#   scripts/restore-stock.sh                       # uses default port + firmware path
#   PORT=/dev/ttyUSB0 scripts/restore-stock.sh     # override UART port
#   FIRMWARE=path/to/dump.bin scripts/restore-stock.sh  # override firmware path

set -euo pipefail

PORT="${PORT:-/dev/cu.usbserial-0001}"
FIRMWARE="${FIRMWARE:-niwa-firmware.bin}"
BAUD="${BAUD:-230400}"

if [ ! -e "$PORT" ]; then
  echo "ERROR: UART device '$PORT' not found." >&2
  echo "       Set PORT=... to point at your USB-to-TTL adapter." >&2
  echo "       Common values: /dev/cu.usbserial-0001 (macOS CP2102)," >&2
  echo "                      /dev/cu.SLAB_USBtoUART (macOS SiLabs)," >&2
  echo "                      /dev/ttyUSB0           (Linux)." >&2
  exit 1
fi

if [ ! -f "$FIRMWARE" ]; then
  echo "ERROR: firmware file '$FIRMWARE' not found." >&2
  echo "       This script expects a stock-firmware dump (~4 MB)." >&2
  echo "       Create one from your own stock unit with esptool read_flash before restoring." >&2
  exit 1
fi

if ! command -v esptool >/dev/null 2>&1; then
  echo "ERROR: esptool not in PATH." >&2
  echo "       Install with: pip install esptool" >&2
  exit 1
fi

echo "Restoring stock firmware:"
echo "  port:     $PORT"
echo "  firmware: $FIRMWARE"
echo "  baud:     $BAUD"
echo
echo "Make sure the device is in download mode (GPIO0 held LOW at boot —"
echo "bridge the O pad to the G pad on the UART header, then power cycle)."
echo

esptool --port "$PORT" --baud "$BAUD" write_flash 0x0 "$FIRMWARE"
