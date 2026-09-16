#!/usr/bin/env bash
set -euo pipefail
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
control="$root/capacity-control-v1"
test "$(cat "$root/focused-control-v1/exit-code")" = 0
test -s "$root/factory-focused-local-receipt-v1.json"
test ! -e "$control"
test ! -e "$root/capacity-v1"
bash -n "$root/input/capacity_worker_v1.sh"
mkdir "$control"
nohup bash "$root/input/capacity_worker_v1.sh" > "$control/worker.log" 2>&1 </dev/null &
printf '%s\n' "$!" > "$control/pid"
printf 'NATIVE_CAPACITY_WORKER_STARTED '
cat "$control/pid"
