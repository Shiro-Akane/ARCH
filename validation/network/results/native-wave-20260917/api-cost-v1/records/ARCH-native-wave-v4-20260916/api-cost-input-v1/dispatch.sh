#!/usr/bin/env bash
set -euo pipefail
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
control="$root/api-cost-control-v1"
test "$(cat "$root/advance-shape-control-v1/exit-code")" = 0
test -s "$root/advance-shape-local-receipt-v1.json"
test ! -e "$control"
test ! -e "$root/api-cost-focused-v1"
bash -n "$root/api-cost-input-v1/worker.sh"
mkdir "$control"
nohup bash "$root/api-cost-input-v1/worker.sh" > "$control/worker.log" 2>&1 </dev/null &
printf '%s\n' "$!" > "$control/pid"
printf 'API_COST_DIAGNOSTIC_STARTED pid='
cat "$control/pid"
