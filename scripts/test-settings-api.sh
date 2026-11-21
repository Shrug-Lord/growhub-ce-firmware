#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cjson_dir="${CJSON_DIR:-${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/framework-espidf/components/json/cJSON}"
api_test_bin="$(mktemp -t growhub-settings-test.XXXXXX)"
trap 'rm -f "$api_test_bin"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -fsanitize=address,undefined -g \
    -Ifirmware/src -I"$cjson_dir" tests/settings_snapshot_test.c \
    firmware/src/settings_snapshot.c "$cjson_dir/cJSON.c" -o "$api_test_bin"
ASAN_OPTIONS=detect_leaks=0 "$api_test_bin"
