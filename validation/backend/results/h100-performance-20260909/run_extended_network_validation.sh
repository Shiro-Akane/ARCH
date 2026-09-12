#!/usr/bin/env bash
# After the upstream focused matrix and the test-only capacity extension pass.
set -euo pipefail
cd /home/ubuntu/projects/ARCH-perf-20260909
export PATH="/home/ubuntu/projects/.envs/arch/bin:$PATH"
export OMP_NUM_THREADS=8 OMP_DYNAMIC=FALSE OMP_PLACES=cores OMP_PROC_BIND=close
python="$PWD/build/network-python-20260909/bin/python"
output="$PWD/build/large-network-20260909"
build="$output/release"
failed=0
for network in audit150 audit200; do
  # Do not promote a failed/absent original focused gate to a scaling campaign.
  if ! "$python" -c 'import json,sys; e=json.load(open(sys.argv[1])); assert e["focused_gate_pass"] and e["identity_verified_after_run"] and not e["release_qualified"]' \
      "$output/results/$network-fourstep/evidence.json"; then
    failed=1
    continue
  fi
  for case in default-recheck capacity32 capacity128 long40; do
    first=2 second=3 pool=2 steps=4 interval=1e-10
    case "$case" in
      capacity32) first=32; second=33; pool=32 ;;
      capacity128) first=128; second=129; pool=32 ;;
      long40) steps=40; interval=1e-8 ;;
    esac
    if ! "$python" tools/run_memory_guarded.py --min-available-mib 16384 \
      --max-swap-growth-mib 0 --pressure-guard --gpu-memory-device 0 \
      --log "$output/$network-$case.log" -- \
      "$python" validation/network/run_sparse_validation.py \
        --build-dir "$build" --output-dir "$output/results/$network-$case" \
        --network-id "$network" --rho 1e7 --temperature 3e9 \
        --interval "$interval" --cv 1e8 --rtol 1e-7 --steps "$steps" \
        --storage-cells "$first" "$second" --pool-cells "$pool" \
        --composition c12=0.5 o16=0.5 --timeout 2400; then
      failed=1
      # Inspect a network's first failure before increasing its workload.
      break
    fi
  done
done
exit "$failed"
