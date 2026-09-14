#!/usr/bin/env bash
# Same physical fixes as the candidate, deliberately without S5 batching.
# The uncorrected 128-block coupled baseline is invalid, not a speed sample.
set -euo pipefail
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
original=/home/ubuntu/projects/ARCH-microphysics-20260914
root=/home/ubuntu/projects/ARCH-corrected-fused-baseline-20260914
python="$original/build/p12-20260914/validation-python/bin/python"
cd "$original"
test ! -e "$root"
git worktree add --detach "$root" 32cc416220a69cad5ab12030ba7b26db360bcd22
cd "$root"
cp "$original/EOS_toolkit/tables/helmholtz/helm_table.dat" EOS_toolkit/tables/helmholtz/helm_table.dat
tar -xf "$original/build/p12-20260914/multiphysics-closure-muscl-overlay-v3.tar"
out=build/corrected-fused-20260914
mkdir -p "$out"
git rev-parse HEAD > "$out/source-head.txt"
git diff -- src tests > "$out/source.patch"
find CMakeLists.txt src cmake tests EOS_toolkit/tables/helmholtz/helm_table.dat -type f -print0 | sort -z | xargs -0 sha256sum > "$out/source-files.sha256"
cmake -S . -B "$out/release" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DARCH_ENABLE_CUDA=ON -DARCH_ENABLE_KLU=OFF -DARCH_ENABLE_CUDSS=OFF \
  -DARCH_ENABLE_OPENMP=ON -DARCH_FETCH_SUITESPARSE=OFF \
  -DCMAKE_C_COMPILER=/usr/bin/gcc-11 -DCMAKE_CXX_COMPILER=/usr/bin/g++-11 \
  -DCMAKE_CUDA_COMPILER=/home/ubuntu/projects/.envs/arch/bin/nvcc \
  -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/g++-11 -DCMAKE_CUDA_ARCHITECTURES=90 \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DFETCHCONTENT_SOURCE_DIR_HIGHFIVE=/home/ubuntu/projects/ARCH-cuda-v2-mainline-build/task-buildopt-20260901T/build-sm90/_deps/highfive-src \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$root/$out/release/bin" -DARCH_CUDA_HEAVY_COMPILE_JOBS=4 \
  -DCCACHE_PROGRAM=CCACHE_PROGRAM-NOTFOUND \
  '-DCMAKE_CXX_COMPILER_LAUNCHER=/usr/bin/time;-f;ARCH_COMPILE_METRIC elapsed_seconds=%e peak_rss_kib=%M exit_code=%x command=%C' \
  '-DCMAKE_CUDA_COMPILER_LAUNCHER=/usr/bin/time;-f;ARCH_COMPILE_METRIC elapsed_seconds=%e peak_rss_kib=%M exit_code=%x command=%C' \
  > "$out/configure.log" 2>&1
timeout --signal=INT --kill-after=30s 6h "$python" "$original/tools/run_memory_guarded.py" \
  --min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard \
  --log "$out/build-child.log" -- cmake --build "$out/release" \
  --target arch_cuda_backend ARCH arch_cuda_single_level_validation --parallel 4 --verbose \
  > "$out/build-guard.log" 2>&1
sha256sum "$out/release/bin/ARCH" "$out/release/libarch_cuda_backend.a" \
  "$out/release/arch_cuda_single_level_validation" > "$out/artifacts.sha256"
sha256sum -c "$out/source-files.sha256" > "$out/source-identity-after.log"
printf 'CORRECTED_FUSED_BASELINE_BUILD_PASS\n'
