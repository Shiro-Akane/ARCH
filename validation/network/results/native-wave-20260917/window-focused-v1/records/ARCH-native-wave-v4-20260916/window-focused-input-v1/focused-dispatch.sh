#!/usr/bin/env bash
set -euo pipefail
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
control="$root/window-focused-control-v1"
test "$(cat "$root/window-factory-control-v1/exit-code")" = 0
test -s "$root/window-factory-collection-v2.json"
test -s "$root/window-factory-local-receipt-v2.json"
test ! -e "$control"
test ! -e "$root/window-focused-v1"
# Exact prior source/product/archive checks happen before running any physics.
bash -n "$root/window-focused-input-v1/focused-worker.sh"
mkdir "$control"
nohup bash "$root/window-focused-input-v1/focused-worker.sh" > "$control/worker.log" 2>&1 </dev/null &
printf '%s\n' "$!" > "$control/pid"
printf 'WINDOW_FOCUSED_STARTED pid='
cat "$control/pid"
