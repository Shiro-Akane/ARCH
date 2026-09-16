#!/usr/bin/env bash
set -euo pipefail
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
test ! -e "$root/factory-control-v2"
bash -n "$root/input/factory_worker_v2.sh"
mkdir "$root/factory-control-v2"
nohup bash "$root/input/factory_worker_v2.sh" > "$root/factory-control-v2/worker.log" 2>&1 </dev/null &
printf '%s\n' "$!" > "$root/factory-control-v2/pid"
printf 'FRESH_FACTORY_WORKER_STARTED '
cat "$root/factory-control-v2/pid"
