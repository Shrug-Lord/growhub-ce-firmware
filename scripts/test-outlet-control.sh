#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cjson_dir="${CJSON_DIR:-${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/framework-espidf/components/json/cJSON}"
if [[ ! -f "$cjson_dir/cJSON.c" ]]; then
    echo 'Set CJSON_DIR to the cJSON source directory (included with the ESP-IDF build tools).' >&2
    exit 1
fi
outlet_test_bin="$(mktemp -t growhub-outlet-test.XXXXXX)"
trap 'rm -f "$outlet_test_bin"' EXIT
"${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-unused-parameter \
    -fsanitize=address,undefined -g -pthread \
    -Itests/stubs -Ifirmware/src -I"$cjson_dir" \
    tests/outlet_control_test.c firmware/src/relays.c firmware/src/schedule.c \
    "$cjson_dir/cJSON.c" -lm -o "$outlet_test_bin"
ASAN_OPTIONS=detect_leaks=0 "$outlet_test_bin"
