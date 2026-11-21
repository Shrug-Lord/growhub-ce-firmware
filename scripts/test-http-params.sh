#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
params_test_bin="$(mktemp -t growhub-params-test.XXXXXX)"
trap 'rm -f "$params_test_bin"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -Ifirmware/src tests/form_params_test.c -o "$params_test_bin"
ASAN_OPTIONS=detect_leaks=0 "$params_test_bin"
