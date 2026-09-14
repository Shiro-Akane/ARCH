#!/usr/bin/env bash
# Execute only after the S5 build and any other GPU diagnostic have exited.
# Concurrent CPU compilation, if any, disqualifies timings but not these gates.
set -euo pipefail
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
export OMP_NUM_THREADS=8 OMP_DYNAMIC=FALSE OMP_PLACES=cores OMP_PROC_BIND=close
export PYTHONDONTWRITEBYTECODE=1
root=/home/ubuntu/projects/ARCH-s5-indicator-20260914
build="$root/build/s5-20260914/release"
out="$root/build/s5-20260914/validation-v1"
python=/home/ubuntu/projects/ARCH-microphysics-20260914/build/p12-20260914/validation-python/bin/python
cd "$root"
test ! -e "$out"
sha256sum -c build/s5-20260914/artifacts.sha256
mkdir -p "$out"
ctest --test-dir "$build" --show-only=json-v1 -R '^cuda_refinement_indicators$' > "$out/indicator-inventory.json"
"$python" -c 'import json,sys; r=json.load(open(sys.argv[1])); assert [t["name"] for t in r["tests"]]==["cuda_refinement_indicators"]' "$out/indicator-inventory.json"
ctest --test-dir "$build" --output-on-failure --no-tests=error --parallel 1 \
  --timeout 600 -R '^cuda_refinement_indicators$' > "$out/indicator-contract.log" 2>&1
if grep -Eq '\*\*\*Skipped|Not Run|tests did not run' "$out/indicator-contract.log"; then
  exit 1
fi
printf 'S5_INDICATOR_CONTRACT_PASS\n'
for phase in contracts batch-contracts canonical independent first-law nse coupled coupled-all-transport amr curved lifecycle restart coupled-restart coupled-all-transport-restart tails; do
  printf 'S5_NUMERIC_START %s\n' "$phase"
  timeout --signal=INT --kill-after=30s 6h "$python" validation/backend/run_microphysics_validation.py \
    --build-dir "$build" --output-dir "$out/$phase" --phase "$phase" \
    > "$out/$phase-driver.stdout" 2> "$out/$phase-driver.stderr"
  printf 'S5_NUMERIC_PASS %s\n' "$phase"
done
sha256sum -c build/s5-20260914/artifacts.sha256 > "$out/artifact-identity-check.log"
printf 'S5_NUMERIC_ALL_PASS\n'
