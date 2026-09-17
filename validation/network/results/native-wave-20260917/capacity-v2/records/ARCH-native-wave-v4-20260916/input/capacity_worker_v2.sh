#!/usr/bin/env bash
# Same original physical capacity matrix; only wall allowance 1800 -> 7200.
set -euo pipefail
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
source="$root/source"
build="$root/factory-release"
control="$root/capacity-control-v2"
python=/home/ubuntu/projects/ARCH-microphysics-20260914/build/p12-20260914/validation-python/bin/python
test -d "$control"
test ! -e "$control/exit-code"
exec 9>"$control/lock"
flock -n 9
finish() { result=$?; printf '%s\n' "$result" > "$control/exit-code"; exit "$result"; }
trap finish EXIT
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
export OMP_NUM_THREADS=8 OMP_DYNAMIC=FALSE OMP_PLACES=cores OMP_PROC_BIND=close
export PYTHONDONTWRITEBYTECODE=1 CUDA_VISIBLE_DEVICES=0
test -z "${LD_PRELOAD:-}"
test -z "${ARCH_NATIVE_WAVE_WORK_OBSERVER:-}"
test "$(cat "$root/focused-control-v1/exit-code")" = 0
test "$(cat "$root/capacity-control-v1/exit-code")" = 1
test -s "$root/capacity-collection-v1.json"
test -s "$root/capacity-local-receipt-v1.json"
test ! -e "$root/capacity-v2"
for comm in ARCH nvcc ptxas cc1plus; do
  if pgrep -x "$comm" >/dev/null; then echo 'Other build/application active' >&2; exit 2; fi
done
test -z "$(nvidia-smi --query-compute-apps=pid --format=csv,noheader)"
cd "$source"
for name in source-files network-files vendor artifacts; do
  sha256sum -c "$root/factory-control-v2/$name.sha256" > "$control/$name-check-before.log"
done
date -u > "$control/started-utc.txt"
printf '%s  %s\n' ac8c012e71754548f19cdcd6929eb07fba0dc5959344a8801d8be87f039386e7 \
  "$root/input/run_sparse_capacity_v1.py" | sha256sum -c - > "$control/recipe-pinned-check.log"
sha256sum "$root/input/run_sparse_capacity_v1.py" > "$control/recipe.sha256"
set +e
timeout --signal=INT --kill-after=30s 12h "$python" tools/run_memory_guarded.py \
  --min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard --gpu-memory-device 0 \
  --log "$control/capacity-child.log" -- \
  "$python" "$root/input/run_sparse_capacity_v1.py" \
  --build-dir "$build" --source "$source/tests/cuda/test_generated_sparse_burn.cpp" \
  --provider "$build/libarch_cuda_sparse_provider.a" --output-dir "$root/capacity-v2" \
  --methods be_nr bd ros4 --pools 8 32 --storage 32 33 --steps 4 --duration 1e-10 --run-timeout 7200 \
  > "$control/capacity-guard.log" 2>&1
runtime=$?
set -e
printf '%s\n' "$runtime" > "$control/runtime-exit-code"
for name in source-files network-files vendor artifacts; do
  sha256sum -c "$root/factory-control-v2/$name.sha256" > "$control/$name-check-after.log"
done
sha256sum -c "$control/recipe.sha256" > "$control/recipe-check-after.log"
if test "$runtime" -ne 0; then exit "$runtime"; fi
printf 'NATIVE_CAPACITY_4STEP_MATRIX_PASS_NOT_LONG_HELM_OR_PERFORMANCE_QUALIFIED\n'
