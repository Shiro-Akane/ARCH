#!/usr/bin/env bash
set -euo pipefail
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
test ! -e "$root/factory-control"
bash -n "$root/input/factory_worker.sh"
mkdir "$root/factory-control"
nohup bash "$root/input/factory_worker.sh" > "$root/factory-control/worker.log" 2>&1 </dev/null &
printf '%s\n' "$!" > "$root/factory-control/pid"
printf 'FRESH_FACTORY_WORKER_STARTED '
cat "$root/factory-control/pid"
