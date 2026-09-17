#!/usr/bin/env bash
set -euo pipefail
cd /home/ubuntu/projects/ARCH-main-merge-20260917
test "$(sha256sum source-v2.tar | cut -d ' ' -f 1)" = 24dcfae52cf22c0d1d5f78a82069cdf815b32ebdb4a284701f5f9a6344e52590
test ! -e source
mkdir source logs
tar -xf source-v2.tar -C source
cd source
set +e
/usr/bin/time -v /usr/bin/g++-11 -std=c++20 -O1 -fno-fast-math -ffp-contract=off -DARCH_CUDA_BUILD_ENABLED=0 -I src tests/host/test_predictive_amr_recorder.cpp -o ../recorder-contract > ../logs/recorder-build.stdout 2> ../logs/recorder-build.stderr
status=$?
printf '%s\n' "$status" > ../logs/recorder-build.exit
if [ "$status" != 0 ]; then exit "$status"; fi
../recorder-contract ../recorder-data > ../logs/recorder-run.stdout 2> ../logs/recorder-run.stderr
status=$?
printf '%s\n' "$status" > ../logs/recorder-run.exit
exit "$status"
