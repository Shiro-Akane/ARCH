#!/usr/bin/env bash
set -euo pipefail
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
control="$root/window-factory-control-v1"
test "$(cat "$root/window-control-v1/exit-code")" = 0
test -s "$root/window-collection-v1.json"
test -s "$root/window-local-receipt-v1.json"
test ! -e "$control"
test ! -e "$root/window-factory-v1"
# Full contract and archive byte checks happen before building in the runner.
bash -n "$root/window-factory-input-v1/factory-worker.sh"
mkdir "$control"
nohup bash "$root/window-factory-input-v1/factory-worker.sh" > "$control/worker.log" 2>&1 </dev/null &
printf '%s\n' "$!" > "$control/pid"
printf 'WINDOW_FACTORY_STARTED pid='
cat "$control/pid"
