#!/usr/bin/env bash
# Additional diagnostics after the original large BE long/capacity timeout.
# Neither branch below replaces or passes that failed combined case.
# Run only when this task owns the otherwise-idle GPU; never during timings.
set -euo pipefail
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
export OMP_NUM_THREADS=8 OMP_DYNAMIC=FALSE OMP_PLACES=cores OMP_PROC_BIND=close
export PYTHONDONTWRITEBYTECODE=1
root=/home/ubuntu/projects/ARCH-microphysics-20260914
old=/home/ubuntu/projects/ARCH-perf-20260909/build/large-network-20260909/release
out="$root/build/p12-20260914/factor-cache"
python="$root/build/p12-20260914/validation-python/bin/python"
provider="$out/candidate-v2/libarch_cuda_sparse_provider.a"
phase=${1:?long-bd-ros-v1 or long-be-small-v1}
cd "$root"
test "$(sha256sum "$provider" | cut -d ' ' -f 1)" = 24bb9be952ef34c68cd4ea65de498f0cd0de8bb70dc5419d6ef9d33ecf6b4fcd
test ! -e "$out/$phase"
case "$phase" in
  long-bd-ros-v1) options=(--methods bd ros4 --pools 8 32 --storage 32 33);;
  long-be-small-v1) options=(--methods be_nr --pools 2 --storage 2 3);;
  *) exit 2;;
esac
timeout --signal=INT --kill-after=30s 12h "$python" tools/run_memory_guarded.py \
  --min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard \
  --gpu-memory-device 0 --log "$out/$phase-child.log" -- \
  "$python" validation/network/run_sparse_capacity.py --build-dir "$old" \
  --source "$root/tests/cuda/test_generated_sparse_burn.cpp" --provider "$provider" \
  --output-dir "$out/$phase" --steps 16 --duration 1e-9 "${options[@]}" \
  > "$out/$phase-guard.log" 2>&1
printf 'LARGE_FOLLOWUP_PASS %s\n' "$phase"
