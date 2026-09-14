#!/usr/bin/env bash
# Clean, separate leaf build. Do not alter the application build being tested.
set -euo pipefail
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
original=/home/ubuntu/projects/ARCH-microphysics-20260914
root=/home/ubuntu/projects/ARCH-multiphysics-fix-20260914
python="$original/build/p12-20260914/validation-python/bin/python"
cd "$root"
out=build/fix-leaves-v3-20260914
test ! -e "$out"
mkdir -p "$out"
git diff -- src tests > "$out/closure-source.patch"
find CMakeLists.txt src cmake tests EOS_toolkit/tables/helmholtz/helm_table.dat -type f -print0 | sort -z | xargs -0 sha256sum > "$out/closure-source-files.sha256"
cmake -S . -B "$out/release" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DARCH_ENABLE_CUDA=ON -DARCH_ENABLE_KLU=OFF -DARCH_ENABLE_CUDSS=OFF \
  -DARCH_ENABLE_OPENMP=ON -DARCH_FETCH_SUITESPARSE=OFF \
  -DCMAKE_C_COMPILER=/usr/bin/gcc-11 -DCMAKE_CXX_COMPILER=/usr/bin/g++-11 \
  -DCMAKE_CUDA_COMPILER=/home/ubuntu/projects/.envs/arch/bin/nvcc \
  -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/g++-11 -DCMAKE_CUDA_ARCHITECTURES=90 \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DFETCHCONTENT_SOURCE_DIR_HIGHFIVE=/home/ubuntu/projects/ARCH-cuda-v2-mainline-build/task-buildopt-20260901T/build-sm90/_deps/highfive-src \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$root/$out/release/bin" -DARCH_CUDA_HEAVY_COMPILE_JOBS=4 \
  -DCCACHE_PROGRAM=CCACHE_PROGRAM-NOTFOUND > "$out/configure.log" 2>&1
"$python" "$original/tools/run_memory_guarded.py" --min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard \
  --log "$out/closure-leaves-build-child.log" -- cmake --build "$out/release" \
  --target arch_amr_operation_plans arch_cuda_amr_composition --parallel 2 --verbose \
  > "$out/closure-leaves-build-guard.log" 2>&1
sha256sum -c "$out/closure-source-files.sha256" > "$out/closure-leaves-identity-after.log"
sha256sum "$out/release/arch_amr_operation_plans" "$out/release/arch_cuda_amr_composition" > "$out/artifacts.sha256"
printf 'MULTIPHYSICS_V3_LEAVES_BUILD_PASS_GPU_TEST_NOT_YET_RUN\n'
