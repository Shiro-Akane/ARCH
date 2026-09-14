#!/usr/bin/env bash
# Numerical-only sequence. Other CPU builds may continue; no formal timing.
set -euo pipefail
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
export OMP_NUM_THREADS=8 OMP_DYNAMIC=FALSE OMP_PLACES=cores OMP_PROC_BIND=close
export PYTHONDONTWRITEBYTECODE=1
root=/home/ubuntu/projects/ARCH-multiphysics-fix-20260914
original=/home/ubuntu/projects/ARCH-microphysics-20260914
python="$original/build/p12-20260914/validation-python/bin/python"
recipes="$original/build/p12-20260914"
cd "$root"
out=build/fix-20260914
sha256sum -c "$out/full-build-v1/artifacts.sha256"
test ! -e "$out/critical-contracts.log"
ctest --test-dir "$out/release" --verbose --no-tests=error --parallel 1 --timeout 600 \
  -R '^(amr_operation_plans|cuda_amr_composition|cuda_refinement_indicators)$' \
  > "$out/critical-contracts.log" 2>&1
if grep -Eq '\*\*\*Skipped|Not Run|tests did not run' "$out/critical-contracts.log"; then exit 1; fi
printf 'FIXED_CRITICAL_CONTRACTS_PASS\n'
"$python" "$recipes/replay-fixed-coupled-scale-20260914.py" \
  --label coupled-critical-b128-v1 --modules coupled_bd_rkl2_all_transport --blocks 128 \
  > "$out/critical-b128-driver.log" 2>&1
printf 'FIXED_CRITICAL_B128_PASS\n'
bash "$recipes/replay-multiphysics-fix-numeric-20260914.sh"
"$python" "$recipes/replay-fixed-coupled-scale-20260914.py" \
  > "$out/coupled-scale-driver-v1.log" 2>&1
sha256sum -c "$out/full-build-v1/artifacts.sha256"
printf 'FIXED_ALL_SCIENCE_PASS\n'
