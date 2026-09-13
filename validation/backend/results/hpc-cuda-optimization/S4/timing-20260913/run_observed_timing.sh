#!/usr/bin/env bash
# Run one unchanged S4 timing phase and retain read-only machine observations.
# No profiler, rebuild, affinity changes, or unrelated process control.
set -euo pipefail
source_root=/home/ubuntu/projects/ARCH-hpc-s4-validation-20260913
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
phase=${1:?pilot or timing}
threads=${2:?thread count}
case "$phase" in pilot|timing) ;; *) exit 2 ;; esac
case "$threads" in 1|2|4|8|16) ;; *) exit 2 ;; esac
cd "$source_root"
output="$source_root/build/s4-timing-20260913/observed-${phase}-threads${threads}"
test ! -e "$output"
mkdir -p "$output"
capture_environment() {
    date -u --iso-8601=seconds
    hostname
    git rev-parse HEAD
    git status --short
    uname -a
    lscpu
    grep -E 'Cpus_allowed_list|Mems_allowed_list' /proc/self/status
    nvidia-smi -q
    /usr/bin/g++-11 --version
    nvcc --version
    cmake --version
    ldd build/s4-validation-20260913/release/bin/ARCH
    free -m
    df -h .
    vmstat 1 4
    ps -eo pid,ppid,etimes,pcpu,rss,comm --sort=-pcpu | head -n 24
}
capture_environment > "$output/environment-before.log" 2>&1
python3 -B build/s4-validation-20260913/verify_s4.py \
    --source-root "$source_root" \
    --build-dir "$source_root/build/s4-validation-20260913/release" \
    --output-root "$output/phase" --phase "$phase" --threads "$threads" \
    > "$output/phase.log" 2>&1 &
runner=$!
printf 'TIMING_RUNNER_PID=%s OUTPUT=%s\n' "$runner" "$output"
while kill -0 "$runner" 2>/dev/null; do
    {
        date -u --iso-8601=seconds
        ps -eo pid,ppid,etimes,pcpu,rss,comm --sort=-pcpu | head -n 24
        nvidia-smi --query-gpu=utilization.gpu,memory.used,memory.total --format=csv
        vmstat 1 2
    } >> "$output/resource-observations.log" 2>&1
    sleep 9
done
result=0
wait "$runner" || result=$?
printf '%s\n' "$result" > "$output/exit-code.txt"
capture_environment > "$output/environment-after.log" 2>&1
cat "$output/phase.log"
exit "$result"
