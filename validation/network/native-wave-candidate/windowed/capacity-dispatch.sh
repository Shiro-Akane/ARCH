#!/usr/bin/env bash
set -euo pipefail
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
control="$root/window-capacity-control-v1"
test "$(cat "$root/window-focused-control-v1/exit-code")" = 0
test -s "$root/window-focused-collection-v1.json"
test -s "$root/window-focused-local-receipt-v1.json"
test ! -e "$control"
test ! -e "$root/window-capacity-v1"
bash -n "$root/window-capacity-input-v1/capacity-worker.sh"
mkdir "$control"
nohup bash "$root/window-capacity-input-v1/capacity-worker.sh" > "$control/worker.log" 2>&1 </dev/null &
printf '%s\n' "$!" > "$control/pid"
printf 'WINDOW_CAPACITY_STARTED pid='
cat "$control/pid"
