#!/usr/bin/env bash
set -euo pipefail
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
control="$root/leaf-inline-control-v1"
test "$(cat "$root/api-cost-control-v1/exit-code")" = 0
test -s "$root/api-cost-local-receipt-v1.json"
test ! -e "$control"
test ! -e "$root/leaf-inline-v1"
bash -n "$root/leaf-inline-input-v1/worker.sh"
mkdir "$control"
nohup bash "$root/leaf-inline-input-v1/worker.sh" > "$control/worker.log" 2>&1 </dev/null &
printf '%s\n' "$!" > "$control/pid"
printf 'LEAF_INLINE_DIAGNOSTIC_STARTED pid='
cat "$control/pid"
