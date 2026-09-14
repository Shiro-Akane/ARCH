#!/usr/bin/env bash
# Run only after all builds and other GPU/CPU diagnostics have exited.
# Startup-to-exit timings include I/O; no observer and no steady-state claim.
set -euo pipefail
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
export OMP_NUM_THREADS=8 OMP_DYNAMIC=FALSE OMP_PLACES=cores OMP_PROC_BIND=close
export PYTHONDONTWRITEBYTECODE=1
root=/home/ubuntu/projects/ARCH-s5-indicator-20260914
baseline=/home/ubuntu/projects/ARCH-microphysics-20260914
python="$baseline/build/p12-20260914/validation-python/bin/python"
out="$root/build/s5-20260914/timing"
mode=${1:?pilot or formal}
[[ "$mode" == pilot || "$mode" == formal ]]
cd "$root"
sha256sum -c build/s5-20260914/artifacts.sha256
"$python" -c 'import json,pathlib,sys; p=pathlib.Path(sys.argv[1]); phases="contracts batch-contracts canonical independent first-law nse coupled coupled-all-transport amr curved lifecycle restart coupled-restart coupled-all-transport-restart tails".split(); assert all(json.load(open(p/x/"record.json"))["status"]=="passed" for x in phases)' "$root/build/s5-20260914/validation-v1"
modules=(diffusion_rkl1 diffusion_rkl2 burn_be_nr burn_bd burn_ros4
  coupled_be_nr_rkl1_all_transport coupled_be_nr_rkl2_all_transport
  coupled_bd_rkl1_all_transport coupled_bd_rkl2_all_transport
  coupled_ros4_rkl1_all_transport coupled_ros4_rkl2_all_transport)
options=(--threads 1 8 16 --baseline-threads 8 --gpu-threads 8 --blocks 8 32 128)
if [[ "$mode" == pilot ]]; then options+=(--pilot); fi
for module in "${modules[@]}"; do
  directory="$out/$mode-$module-v1"
  test ! -e "$directory"
  test "$(df -Pk "$root" | awk 'NR==2 {print $4}')" -ge 8388608
  mkdir -p "$out"
  printf 'S5_TIMING_START %s %s\n' "$mode" "$module"
  "$python" validation/backend/run_microphysics_timing.py \
    --candidate-source "$root" --candidate-build "$root/build/s5-20260914/release" \
    --baseline-source "$baseline" --baseline-build "$baseline/build/p12-20260914/release" \
    --output-root "$directory" --modules "$module" --timeout 1800 "${options[@]}" \
    > "$out/$mode-$module-v1.stdout" 2> "$out/$mode-$module-v1.stderr"
  printf 'S5_TIMING_PASS %s %s\n' "$mode" "$module"
done
sha256sum -c build/s5-20260914/artifacts.sha256
