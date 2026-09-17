#!/usr/bin/env bash
set -euo pipefail
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
control="$root/advance-shape-control-v1"
test "$(cat "$root/batch-launch-focused-control-v1/exit-code")" = 0
test -s "$root/batch-launch-focused-local-receipt-v1.json"
test ! -e "$control"
test ! -e "$root/advance-shape-diagnostic-v1"
bash -n "$root/advance-shape-input-v1/worker.sh"
mkdir "$control"
nohup bash "$root/advance-shape-input-v1/worker.sh" > "$control/worker.log" 2>&1 </dev/null &
printf '%s\n' "$!" > "$control/pid"
printf 'ADVANCE_SHAPE_DIAGNOSTIC_STARTED pid='
cat "$control/pid"
