#!/usr/bin/env bash
# Only after the clean large build has finished, and S5 + shared composition
# fixes are numerically qualified and published. Preserve original products;
# let Ninja rebuild every actual
# dependency. This is a documented incremental integration, not another clean build.
set -euo pipefail
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
export PYTHONDONTWRITEBYTECODE=1
root=/home/ubuntu/projects/ARCH-large-application-20260914
python=/home/ubuntu/projects/ARCH-microphysics-20260914/build/p12-20260914/validation-python/bin/python
base="$root/build/p12-20260914/large-application-v2"
out="$base/fixed-integration-v1"
build="$base/release"
commit=${1:?Published S5 plus shared-composition-fix commit required}
[[ "$commit" =~ ^[0-9a-f]{40}$ ]]
cd "$root"
test "$(git rev-parse HEAD)" = 32cc416220a69cad5ab12030ba7b26db360bcd22
test ! -e "$out"
sha256sum -c "$base/artifacts.sha256"
sha256sum -c "$base/source-files.sha256" > "$base/source-before-integration.log"
git diff --quiet HEAD -- CMakeLists.txt src cmake tests
git cat-file -e "$commit^{commit}"
git merge-base --is-ancestor HEAD "$commit"
"$python" -c 'import subprocess,sys; names=subprocess.check_output(["git","diff","--name-only","HEAD",sys.argv[1],"--","src","cmake","CMakeLists.txt","tests"],text=True).splitlines(); assert set(names)=={"src/cuda/amr/RefinementIndicators.cu","src/cuda/amr/RefinementIndicators.h","src/cuda/runtime/amr/CudaBackendIndicators.cpp","src/cuda/runtime/control/CudaBackendInternal.h","tests/cuda/test_cuda_multiblock_hydro.cu","tests/cuda/test_refinement_indicators.cpp","src/amr/LimitedLinearProlongation.h","src/amr/GhostExchange.h","src/cuda/amr/CoarseFineExchangeKernels.cuh","src/cuda/hydro/HydroReconstructionPolicies.cuh","src/numerics/reconstruction/Reconstruction.h","tests/cuda/test_cuda_amr_composition.cu","tests/host/test_amr_operation_plans.cpp","tests/fixtures/amr_composition_test_cases.h"},names' "$commit"
"$python" -c 'import json,pathlib; root=pathlib.Path("/home/ubuntu/projects/ARCH-multiphysics-fix-20260914/build/fix-20260914"); phases="contracts batch-contracts canonical independent first-law nse coupled coupled-all-transport amr curved lifecycle restart coupled-restart coupled-all-transport-restart tails".split(); assert all(json.load(open(root/"validation-v1"/p/"record.json"))["status"]=="passed" for p in phases); r=json.load(open(root/"coupled-scale-v1/evidence.json")); assert r["status"]=="passed" and r["identity_verified"] and len(r["lanes"])==36 and len(r["comparisons"])==18'
test "$(df -Pk "$root" | awk 'NR==2 {print $4}')" -ge 3145728
mkdir -p "$out"
cp "$base/artifacts.sha256" "$out/base-artifacts.sha256"
find "$build/CMakeFiles" -type f -path '*/generated/cuda_sparse_burn/custom_audit*.cu.o' -print0 | sort -z | xargs -0 sha256sum > "$out/large-objects-before.sha256"
test "$(wc -l < "$out/large-objects-before.sha256")" = 8
tar --use-compress-program='zstd -T2 -3' -cf "$out/base-products.tar.zst" -C "$build" \
  bin/ARCH libarch_cuda_backend.a libarch_cuda_sparse_provider.a arch_cuda_single_level_validation
sha256sum "$out/base-products.tar.zst" > "$out/base-products.sha256"
sha256sum -c "$out/base-artifacts.sha256" > "$out/base-products-after-archive.log"
git merge --ff-only "$commit" > "$out/fast-forward.log" 2>&1
git rev-parse HEAD > "$out/source-head.txt"
git status --short > "$out/source-status.txt"
find CMakeLists.txt src cmake tests -type f -print0 | sort -z | xargs -0 sha256sum > "$out/source-files.sha256"
timeout --signal=INT --kill-after=30s 6h "$python" tools/run_memory_guarded.py \
  --min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard \
  --log "$out/build-child.log" -- \
  cmake --build "$build" --target arch_cuda_backend ARCH arch_cuda_single_level_validation \
    --parallel 4 --verbose > "$out/build-guard.log" 2>&1
sha256sum -c "$out/source-files.sha256" > "$out/source-identity-after-build.log"
sha256sum -c "$base/network-files.sha256" > "$out/network-identity.log"
sha256sum -c "$base/cudss.sha256" > "$out/cudss-identity.log"
sha256sum -c "$base/cublas.sha256" > "$out/cublas-identity.log"
find "$build/CMakeFiles" -type f -path '*/generated/cuda_sparse_burn/custom_audit*.cu.o' -print0 | sort -z | xargs -0 sha256sum > "$out/large-objects-after.sha256"
test "$(wc -l < "$out/large-objects-after.sha256")" = 8
sha256sum "$build/bin/ARCH" "$build/libarch_cuda_backend.a" "$build/libarch_cuda_sparse_provider.a" \
  "$build/arch_cuda_single_level_validation" > "$out/artifacts.sha256"
printf 'LARGE_FIXED_INTEGRATION_BUILD_PASS\n'
