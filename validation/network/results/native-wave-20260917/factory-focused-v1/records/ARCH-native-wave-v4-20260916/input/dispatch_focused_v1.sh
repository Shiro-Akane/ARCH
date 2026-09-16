#!/usr/bin/env bash
# One-shot dispatch only after successful factory evidence is inspected.
set -euo pipefail
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
control="$root/focused-control-v1"
test "$(cat "$root/factory-control-v2/exit-code")" = 0
test ! -e "$control"
test ! -e "$root/focused-v1"
bash -n "$root/input/focused_worker_v1.sh"
mkdir "$control"
nohup bash "$root/input/focused_worker_v1.sh" > "$control/worker.log" 2>&1 </dev/null &
printf '%s\n' "$!" > "$control/pid"
printf 'NATIVE_FOCUSED_WORKER_STARTED '
cat "$control/pid"
