#!/usr/bin/env bash
set -euo pipefail
cd /home/ubuntu/projects/ARCH-perf-20260909
toolchain=/home/ubuntu/projects/.envs/arch
export PATH="$toolchain/bin:$PATH"
python="$PWD/build/network-python-20260909/bin/python"
output="$PWD/build/large-network-20260909"
build="$output/release"
cudss="$PWD/build/network-python-20260909/lib/python3.11/site-packages/nvidia/cu12"
cublas="$PWD/build/network-python-20260909/lib/python3.11/site-packages/nvidia/cublas"
highfive=/home/ubuntu/projects/ARCH-cuda-v2-mainline-build/task-buildopt-20260901T/build-sm90/_deps/highfive-src
launcher="$python;$PWD/build/performance-20260909/measure_compile.py;$output/compile-metrics"
cmake -S . -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DARCH_ENABLE_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=90 \
  -DCMAKE_C_COMPILER=/usr/bin/gcc-11 -DCMAKE_CXX_COMPILER=/usr/bin/g++-11 \
  -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/g++-11 \
  -DCMAKE_CUDA_COMPILER="$toolchain/bin/nvcc" \
  -DCMAKE_PREFIX_PATH="$toolchain" -DFETCHCONTENT_SOURCE_DIR_HIGHFIVE="$highfive" \
  -DPython3_EXECUTABLE="$python" \
  -DARCH_ENABLE_KLU=ON -DARCH_FETCH_SUITESPARSE=ON \
  -DARCH_ENABLE_CUDSS=ON -DCUDSS_ROOT="$cudss" \
  -DCUDAToolkit_CUBLAS_INCLUDE_DIR="$cublas/include" \
  -DCUDA_cublas_LIBRARY="$cublas/lib/libcublas.so.12" \
  -DCUDA_cublasLt_LIBRARY="$cublas/lib/libcublasLt.so.12" \
  -DARCH_CUSTOM_NETWORK_ROOT=/home/ubuntu/projects/ARCH-large-networks-20260909 \
  '-DARCH_CUSTOM_NETWORKS=audit150;audit200' \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$build/bin" \
  -DARCH_CUDA_HEAVY_COMPILE_JOBS=2 \
  -DCMAKE_CUDA_COMPILER_LAUNCHER="$launcher" \
  -DCMAKE_CXX_COMPILER_LAUNCHER="$launcher" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON 2>&1 | tee "$output/configure.log"
