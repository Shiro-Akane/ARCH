#!/usr/bin/env bash
# One-shot isolated native-wave contract build. Launch only AFTER local BE backup.
set -euo pipefail
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
original=/home/ubuntu/projects/ARCH-microphysics-20260914
base="$original/build/p12-20260914"
control="$root/control"
input="$root/input"
python="$base/validation-python/bin/python"
old=/home/ubuntu/projects/ARCH-perf-20260909/build/large-network-20260909/release
helper="$base/factor-cache/candidate-v2/libarch_cuda_sparse_provider.a"
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
test "$(cat "$base/be-extended-wall-controller-v1/exit-code")" = 0
test -s "$base/factor-cache/long-be-extended-wall-v1-archive/compact/raw-archive.json"
test ! -e "$root/contracts"
if pgrep -x ARCH >/dev/null || pgrep -x nvcc >/dev/null || pgrep -x ptxas >/dev/null || pgrep -x cc1plus >/dev/null; then
  printf 'Other application/build work exists; no native test started.\n' >&2
  exit 2
fi
if pgrep -f '^/home/ubuntu/projects/.*/arch_cuda_generated_sparse_burn_' >/dev/null; then
  printf 'A sparse harness is active; no native test started.\n' >&2
  exit 2
fi
test -z "$(nvidia-smi --query-compute-apps=pid --format=csv,noheader)"
available_kib=$(df -Pk "$root" | awk 'NR==2 { print $4 }')
test "$available_kib" -ge 2097152
test "$(sha256sum "$helper" | cut -d ' ' -f 1)" = 24bb9be952ef34c68cd4ea65de498f0cd0de8bb70dc5419d6ef9d33ecf6b4fcd
cd "$original"
timeout --signal=INT --kill-after=30s 10800s "$python" tools/run_memory_guarded.py \
  --min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard \
  --gpu-memory-device 0 --log "$control/child.log" -- \
  "$python" "$input/run_standalone_contracts.py" --build-dir "$old" \
  --recorded-link "$input/recorded-link.json" --payload "$input/payload" \
  --shared-manifest "$input/shared-inputs.json" --helper-library "$helper" \
  --output-dir "$root/contracts" > "$control/guard.log" 2>&1
printf 'NATIVE_WAVE_STANDALONE_FINISHED_NOT_ODE_OR_RELEASE_QUALIFIED\n'
