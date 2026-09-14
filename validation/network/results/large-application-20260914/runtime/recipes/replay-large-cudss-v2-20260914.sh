#!/usr/bin/env bash
# Server-specific, auditable integration recipe. No dependency or physics edits.
set -euo pipefail
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
root=/home/ubuntu/projects/ARCH-large-application-20260914
out="$root/build/p12-20260914/large-application-v2"
build="$out/release"
networks=/home/ubuntu/projects/ARCH-large-networks-20260909
old=/home/ubuntu/projects/ARCH-perf-20260909/build/large-network-20260909/release
cudss=/home/ubuntu/projects/ARCH-perf-20260909/build/network-python-20260909/lib/python3.11/site-packages/nvidia/cu12
cublas=/home/ubuntu/projects/ARCH-perf-20260909/build/network-python-20260909/lib/python3.11/site-packages/nvidia/cublas/lib
highfive=/home/ubuntu/projects/ARCH-cuda-v2-mainline-build/task-buildopt-20260901T/build-sm90/_deps/highfive-src
python=/home/ubuntu/projects/ARCH-microphysics-20260914/build/p12-20260914/validation-python/bin/python
cd "$root"
required_commit=${1:?Pass the frozen full commit ID explicitly}
test "$(git rev-parse HEAD)" = "$required_commit"
git diff --quiet HEAD -- CMakeLists.txt src cmake
# A checkout may contain only the LFS pointer. Reuse the already-validated
# immutable table during setup, but require its committed data SHA here.
table=EOS_toolkit/tables/helmholtz/helm_table.dat
expected_table=$(git show "HEAD:$table" | sed -n 's/^oid sha256://p' | tr -d '\r')
[[ "$expected_table" =~ ^[0-9a-f]{64}$ ]]
printf '%s  %s\n' "$expected_table" "$table" | sha256sum --check --status -
test ! -e "$out"
available=$(df -B1 --output=avail . | tail -n 1 | tr -d ' ')
test "$available" -ge 8589934592
test -f "$networks/audit150/manifest.json"
test -f "$networks/audit200/manifest.json"
test -f "$cudss/lib/libcudss.so.0"
test -f "$cublas/libcublas.so.12"
test -f "$cublas/libcublasLt.so.12"
test -f "$old/_deps/suitesparse-src/CMakeLists.txt"
test -f "$highfive/CMakeLists.txt"
mkdir -p "$out"
git status --short > "$out/source-status.txt"
git diff --binary -- . ':(exclude)EOS_toolkit/tables/helmholtz/helm_table.dat' > "$out/source-tracked.patch"
git rev-parse HEAD > "$out/source-head.txt"
find CMakeLists.txt src cmake "$table" -type f -print0 | sort -z | xargs -0 sha256sum > "$out/source-files.sha256"
find "$networks/audit150" "$networks/audit200" -type f -print0 | sort -z | xargs -0 sha256sum > "$out/network-files.sha256"
sha256sum "$cudss/lib/libcudss.so.0" > "$out/cudss.sha256"
sha256sum "$cublas/libcublas.so.12" "$cublas/libcublasLt.so.12" > "$out/cublas.sha256"
cmake -S . -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_C_COMPILER=/usr/bin/gcc-11 \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++-11 \
  -DCMAKE_CUDA_COMPILER=/home/ubuntu/projects/.envs/arch/bin/nvcc \
  -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/g++-11 \
  -DCMAKE_CUDA_ARCHITECTURES=90 \
  -DARCH_ENABLE_CUDA=ON -DARCH_ENABLE_KLU=ON -DARCH_ENABLE_CUDSS=ON \
  -DARCH_ENABLE_OPENMP=ON -DARCH_CUDSS_IR_STEPS=2 \
  -DARCH_FETCH_SUITESPARSE=ON -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DFETCHCONTENT_SOURCE_DIR_SUITESPARSE="$old/_deps/suitesparse-src" \
  -DFETCHCONTENT_SOURCE_DIR_HIGHFIVE="$highfive" \
  -DARCH_CUSTOM_NETWORK_ROOT="$networks" '-DARCH_CUSTOM_NETWORKS=audit150;audit200' \
  -DCUDSS_ROOT="$cudss" -DCuDSS_INCLUDE_DIR="$cudss/include" \
  -DCuDSS_LIBRARY="$cudss/lib/libcudss.so.0" \
  -DCUDA_cublas_LIBRARY="$cublas/libcublas.so.12" \
  -DCUDA_cublasLt_LIBRARY="$cublas/libcublasLt.so.12" \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$build/bin" \
  -DARCH_CUDA_HEAVY_COMPILE_JOBS=4 -DCCACHE_PROGRAM=CCACHE_PROGRAM-NOTFOUND \
  '-DCMAKE_CXX_COMPILER_LAUNCHER=/usr/bin/time;-f;ARCH_COMPILE_METRIC elapsed_seconds=%e peak_rss_kib=%M exit_code=%x command=%C' \
  '-DCMAKE_CUDA_COMPILER_LAUNCHER=/usr/bin/time;-f;ARCH_COMPILE_METRIC elapsed_seconds=%e peak_rss_kib=%M exit_code=%x command=%C' \
  > "$out/configure.log" 2>&1
for target in arch_cuda_backend ARCH arch_cuda_single_level_validation; do
  timeout --signal=INT --kill-after=30s 24h "$python" tools/run_memory_guarded.py \
    --min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard \
    --log "$out/$target-child.log" -- \
    cmake --build "$build" --target "$target" --parallel 4 --verbose \
    > "$out/$target-guard.log" 2>&1
  printf 'LARGE_APPLICATION_BUILD_TARGET_PASS %s\n' "$target"
done
sha256sum -c "$out/source-files.sha256" > "$out/source-identity-check.log"
sha256sum -c "$out/network-files.sha256" > "$out/network-identity-check.log"
sha256sum -c "$out/cudss.sha256" > "$out/cudss-identity-check.log"
sha256sum -c "$out/cublas.sha256" > "$out/cublas-identity-check.log"
sha256sum "$build/bin/ARCH" "$build/libarch_cuda_backend.a" \
  "$build/libarch_cuda_sparse_provider.a" "$build/arch_cuda_single_level_validation" > "$out/artifacts.sha256"
printf 'LARGE_APPLICATION_BUILD_PASS\n'
