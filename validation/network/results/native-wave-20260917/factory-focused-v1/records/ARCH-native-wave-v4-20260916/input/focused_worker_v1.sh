#!/usr/bin/env bash
# Run only AFTER reviewing the successful fresh factory build and identities.
set -euo pipefail
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
source="$root/source"
build="$root/factory-release"
control="$root/focused-control-v1"
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
test "$(cat "$root/factory-control-v2/exit-code")" = 0
test ! -e "$root/focused-v1"
for comm in ARCH nvcc ptxas cc1plus; do
  if pgrep -x "$comm" >/dev/null; then echo 'Other build/application active' >&2; exit 2; fi
done
test -z "$(nvidia-smi --query-compute-apps=pid --format=csv,noheader)"
date -u > "$control/started-utc.txt"
uname -a > "$control/kernel.txt"
lscpu > "$control/cpu.txt"
nvidia-smi -q > "$control/gpu.txt"
/usr/bin/g++-11 --version > "$control/host-compiler.txt"
/home/ubuntu/projects/.envs/arch/bin/nvcc --version > "$control/cuda-compiler.txt"
cd "$source"
sha256sum -c "$root/factory-control-v2/source-files.sha256" > "$control/source-check-before.log"
sha256sum -c "$root/factory-control-v2/network-files.sha256" > "$control/network-check-before.log"
sha256sum -c "$root/factory-control-v2/vendor.sha256" > "$control/vendor-check-before.log"
sha256sum -c "$root/factory-control-v2/artifacts.sha256" > "$control/product-check-before.log"
mkdir "$root/focused-v1"
for network in audit150 audit200; do
  timeout --signal=INT --kill-after=30s 2400s "$python" tools/run_memory_guarded.py \
    --min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard --gpu-memory-device 0 \
    --log "$control/$network-child.log" -- \
    "$python" validation/network/run_sparse_validation.py \
    --build-dir "$build" --output-dir "$root/focused-v1/$network" --network-id "$network" \
    --rho 1e7 --temperature 3e9 --interval 1e-10 --cv 1e8 --rtol 1e-7 \
    --steps 4 --storage-cells 2 3 --pool-cells 2 --composition c12=0.5 o16=0.5 --timeout 1800 \
    > "$control/$network-guard.log" 2>&1
  printf 'NATIVE_FOCUSED_ALL_THREE_ODE_PASS %s\n' "$network"
done
sha256sum -c "$root/factory-control-v2/source-files.sha256" > "$control/source-check-after.log"
sha256sum -c "$root/factory-control-v2/network-files.sha256" > "$control/network-check-after.log"
sha256sum -c "$root/factory-control-v2/vendor.sha256" > "$control/vendor-check-after.log"
sha256sum -c "$root/factory-control-v2/artifacts.sha256" > "$control/product-check-after.log"
printf 'NATIVE_FOCUSED_MATRIX_PASS_NOT_HELM_OR_PERFORMANCE_QUALIFIED\n'
