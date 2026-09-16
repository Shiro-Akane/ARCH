#!/usr/bin/env bash
set -euo pipefail
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
control="$root/capacity-control-v2"
test "$(cat "$root/capacity-control-v1/exit-code")" = 1
test -s "$root/capacity-local-receipt-v1.json"
test ! -e "$control"
test ! -e "$root/capacity-v2"
bash -n "$root/input/capacity_worker_v2.sh"
mkdir "$control"
nohup bash "$root/input/capacity_worker_v2.sh" > "$control/worker.log" 2>&1 </dev/null &
printf '%s\n' "$!" > "$control/pid"
printf 'NATIVE_CAPACITY_WALL_ONLY_V2_WORKER_STARTED '
cat "$control/pid"
