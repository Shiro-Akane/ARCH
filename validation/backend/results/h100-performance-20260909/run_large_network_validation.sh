#!/usr/bin/env bash
# Run only after the uncontended performance/diagnostic window has ended.
set -euo pipefail
cd /home/ubuntu/projects/ARCH-perf-20260909
export PATH="/home/ubuntu/projects/.envs/arch/bin:$PATH"
export OMP_NUM_THREADS=8 OMP_DYNAMIC=FALSE OMP_PLACES=cores OMP_PROC_BIND=close
python="$PWD/build/network-python-20260909/bin/python"
output="$PWD/build/large-network-20260909"
build="$output/release"
"$python" tools/run_memory_guarded.py --min-available-mib 16384 \
  --max-swap-growth-mib 0 --pressure-guard --log "$output/focused-build.log" -- \
  /usr/bin/time -v cmake --build "$build" --parallel 2 --target \
  arch_cuda_generated_math_audit150 arch_cuda_generated_math_audit200 \
  arch_cuda_generated_sparse_burn_audit150 arch_cuda_generated_sparse_burn_audit200
"$python" tools/summarize_cuda_compile_memory.py "$output/focused-build.log" \
  --format commands --output "$output/compile-memory.csv"
ctest --test-dir "$build" -R '^cuda_generated_math_audit(150|200)$' \
  --output-on-failure --output-log "$output/generated-math-ctest.log"
failed=0
for network in audit150 audit200; do
  if ! "$python" tools/run_memory_guarded.py --min-available-mib 16384 \
    --max-swap-growth-mib 0 --pressure-guard --gpu-memory-device 0 \
    --log "$output/$network-fourstep.log" -- \
    "$python" validation/network/run_sparse_validation.py \
      --build-dir "$build" --output-dir "$output/results/$network-fourstep" \
      --network-id "$network" --rho 1e7 --temperature 3e9 \
      --interval 1e-10 --cv 1e8 --rtol 1e-7 --steps 4 \
      --composition c12=0.5 o16=0.5 --timeout 1200; then
    failed=1
  fi
done
exit "$failed"
