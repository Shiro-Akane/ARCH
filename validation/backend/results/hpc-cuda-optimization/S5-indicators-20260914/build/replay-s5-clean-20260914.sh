#!/usr/bin/env bash
# New real source/build root; the qualified fused root remains the baseline.
set -euo pipefail
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
root=/home/ubuntu/projects/ARCH-s5-indicator-20260914
out="$root/build/s5-20260914"
build="$out/release"
highfive=/home/ubuntu/projects/ARCH-cuda-v2-mainline-build/task-buildopt-20260901T/build-sm90/_deps/highfive-src
python=/home/ubuntu/projects/ARCH-microphysics-20260914/build/p12-20260914/validation-python/bin/python
cd "$root"
required_commit=${1:?Pass the frozen full base commit ID explicitly}
test "$(git rev-parse HEAD)" = "$required_commit"
table=EOS_toolkit/tables/helmholtz/helm_table.dat
expected_table=$(git show "HEAD:$table" | sed -n 's/^oid sha256://p' | tr -d '\r')
[[ "$expected_table" =~ ^[0-9a-f]{64}$ ]]
printf '%s  %s\n' "$expected_table" "$table" | sha256sum --check --status -
test ! -e "$out"
available=$(df -B1 --output=avail . | tail -n 1 | tr -d ' ')
test "$available" -ge 8589934592
test -f "$highfive/CMakeLists.txt"
mkdir -p "$out"
git status --short > "$out/source-status.txt"
git diff --binary -- . ':(exclude)EOS_toolkit/tables/helmholtz/helm_table.dat' > "$out/source-tracked.patch"
git rev-parse HEAD > "$out/source-head.txt"
find CMakeLists.txt src cmake tests "$table" -type f -print0 | sort -z | xargs -0 sha256sum > "$out/source-files.sha256"
cmake -S . -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_C_COMPILER=/usr/bin/gcc-11 -DCMAKE_CXX_COMPILER=/usr/bin/g++-11 \
  -DCMAKE_CUDA_COMPILER=/home/ubuntu/projects/.envs/arch/bin/nvcc \
  -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/g++-11 -DCMAKE_CUDA_ARCHITECTURES=90 \
  -DARCH_ENABLE_CUDA=ON -DARCH_ENABLE_KLU=OFF -DARCH_ENABLE_CUDSS=OFF \
  -DARCH_ENABLE_OPENMP=ON -DARCH_FETCH_SUITESPARSE=OFF \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DFETCHCONTENT_SOURCE_DIR_HIGHFIVE="$highfive" \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$build/bin" \
  -DARCH_CUDA_HEAVY_COMPILE_JOBS=4 -DCCACHE_PROGRAM=CCACHE_PROGRAM-NOTFOUND \
  '-DCMAKE_CXX_COMPILER_LAUNCHER=/usr/bin/time;-f;ARCH_COMPILE_METRIC elapsed_seconds=%e peak_rss_kib=%M exit_code=%x command=%C' \
  '-DCMAKE_CUDA_COMPILER_LAUNCHER=/usr/bin/time;-f;ARCH_COMPILE_METRIC elapsed_seconds=%e peak_rss_kib=%M exit_code=%x command=%C' \
  > "$out/configure.log" 2>&1
# Complete production archive/executable, original 24 contracts, all 10 batch
# contracts, independent burn reference and the new indicator leaf contract.
# Several CTests share one executable; this is not a reduced production route set.
targets=(arch_cuda_backend ARCH arch_cuda_single_level_validation
  arch_burn_mainline_reference arch_compute_backend arch_shared_stage_scheduler
  arch_state_residency arch_device_block_store_lifecycle arch_amr_operation_plans
  arch_same_level_exchange_plan arch_cuda_hydro_block arch_cuda_multiblock_hydro
  arch_cuda_multiblock_diffusion arch_cuda_multiblock_burn arch_cuda_amr_exchange
  arch_cuda_regrid_transaction arch_cuda_regrid_migration arch_cuda_store_lifecycle
  arch_cuda_hydro_leaf_parity arch_cuda_boundary_plan_parity arch_cuda_hydro_eos_failure
  arch_cuda_hydro_dispatch arch_cuda_diffusion_rkl_parity arch_cuda_refinement_indicators)
printf '%s\n' "${targets[@]}" > "$out/required-targets.txt"
timeout --signal=INT --kill-after=30s 24h "$python" tools/run_memory_guarded.py \
  --min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard \
  --log "$out/build-child.log" -- \
  cmake --build "$build" --target "${targets[@]}" --parallel 4 --verbose \
  > "$out/build-guard.log" 2>&1
sha256sum -c "$out/source-files.sha256" > "$out/source-identity-check.log"
sha256sum "$build/bin/ARCH" "$build/libarch_cuda_backend.a" \
  "$build/arch_cuda_single_level_validation" > "$out/artifacts.sha256"
printf 'S5_CLEAN_BUILD_PASS\n'
