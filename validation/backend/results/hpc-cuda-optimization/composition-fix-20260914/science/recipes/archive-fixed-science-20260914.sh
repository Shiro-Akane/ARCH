#!/usr/bin/env bash
# Only after the complete fixed scientific sequence exits; not during timing.
set -euo pipefail
root=/home/ubuntu/projects/ARCH-multiphysics-fix-20260914
original=/home/ubuntu/projects/ARCH-microphysics-20260914
baseline=/home/ubuntu/projects/ARCH-corrected-fused-baseline-20260914
python="$original/build/p12-20260914/validation-python/bin/python"
cd "$root"
base=build/fix-20260914
out="$base/science-archive-v1"
test ! -e "$out"
test ! -e build/fixed-science-v1.tar.zst
test ! -e build/fixed-science-compact-v1.tar.zst
test "$(df -Pk "$root" | awk 'NR==2 {print $4}')" -ge 3145728
"$python" -c 'import json,pathlib,sys; p=pathlib.Path(sys.argv[1]); phases="contracts batch-contracts canonical independent first-law nse coupled coupled-all-transport amr curved lifecycle restart coupled-restart coupled-all-transport-restart tails".split(); assert all(json.load(open(p/x/"record.json"))["status"]=="passed" and json.load(open(p/x/"record.json"))["identity_verified_after_run"] for x in phases)' "$base/validation-v1"
"$python" -c 'import json,sys; r=json.load(open(sys.argv[1])); assert r["status"]=="passed" and r["identity_verified"] and len(r["cases"])==18 and len(r["lanes"])==36 and len(r["comparisons"])==18' "$base/coupled-scale-v1/evidence.json"
grep -q 'valid=21 invalid_no_scatter=24' "$base/validation-v1/fix-contracts.log"
sha256sum -c "$base/full-build-v1/artifacts.sha256"
sha256sum -c "$base/full-build-v1/source-files.sha256" > "$base/source-identity-after-science.log"
mkdir -p "$out/compact/validation" "$out/compact/build" "$out/compact/details" "$out/compact/recipes" "$out/compact/baseline"
for item in "$base/validation-v1"/*/record.json; do
  phase=$(basename "$(dirname "$item")")
  cp "$item" "$out/compact/validation/$phase.json"
done
cp -a "$base/full-build-v1" "$out/compact/build/"
cp -a "$base/explicit-amr-test-rebuild-v1" "$out/compact/build/"
for file in build-pair-v1.json tooling-tests-v1.log critical-contracts.log science-child-v1.log science-guard-v1.log critical-b128-driver.log coupled-scale-driver-v1.log source-identity-after-science.log clean-leaf-v3-inventory.json clean-leaf-v3-contracts.log clean-leaf-v3-artifacts.sha256; do
  cp "$base/$file" "$out/compact/build/"
done
cp "$base/release/CMakeCache.txt" "$base/release/compile_commands.json" "$out/compact/build/"
cp "$base/validation-v1/fix-contracts.log" "$base/validation-v1/fix-inventory.json" "$base/validation-v1/artifact-identity-check.log" "$out/compact/build/"
for file in configure.log build-child.log build-guard.log source.patch source-head.txt source-files.sha256 source-identity-after.log artifacts.sha256; do
  cp "$baseline/build/corrected-fused-20260914/$file" "$out/compact/baseline/"
done
for file in replay-multiphysics-fix-full-20260914.sh replay-corrected-fused-baseline-20260914.sh replay-multiphysics-fix-numeric-20260914.sh replay-fixed-science-20260914.sh replay-fixed-coupled-scale-20260914.py replay-fixed-timing-20260914.sh verify-fixed-build-pair-20260914.py rebuild-explicit-amr-composition-target-20260914.py replay-fix-leaf-v3-20260914.sh archive-fixed-science-20260914.sh; do
  cp "$original/build/p12-20260914/$file" "$out/compact/recipes/"
done
cp "$original/build/p12-20260914/multiphysics-closure-muscl-overlay-v3.tar" "$out/"
for name in validation-v1 coupled-critical-b128-v1 coupled-scale-v1; do
  mkdir "$out/compact/details/$name"
  (cd "$base/$name"; find . -type f -name '*.json' -exec cp --parents --target-directory="$root/$out/compact/details/$name" {} +)
done
paths=("$base/validation-v1" "$base/coupled-critical-b128-v1" "$base/coupled-scale-v1"
  "$base/release/bin/ARCH" "$base/release/libarch_cuda_backend.a" "$base/release/arch_cuda_single_level_validation"
  "$base/release/arch_cuda_amr_composition" "$base/release/arch_amr_operation_plans"
  build/fix-leaves-v3-20260914/release/arch_cuda_amr_composition
  build/fix-leaves-v3-20260914/release/arch_amr_operation_plans
  "$out/multiphysics-closure-muscl-overlay-v3.tar" EOS_toolkit/tables/helmholtz/helm_table.dat)
find "${paths[@]}" -type f -print0 | sort -z | xargs -0 sha256sum > "$out/compact/raw-files.sha256"
tar --use-compress-program='zstd -T2 -3' -cf build/fixed-science-v1.tar.zst "${paths[@]}" "$out/compact"
sha256sum -c "$out/compact/raw-files.sha256" > "$out/after-archive-identity.log"
sha256sum build/fixed-science-v1.tar.zst > "$out/compact/raw-archive.sha256"
tar --use-compress-program='zstd -T2 -3' -cf build/fixed-science-compact-v1.tar.zst -C "$out" compact
sha256sum build/fixed-science-v1.tar.zst build/fixed-science-compact-v1.tar.zst
printf 'FIXED_SCIENCE_ARCHIVE_PASS\n'
