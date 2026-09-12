#!/usr/bin/env bash
# Independent pristine worktree: no overlap with test-only capacity patches.
set -euo pipefail
baseline=/home/ubuntu/projects/ARCH-perf-20260909
source=/home/ubuntu/projects/ARCH-large-integration-20260909
commit=0266d96f20b184d4b17ebc6a066ac3b9021f1642
if [[ ! -e "$source" ]]; then
  git -C "$baseline" worktree add --detach "$source" "$commit"
fi
[[ "$(git -C "$source" rev-parse HEAD)" == "$commit" ]]
[[ -z "$(git -C "$source" status --porcelain)" ]]
cd "$source"
toolchain=/home/ubuntu/projects/.envs/arch
export PATH="$toolchain/bin:$PATH"
python="$baseline/build/network-python-20260909/bin/python"
output="$PWD/build/large-integration-20260909"
build="$output/release"
mkdir -p "$output"
cudss="$baseline/build/network-python-20260909/lib/python3.11/site-packages/nvidia/cu12"
cublas="$baseline/build/network-python-20260909/lib/python3.11/site-packages/nvidia/cublas"
highfive=/home/ubuntu/projects/ARCH-cuda-v2-mainline-build/task-buildopt-20260901T/build-sm90/_deps/highfive-src
suitesparse="$baseline/build/large-network-20260909/release/_deps/suitesparse-src"
launcher="$python;$baseline/build/performance-20260909/measure_compile.py;$output/compile-metrics"
cmake -S . -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DARCH_ENABLE_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=90 \
  -DCMAKE_C_COMPILER=/usr/bin/gcc-11 -DCMAKE_CXX_COMPILER=/usr/bin/g++-11 \
  -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/g++-11 \
  -DCMAKE_CUDA_COMPILER="$toolchain/bin/nvcc" \
  -DCMAKE_PREFIX_PATH="$toolchain" -DFETCHCONTENT_SOURCE_DIR_HIGHFIVE="$highfive" \
  -DFETCHCONTENT_SOURCE_DIR_SUITESPARSE="$suitesparse" \
  -DPython3_EXECUTABLE="$python" \
  -DARCH_ENABLE_KLU=ON -DARCH_FETCH_SUITESPARSE=ON \
  -DARCH_ENABLE_CUDSS=ON -DCUDSS_ROOT="$cudss" \
  -DCUDAToolkit_CUBLAS_INCLUDE_DIR="$cublas/include" \
  -DCUDA_cublas_LIBRARY="$cublas/lib/libcublas.so.12" \
  -DCUDA_cublasLt_LIBRARY="$cublas/lib/libcublasLt.so.12" \
  -DARCH_CUSTOM_NETWORK_ROOT=/home/ubuntu/projects/ARCH-large-networks-20260909 \
  '-DARCH_CUSTOM_NETWORKS=audit150;audit200' \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$build/bin" \
  -DARCH_CUDA_HEAVY_COMPILE_JOBS=4 \
  -DCMAKE_CUDA_COMPILER_LAUNCHER="$launcher" \
  -DCMAKE_CXX_COMPILER_LAUNCHER="$launcher" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON 2>&1 | tee "$output/configure.log"
"$python" tools/run_memory_guarded.py --min-available-mib 32768 \
  --max-swap-growth-mib 0 --pressure-guard --log "$output/build.log" -- \
  /usr/bin/time -v cmake --build "$build" --target ARCH --parallel 6
"$python" tools/summarize_cuda_compile_memory.py "$output/build.log" \
  --format commands --output "$output/compile-memory.csv"
[[ -z "$(git status --porcelain)" ]]
sha256sum "$build/bin/ARCH" "$build/libarch_cuda_backend.a" | tee "$output/artifact-sha256.txt"
