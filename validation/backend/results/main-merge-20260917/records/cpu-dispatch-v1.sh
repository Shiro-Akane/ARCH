#!/usr/bin/env bash
set -euo pipefail
cd /home/ubuntu/projects/ARCH-main-merge-20260917
test ! -e logs/cpu-build-v1.guard
set +e
python3 source/tools/run_memory_guarded.py --min-available-mib 16384 \
    --max-swap-growth-mib 256 --pressure-guard --log logs/cpu-build-v1.child \
    -- bash -x cpu-build-v1.sh > logs/cpu-build-v1.guard 2>&1
status=$?
printf '%s\n' "$status" > logs/cpu-build-v1.exit
exit "$status"
