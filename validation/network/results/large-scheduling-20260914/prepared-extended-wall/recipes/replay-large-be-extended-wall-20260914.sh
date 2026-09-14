#!/usr/bin/env bash
# Original BE large-capacity trajectories with a larger WALL guard only.
# Run after all formal timing phases exit; no observer, no physics changes.
set -euo pipefail
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
export OMP_NUM_THREADS=8 OMP_DYNAMIC=FALSE OMP_PLACES=cores OMP_PROC_BIND=close
export PYTHONDONTWRITEBYTECODE=1
test -z "${LD_PRELOAD:-}"
if pgrep -x ARCH >/dev/null || pgrep -x nvcc >/dev/null || pgrep -x ptxas >/dev/null || pgrep -x cc1plus >/dev/null; then
  printf 'Other application/build work exists; wait, do not stop it.\n' >&2
  exit 2
fi
if pgrep -f '^/home/ubuntu/projects/.*/arch_cuda_generated_sparse_burn_' >/dev/null; then
  printf 'Another sparse harness is active; wait.\n' >&2
  exit 2
fi
test -z "$(nvidia-smi --query-compute-apps=pid --format=csv,noheader)"
root=/home/ubuntu/projects/ARCH-microphysics-20260914
old=/home/ubuntu/projects/ARCH-perf-20260909/build/large-network-20260909/release
out="$root/build/p12-20260914/factor-cache"
python="$root/build/p12-20260914/validation-python/bin/python"
recipes="$root/build/p12-20260914/be-extended-wall-recipes-v1"
provider="$out/candidate-v2/libarch_cuda_sparse_provider.a"
phase=long-be-extended-wall-v1
cd "$root"
test "$(sha256sum "$provider" | cut -d ' ' -f 1)" = 24bb9be952ef34c68cd4ea65de498f0cd0de8bb70dc5419d6ef9d33ecf6b4fcd
test ! -e "$out/$phase"
# Four harness invocations; each keeps BOTH original storage sizes (32 then 33).
# Default 1800-second build limits remain; only runtime commands get six hours.
timeout --signal=INT --kill-after=30s 26h "$python" tools/run_memory_guarded.py \
  --min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard \
  --gpu-memory-device 0 --log "$out/$phase-child.log" -- \
  "$python" "$recipes/validation/network/run_sparse_capacity.py" --build-dir "$old" \
  --source "$root/tests/cuda/test_generated_sparse_burn.cpp" --provider "$provider" \
  --output-dir "$out/$phase" --methods be_nr --pools 8 32 --storage 32 33 \
  --steps 16 --duration 1e-9 --run-timeout 21600 \
  > "$out/$phase-guard.log" 2>&1
printf 'LARGE_BE_EXTENDED_WALL_COMPLETE\n'
