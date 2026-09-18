#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
control="$root/window-capacity-control-v1"
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
test -z "${ARCH_NATIVE_WAVE_WORK_OBSERVER:-}"
test -z "${ARCH_NATIVE_WINDOW_CELLS:-}"
cd "$root/source"
for name in source-files network-files vendor artifacts; do
  sha256sum -c "$root/factory-control-v2/$name.sha256" > "$control/$name-check-before.log"
done
sha256sum "$root/window-capacity-input-v1/"* > "$control/recipes.sha256"
date -u > "$control/started-utc.txt"
set +e
# Twelve original per-harness 7200 s limits plus bounded guard/cleanup margin.
timeout --signal=INT --kill-after=30s 26h "$python" tools/run_memory_guarded.py \
  --min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard --gpu-memory-device 0 \
  --log "$control/window-capacity-child.log" -- \
  "$python" "$root/window-capacity-input-v1/run_capacity.py" \
  > "$control/window-capacity-guard.log" 2>&1
runtime=$?
set -e
printf '%s\n' "$runtime" > "$control/runtime-exit-code"
for name in source-files network-files vendor artifacts; do
  sha256sum -c "$root/factory-control-v2/$name.sha256" > "$control/$name-check-after.log"
done
sha256sum -c "$control/recipes.sha256" > "$control/recipes-check-after.log"
exit "$runtime"
