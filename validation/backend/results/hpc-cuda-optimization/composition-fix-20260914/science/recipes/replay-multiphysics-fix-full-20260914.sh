#!/usr/bin/env bash
# Complete production build, all original S5 targets, actual dependency rebuild.
set -euo pipefail
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
root=/home/ubuntu/projects/ARCH-multiphysics-fix-20260914
original=/home/ubuntu/projects/ARCH-microphysics-20260914
s5=/home/ubuntu/projects/ARCH-s5-indicator-20260914
python="$original/build/p12-20260914/validation-python/bin/python"
cd "$root"
out=build/fix-20260914
test ! -e "$out/full-build-v1"
tar -xf "$original/build/p12-20260914/multiphysics-closure-muscl-overlay-v3.tar"
mkdir "$out/full-build-v1"
git diff -- src tests > "$out/full-build-v1/source.patch"
git rev-parse HEAD > "$out/full-build-v1/source-head.txt"
find CMakeLists.txt src cmake tests EOS_toolkit/tables/helmholtz/helm_table.dat -type f -print0 | sort -z | xargs -0 sha256sum > "$out/full-build-v1/source-files.sha256"
cp "$s5/build/s5-20260914/required-targets.txt" "$out/full-build-v1/required-targets.txt"
mapfile -t targets < "$out/full-build-v1/required-targets.txt"
cmake -S . -B "$out/release" \
  '-DCMAKE_CXX_COMPILER_LAUNCHER=/usr/bin/time;-f;ARCH_COMPILE_METRIC elapsed_seconds=%e peak_rss_kib=%M exit_code=%x command=%C' \
  '-DCMAKE_CUDA_COMPILER_LAUNCHER=/usr/bin/time;-f;ARCH_COMPILE_METRIC elapsed_seconds=%e peak_rss_kib=%M exit_code=%x command=%C' \
  > "$out/full-build-v1/configure.log" 2>&1
timeout --signal=INT --kill-after=30s 6h "$python" "$original/tools/run_memory_guarded.py" \
  --min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard \
  --log "$out/full-build-v1/build-child.log" -- cmake --build "$out/release" \
  --target "${targets[@]}" --parallel 4 --verbose > "$out/full-build-v1/build-guard.log" 2>&1
sha256sum "$out/release/bin/ARCH" "$out/release/libarch_cuda_backend.a" \
  "$out/release/arch_cuda_single_level_validation" > "$out/full-build-v1/artifacts.sha256"
sha256sum -c "$out/full-build-v1/source-files.sha256" > "$out/full-build-v1/source-identity-after.log"
printf 'MULTIPHYSICS_FIX_FULL_BUILD_PASS\n'
