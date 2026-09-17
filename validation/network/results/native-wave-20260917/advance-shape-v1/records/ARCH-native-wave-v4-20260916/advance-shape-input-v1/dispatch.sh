#!/usr/bin/env bash
set -euo pipefail
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
control="$root/advance-shape-control-v1"
# Original wrapper failed only at transcript parsing; keep its exit=1 intact.
# The Python runner verifies the separate completed scientific re-audit.
test "$(cat "$root/batch-launch-focused-control-v1/exit-code")" = 1
test -s "$root/batch-launch-focused-reaudit-local-receipt-v1.json"
test ! -e "$control"
test ! -e "$root/advance-shape-diagnostic-v1"
bash -n "$root/advance-shape-input-v1/worker.sh"
mkdir "$control"
nohup bash "$root/advance-shape-input-v1/worker.sh" > "$control/worker.log" 2>&1 </dev/null &
printf '%s\n' "$!" > "$control/pid"
printf 'ADVANCE_SHAPE_DIAGNOSTIC_STARTED pid='
cat "$control/pid"
