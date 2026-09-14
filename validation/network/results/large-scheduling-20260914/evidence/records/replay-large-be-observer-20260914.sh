#!/usr/bin/env bash
# Original failed BE long/capacity input, with a test-only API observer.
# Run after the full fixed-science GPU sequence. This is NOT formal timing;
# the original 1800-second per-run timeout and all physics stay unchanged.
set -euo pipefail
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
export OMP_NUM_THREADS=8 OMP_DYNAMIC=FALSE OMP_PLACES=cores OMP_PROC_BIND=close
export PYTHONDONTWRITEBYTECODE=1
root=/home/ubuntu/projects/ARCH-microphysics-20260914
old=/home/ubuntu/projects/ARCH-perf-20260909/build/large-network-20260909/release
out="$root/build/p12-20260914/factor-cache"
python="$root/build/p12-20260914/validation-python/bin/python"
provider="$out/candidate-v2/libarch_cuda_sparse_provider.a"
observer="$root/build/p12-20260914/native-progress-observer-v1.so"
phase=long-be-observer-v1
cd "$root"
test ! -e "$out/$phase"
test "$(sha256sum "$provider" | cut -d ' ' -f 1)" = 24bb9be952ef34c68cd4ea65de498f0cd0de8bb70dc5419d6ef9d33ecf6b4fcd
sha256sum -c build/p12-20260914/native-progress-observer-v1.sha256
"$python" -c 'import json; r=json.load(open("/home/ubuntu/projects/ARCH-multiphysics-fix-20260914/build/fix-20260914/coupled-scale-v1/evidence.json")); assert r["status"]=="passed" and r["identity_verified"] and len(r["comparisons"])==18'
timeout --signal=INT --kill-after=30s 2h "$python" tools/run_memory_guarded.py \
  --min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard \
  --gpu-memory-device 0 --log "$out/$phase-child.log" -- \
  "$python" validation/network/run_sparse_capacity.py --build-dir "$old" \
  --source "$root/tests/cuda/test_generated_sparse_burn.cpp" --provider "$provider" \
  --output-dir "$out/$phase" --steps 16 --duration 1e-9 \
  --methods be_nr --pools 8 --storage 32 33 --preload "$observer" \
  > "$out/$phase-guard.log" 2>&1
sha256sum -c build/p12-20260914/native-progress-observer-v1.sha256
printf 'LARGE_BE_OBSERVED_TRAJECTORY_PASS_NOT_FORMAL_TIMING\n'
