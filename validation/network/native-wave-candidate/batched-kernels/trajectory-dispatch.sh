#!/usr/bin/env bash
set -euo pipefail
profile=${1:?focused/capacity/long required}
case "$profile" in focused|capacity|long) ;; *) exit 2 ;; esac
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
control="$root/batch-launch-$profile-control-v1"
test "$(cat "$root/batch-launch-control-v1/exit-code")" = 0
test -s "$root/batch-launch-local-receipt-v1.json"
test ! -e "$control"
test ! -e "$root/batch-launch-$profile-v1"
bash -n "$root/batch-trajectory-input-v1/worker.sh"
mkdir "$control"
nohup bash "$root/batch-trajectory-input-v1/worker.sh" "$profile" > "$control/worker.log" 2>&1 </dev/null &
printf '%s\n' "$!" > "$control/pid"
printf 'BATCH_LAUNCH_NUCLEAR_PROFILE_STARTED profile=%s pid=' "$profile"
cat "$control/pid"
