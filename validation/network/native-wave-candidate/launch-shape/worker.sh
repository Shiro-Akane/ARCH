#!/usr/bin/env bash
set -euo pipefail
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
control="$root/advance-shape-control-v1"
python=/home/ubuntu/projects/ARCH-microphysics-20260914/build/p12-20260914/validation-python/bin/python
test -d "$control"
test ! -e "$control/exit-code"
exec 9>"$control/lock"
flock -n 9
finish() { result=$?; printf '%s\n' "$result" > "$control/exit-code"; exit "$result"; }
trap finish EXIT
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
export PYTHONDONTWRITEBYTECODE=1 OMP_NUM_THREADS=8 OMP_DYNAMIC=FALSE OMP_PLACES=cores OMP_PROC_BIND=close CUDA_VISIBLE_DEVICES=0
test -z "${LD_PRELOAD:-}"
test -z "${ARCH_SPARSE_ADVANCE_THREADS:-}"
cd "$root/source"
for name in source-files network-files vendor artifacts; do
  sha256sum -c "$root/factory-control-v2/$name.sha256" > "$control/$name-check-before.log"
done
sha256sum "$root/advance-shape-input-v1/"* > "$control/recipes.sha256"
date -u > "$control/started-utc.txt"
set +e
timeout --signal=INT --kill-after=30s 3h "$python" tools/run_memory_guarded.py \
  --min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard --gpu-memory-device 0 \
  --log "$control/diagnostic-child.log" -- \
  "$python" "$root/advance-shape-input-v1/run_diagnostic.py" \
  > "$control/diagnostic-guard.log" 2>&1
runtime=$?
set -e
printf '%s\n' "$runtime" > "$control/runtime-exit-code"
for name in source-files network-files vendor artifacts; do
  sha256sum -c "$root/factory-control-v2/$name.sha256" > "$control/$name-check-after.log"
done
sha256sum -c "$control/recipes.sha256" > "$control/recipes-check-after.log"
exit "$runtime"
