#!/usr/bin/env bash
set -euo pipefail
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
control="$root/window-control-v1"
# This is never invoked automatically when the preceding diagnostic finishes.
# Its result must first be inspected and the independent next experiment chosen.
test -s "$root/leaf-inline-control-v1/exit-code"
test -s "$root/leaf-inline-collection-v1.json"
test -s "$root/leaf-inline-local-receipt-v1.json"
test "$(cat "$root/batch-launch-control-v1/exit-code")" = 0
test -s "$root/batch-launch-local-receipt-v1.json"
test ! -e "$control"
test ! -e "$root/window-contract-v1"
bash -n "$root/window-input-v1/worker.sh"
mkdir "$control"
nohup bash "$root/window-input-v1/worker.sh" > "$control/worker.log" 2>&1 </dev/null &
printf '%s\n' "$!" > "$control/pid"
printf 'WINDOW_CONTRACT_STARTED pid='
cat "$control/pid"
