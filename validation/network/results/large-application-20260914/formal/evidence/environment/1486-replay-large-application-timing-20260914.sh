#!/usr/bin/env bash
# Only after the full original runtime matrix, all builds/diagnostics/archives
# and backup transfers have completed. One warmup and five alternating samples.
set -euo pipefail
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
export PYTHONDONTWRITEBYTECODE=1
export OMP_NUM_THREADS=8 OMP_DYNAMIC=FALSE OMP_PLACES=cores OMP_PROC_BIND=close
root=/home/ubuntu/projects/ARCH-large-application-20260914
base="$root/build/p12-20260914/large-application-v2"
python=/home/ubuntu/projects/ARCH-microphysics-20260914/build/p12-20260914/validation-python/bin/python
out="$base/timing-formal-v1"
cd "$root"
test ! -e "$out"
test -f "$base/runtime-v1/backend-validation-evidence.json"
sha256sum -c "$base/fixed-integration-v1/artifacts.sha256"
test "$(df -Pk "$root" | awk 'NR==2 {print $4}')" -ge 8388608
date --iso-8601=seconds > "$base/timing-host-before-v1.log"
ps -eo pid,pcpu,pmem,etimes,comm --sort=-pcpu >> "$base/timing-host-before-v1.log"
nvidia-smi >> "$base/timing-host-before-v1.log"
command=("$python" validation/network/run_large_application_timing.py
  --build-dir "$base/release" --validation-evidence "$base/runtime-v1/backend-validation-evidence.json"
  --output-root "$out" --threads 1 8 16 --gpu-threads 8 --warmups 1 --repeats 5 --timeout 3600)
printf '%q ' "${command[@]}" > "$base/timing-command-v1.txt"
printf '\n' >> "$base/timing-command-v1.txt"
timeout --signal=INT --kill-after=30s 24h "$python" tools/run_memory_guarded.py \
  --min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard --gpu-memory-device 0 \
  --log "$base/timing-child-v1.log" -- "${command[@]}" > "$base/timing-guard-v1.log" 2>&1
date --iso-8601=seconds > "$base/timing-host-after-v1.log"
ps -eo pid,pcpu,pmem,etimes,comm --sort=-pcpu >> "$base/timing-host-after-v1.log"
nvidia-smi >> "$base/timing-host-after-v1.log"
sha256sum -c "$base/fixed-integration-v1/artifacts.sha256"
printf 'LARGE_APPLICATION_FORMAL_TIMING_PASS\n'
