#!/usr/bin/env bash
# Fresh factory/ODE build. Never reuse old device factory objects as evidence.
set -euo pipefail
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
original=/home/ubuntu/projects/ARCH-microphysics-20260914
canonical=/home/ubuntu/projects/ARCH-large-application-20260914
source="$root/source"
build="$root/factory-release"
control="$root/factory-control-v2"
python="$original/build/p12-20260914/validation-python/bin/python"
old=/home/ubuntu/projects/ARCH-perf-20260909/build/large-network-20260909/release
networks=/home/ubuntu/projects/ARCH-large-networks-20260909
cudss=/home/ubuntu/projects/ARCH-perf-20260909/build/network-python-20260909/lib/python3.11/site-packages/nvidia/cu12
cublas=/home/ubuntu/projects/ARCH-perf-20260909/build/network-python-20260909/lib/python3.11/site-packages/nvidia/cublas/lib
highfive=/home/ubuntu/projects/ARCH-cuda-v2-mainline-build/task-buildopt-20260901T/build-sm90/_deps/highfive-src
test -d "$control"
test ! -e "$control/exit-code"
exec 9>"$control/lock"
flock -n 9
finish() { result=$?; printf '%s\n' "$result" > "$control/exit-code"; exit "$result"; }
trap finish EXIT
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
export OMP_NUM_THREADS=8 OMP_DYNAMIC=FALSE OMP_PLACES=cores OMP_PROC_BIND=close
export PYTHONDONTWRITEBYTECODE=1 CUDA_VISIBLE_DEVICES=0 GIT_LFS_SKIP_SMUDGE=1
test -z "${LD_PRELOAD:-}"
test "$(cat "$root/control/exit-code")" = 0
test -s "$root/standalone-collection.json"
test "$(cat "$root/factory-control/exit-code")" = 1
test "$(git -C "$source" rev-parse HEAD)" = 8da9b23d5597c2c8bd91d9dfa07b971d2bb91bef
git -C "$source" diff --quiet HEAD -- src cmake tests
test ! -e "$build"
test -s "$root/factory-overlay/integration-record.json"
for comm in ARCH nvcc ptxas cc1plus; do
  if pgrep -x "$comm" >/dev/null; then echo 'Other build/application active' >&2; exit 2; fi
done
test -z "$(nvidia-smi --query-compute-apps=pid --format=csv,noheader)"
# Only two test factory TUs + provider/dependencies here, not a full ARCH build.
test "$(df -Pk "$root" | awk 'NR==2 {print $4}')" -ge 4194304
"$python" "$root/input/install_factory_overlay_v2.py" "$root" > "$control/install.log" 2>&1
cd "$source"
table=EOS_toolkit/tables/helmholtz/helm_table.dat
expected_table=$(git show "HEAD:$table" | sed -n 's/^oid sha256://p' | tr -d '\r')
[[ "$expected_table" =~ ^[0-9a-f]{64}$ ]]
printf '%s  %s\n' "$expected_table" "$canonical/$table" | sha256sum --check --status -
cp "$canonical/$table" "$table"
printf '%s  %s\n' "$expected_table" "$table" | sha256sum --check --status -
git rev-parse HEAD > "$control/source-head.txt"
git status --short > "$control/source-status.txt"
git diff --binary -- src cmake tests > "$control/source-tracked.patch"
find CMakeLists.txt src cmake tests "$table" -type f -print0 | sort -z | xargs -0 sha256sum > "$control/source-files.sha256"
find "$networks/audit150" "$networks/audit200" -type f -print0 | sort -z | xargs -0 sha256sum > "$control/network-files.sha256"
sha256sum "$cudss/lib/libcudss.so.0" "$cublas/libcublas.so.12" "$cublas/libcublasLt.so.12" > "$control/vendor.sha256"
cmake -S . -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_C_COMPILER=/usr/bin/gcc-11 -DCMAKE_CXX_COMPILER=/usr/bin/g++-11 \
  -DCMAKE_CUDA_COMPILER=/home/ubuntu/projects/.envs/arch/bin/nvcc \
  -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/g++-11 -DCMAKE_CUDA_ARCHITECTURES=90 \
  -DARCH_ENABLE_CUDA=ON -DARCH_ENABLE_KLU=ON -DARCH_ENABLE_CUDSS=ON \
  -DARCH_ENABLE_OPENMP=ON -DARCH_CUDSS_IR_STEPS=2 \
  -DARCH_FETCH_SUITESPARSE=ON -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DFETCHCONTENT_SOURCE_DIR_SUITESPARSE="$old/_deps/suitesparse-src" \
  -DFETCHCONTENT_SOURCE_DIR_HIGHFIVE="$highfive" \
  -DARCH_CUSTOM_NETWORK_ROOT="$networks" '-DARCH_CUSTOM_NETWORKS=audit150;audit200' \
  -DCUDSS_ROOT="$cudss" -DCuDSS_INCLUDE_DIR="$cudss/include" -DCuDSS_LIBRARY="$cudss/lib/libcudss.so.0" \
  -DCUDA_cublas_LIBRARY="$cublas/libcublas.so.12" -DCUDA_cublasLt_LIBRARY="$cublas/libcublasLt.so.12" \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$build/bin" \
  -DARCH_CUDA_HEAVY_COMPILE_JOBS=1 -DCCACHE_PROGRAM=CCACHE_PROGRAM-NOTFOUND \
  '-DCMAKE_CXX_COMPILER_LAUNCHER=/usr/bin/time;-f;ARCH_COMPILE_METRIC elapsed_seconds=%e peak_rss_kib=%M exit_code=%x command=%C' \
  '-DCMAKE_CUDA_COMPILER_LAUNCHER=/usr/bin/time;-f;ARCH_COMPILE_METRIC elapsed_seconds=%e peak_rss_kib=%M exit_code=%x command=%C' \
  > "$control/configure.log" 2>&1
timeout --signal=INT --kill-after=30s 12h "$python" tools/run_memory_guarded.py \
  --min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard --gpu-memory-device 0 \
  --log "$control/build-child.log" -- \
  cmake --build "$build" --target arch_cuda_generated_sparse_burn_audit150 arch_cuda_generated_sparse_burn_audit200 --parallel 1 --verbose \
  > "$control/build-guard.log" 2>&1
sha256sum -c "$control/source-files.sha256" > "$control/source-identity-check.log"
sha256sum -c "$control/network-files.sha256" > "$control/network-identity-check.log"
sha256sum -c "$control/vendor.sha256" > "$control/vendor-identity-check.log"
sha256sum "$build/arch_cuda_generated_sparse_burn_audit150" "$build/arch_cuda_generated_sparse_burn_audit200" \
  "$build/libarch_cuda_sparse_provider.a" > "$control/artifacts.sha256"
printf 'NATIVE_FRESH_FACTORIES_BUILD_PASS_NOT_TRAJECTORY_PASS\n'
# No automatic physics/performance job: inspect the new build and identities first.
