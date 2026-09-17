#!/usr/bin/env bash
set -euo pipefail
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
control="$root/batch-launch-control-v1"
test "$(cat "$root/capacity-control-v2/exit-code")" = 0
test -s "$root/capacity-local-receipt-v2.json"
test ! -e "$control"
test ! -e "$root/batch-launch-contract-v1"
bash -n "$root/batch-launch-input-v1/worker.sh"
mkdir "$control"
nohup bash "$root/batch-launch-input-v1/worker.sh" > "$control/worker.log" 2>&1 </dev/null &
printf '%s\n' "$!" > "$control/pid"
printf 'BATCH_LAUNCH_CONTRACT_WORKER_STARTED '
cat "$control/pid"
