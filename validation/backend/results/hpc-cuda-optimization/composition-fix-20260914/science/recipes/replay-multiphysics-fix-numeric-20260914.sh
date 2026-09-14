#!/usr/bin/env bash
# Run only after the full candidate build and other GPU jobs have exited.
# Numerical validation, explicitly not quiet-window performance evidence.
set -euo pipefail
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
export OMP_NUM_THREADS=8 OMP_DYNAMIC=FALSE OMP_PLACES=cores OMP_PROC_BIND=close
export PYTHONDONTWRITEBYTECODE=1
root=/home/ubuntu/projects/ARCH-multiphysics-fix-20260914
build="$root/build/fix-20260914/release"
out="$root/build/fix-20260914/validation-v1"
python=/home/ubuntu/projects/ARCH-microphysics-20260914/build/p12-20260914/validation-python/bin/python
cd "$root"
test ! -e "$out"
sha256sum -c build/fix-20260914/full-build-v1/artifacts.sha256
mkdir -p "$out"
expression='^(amr_operation_plans|cuda_amr_composition|cuda_refinement_indicators)$'
ctest --test-dir "$build" --show-only=json-v1 -R "$expression" > "$out/fix-inventory.json"
"$python" -c 'import json,sys; r=json.load(open(sys.argv[1])); assert sorted(t["name"] for t in r["tests"])==["amr_operation_plans","cuda_amr_composition","cuda_refinement_indicators"]' "$out/fix-inventory.json"
ctest --test-dir "$build" --verbose --no-tests=error --parallel 1 \
  --timeout 600 -R "$expression" > "$out/fix-contracts.log" 2>&1
if grep -Eq '\*\*\*Skipped|Not Run|tests did not run' "$out/fix-contracts.log"; then exit 1; fi
printf 'MULTIPHYSICS_FIX_CONTRACT_PASS\n'
for phase in contracts batch-contracts canonical independent first-law nse coupled coupled-all-transport amr curved lifecycle restart coupled-restart coupled-all-transport-restart tails; do
  printf 'MULTIPHYSICS_FIX_NUMERIC_START %s\n' "$phase"
  timeout --signal=INT --kill-after=30s 6h "$python" validation/backend/run_microphysics_validation.py \
    --build-dir "$build" --output-dir "$out/$phase" --phase "$phase" \
    > "$out/$phase-driver.stdout" 2> "$out/$phase-driver.stderr"
  printf 'MULTIPHYSICS_FIX_NUMERIC_PASS %s\n' "$phase"
done
sha256sum -c build/fix-20260914/full-build-v1/artifacts.sha256 > "$out/artifact-identity-check.log"
sha256sum -c build/fix-20260914/full-build-v1/source-files.sha256 > "$out/source-identity-check.log"
printf 'MULTIPHYSICS_FIX_NUMERIC_ALL_PASS\n'
