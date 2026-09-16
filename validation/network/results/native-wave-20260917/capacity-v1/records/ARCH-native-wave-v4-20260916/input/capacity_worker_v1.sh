#!/usr/bin/env bash
# Original 4-step capacity matrix, only after the small focused gate and backup.
set -euo pipefail
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
source="$root/source"
build="$root/factory-release"
control="$root/capacity-control-v1"
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
test -s "$root/factory-focused-collection-v1.json"
test -s "$root/factory-focused-local-receipt-v1.json"
test ! -e "$root/capacity-v1"
for comm in ARCH nvcc ptxas cc1plus; do
  if pgrep -x "$comm" >/dev/null; then echo 'Other build/application active' >&2; exit 2; fi
done
test -z "$(nvidia-smi --query-compute-apps=pid --format=csv,noheader)"
cd "$source"
sha256sum -c "$root/factory-control-v2/source-files.sha256" > "$control/source-check-before.log"
sha256sum -c "$root/factory-control-v2/network-files.sha256" > "$control/network-check-before.log"
sha256sum -c "$root/factory-control-v2/vendor.sha256" > "$control/vendor-check-before.log"
sha256sum -c "$root/factory-control-v2/artifacts.sha256" > "$control/product-check-before.log"
date -u > "$control/started-utc.txt"
printf '%s  %s\n' ac8c012e71754548f19cdcd6929eb07fba0dc5959344a8801d8be87f039386e7 \
  "$root/input/run_sparse_capacity_v1.py" | sha256sum -c - > "$control/recipe-pinned-check.log"
sha256sum "$root/input/run_sparse_capacity_v1.py" > "$control/recipe.sha256"
timeout --signal=INT --kill-after=30s 12h "$python" tools/run_memory_guarded.py \
  --min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard --gpu-memory-device 0 \
  --log "$control/capacity-child.log" -- \
  "$python" "$root/input/run_sparse_capacity_v1.py" \
  --build-dir "$build" --source "$source/tests/cuda/test_generated_sparse_burn.cpp" \
  --provider "$build/libarch_cuda_sparse_provider.a" --output-dir "$root/capacity-v1" \
  --methods be_nr bd ros4 --pools 8 32 --storage 32 33 --steps 4 --duration 1e-10 --run-timeout 1800 \
  > "$control/capacity-guard.log" 2>&1
sha256sum -c "$root/factory-control-v2/source-files.sha256" > "$control/source-check-after.log"
sha256sum -c "$root/factory-control-v2/network-files.sha256" > "$control/network-check-after.log"
sha256sum -c "$root/factory-control-v2/vendor.sha256" > "$control/vendor-check-after.log"
sha256sum -c "$root/factory-control-v2/artifacts.sha256" > "$control/product-check-after.log"
sha256sum -c "$control/recipe.sha256" > "$control/recipe-check-after.log"
printf 'NATIVE_CAPACITY_4STEP_MATRIX_PASS_NOT_LONG_HELM_OR_PERFORMANCE_QUALIFIED\n'
