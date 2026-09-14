#!/usr/bin/env bash
# Original six-case matrix, not timing and not a substitute for focused gates.
# Run only after the complete large build and other GPU diagnostics have exited.
set -euo pipefail
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
export OMP_NUM_THREADS=8 OMP_DYNAMIC=FALSE OMP_PLACES=cores OMP_PROC_BIND=close
export PYTHONDONTWRITEBYTECODE=1
root=/home/ubuntu/projects/ARCH-large-application-20260914
out="$root/build/p12-20260914/large-application-v2"
build="$out/release"
python=/home/ubuntu/projects/ARCH-microphysics-20260914/build/p12-20260914/validation-python/bin/python
cd "$root"
test ! -e "$out/runtime-v1"
artifact_manifest=${1:-$out/artifacts.sha256}
sha256sum -c "$artifact_manifest"
printf '%s\n' "$artifact_manifest" > "$out/runtime-artifact-manifest-v1.txt"
ldd "$build/bin/ARCH" > "$out/runtime-loader-v1.log"
if grep -q 'not found' "$out/runtime-loader-v1.log"; then exit 1; fi
command=("$python" tools/validate_backend_results.py --manifest validation/network/large_runtime_cases.json
  --arch "$build/bin/ARCH" --checkpoint-validator "$build/arch_cuda_single_level_validation"
  --source-root "$root" --build-dir "$build" --output-root "$out/runtime-v1")
printf '%q ' "${command[@]}" > "$out/runtime-command-v1.txt"
printf '\n' >> "$out/runtime-command-v1.txt"
timeout --signal=INT --kill-after=30s 24h "$python" tools/run_memory_guarded.py \
  --min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard \
  --gpu-memory-device 0 --log "$out/runtime-child-v1.log" -- "${command[@]}" \
  > "$out/runtime-guard-v1.log" 2>&1
sha256sum -c "$artifact_manifest" > "$out/runtime-artifacts-after-v1.log"
printf 'LARGE_APPLICATION_RUNTIME_PASS\n'
