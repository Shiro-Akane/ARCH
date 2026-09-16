#!/usr/bin/env bash
# V3.1: independent process lifetime; remaining coupled phases get a larger WALL
# guard only. Production inputs/binaries, timing algorithm and scientific budgets
# stay frozen. The already-running first phase keeps its original 1800 s guard.
set -euo pipefail
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
export PYTHONDONTWRITEBYTECODE=1
root=/home/ubuntu/projects/ARCH-multiphysics-fix-20260914
recipes=/home/ubuntu/projects/ARCH-microphysics-20260914/build/p12-20260914
python="$recipes/validation-python/bin/python"
base="$root/build/fix-20260914/timing"
module=${1:?module}
mode=${2:?run, collect or internal-sample-wall3600}
case "$module" in
 diffusion_rkl1|diffusion_rkl2|burn_be_nr|burn_bd|burn_ros4|coupled_be_nr_rkl1_all_transport|coupled_be_nr_rkl2_all_transport|coupled_bd_rkl1_all_transport|coupled_bd_rkl2_all_transport|coupled_ros4_rkl1_all_transport|coupled_ros4_rkl2_all_transport) ;;
 *) exit 2 ;;
esac
label="formal-$module-v1"
long_wall=false
case "$module" in
 coupled_be_nr_rkl2_all_transport|coupled_bd_rkl1_all_transport|coupled_bd_rkl2_all_transport|coupled_ros4_rkl1_all_transport|coupled_ros4_rkl2_all_transport) long_wall=true ;;
esac
if [[ "$mode" == internal-sample-wall3600 ]]; then
 # Called only inside the same outer 24 h / memory guard. This is the original
 # frozen invocation with --timeout 3600 instead of 1800, not fewer samples or
 # a different physical trajectory. Keep both old and new recipe evidence.
 [[ "$long_wall" == true ]]
 baseline=/home/ubuntu/projects/ARCH-corrected-fused-baseline-20260914
 cd "$root"
 sha256sum -c build/fix-20260914/full-build-v1/artifacts.sha256
 (cd "$baseline" && sha256sum -c build/corrected-fused-20260914/artifacts.sha256)
 "$python" -c 'import json,pathlib,sys; p=pathlib.Path(sys.argv[1]); phases="contracts batch-contracts canonical independent first-law nse coupled coupled-all-transport amr curved lifecycle restart coupled-restart coupled-all-transport-restart tails".split(); assert all(json.load(open(p/x/"record.json"))["status"]=="passed" for x in phases)' "$root/build/fix-20260914/validation-v1"
 "$python" -c 'import json,sys; r=json.load(open(sys.argv[1])); assert r["status"]=="passed" and r["identity_verified"] and len(r["cases"])==18 and len(r["lanes"])==36 and len(r["comparisons"])==18' "$root/build/fix-20260914/coupled-scale-v1/evidence.json"
 test ! -e "$base/$label"
 test "$(df -Pk "$root" | awk 'NR==2 {print $4}')" -ge 8388608
 export OMP_NUM_THREADS=8 OMP_DYNAMIC=FALSE OMP_PLACES=cores OMP_PROC_BIND=close
 date --iso-8601=seconds > "$base/$label-host-before.log"
 ps -eo pid,pcpu,pmem,etimes,comm --sort=-pcpu >> "$base/$label-host-before.log"
 nvidia-smi >> "$base/$label-host-before.log"
 printf 'FIXED_TIMING_START formal %s wall_seconds=3600\n' "$module"
 "$python" validation/backend/run_microphysics_timing.py \
  --candidate-source "$root" --candidate-build "$root/build/fix-20260914/release" \
  --baseline-source "$baseline" --baseline-build "$baseline/build/corrected-fused-20260914/release" \
  --output-root "$base/$label" --modules "$module" --timeout 3600 \
  --threads 1 8 16 --baseline-threads 8 --gpu-threads 8 --blocks 8 32 128 \
  > "$base/$label.stdout" 2> "$base/$label.stderr"
 date --iso-8601=seconds > "$base/$label-host-after.log"
 ps -eo pid,pcpu,pmem,etimes,comm --sort=-pcpu >> "$base/$label-host-after.log"
 nvidia-smi >> "$base/$label-host-after.log"
 sha256sum -c build/fix-20260914/full-build-v1/artifacts.sha256
 (cd "$baseline" && sha256sum -c build/corrected-fused-20260914/artifacts.sha256)
 printf 'FIXED_TIMING_PASS formal %s wall_seconds=3600\n' "$module"
 exit 0
fi
[[ "$mode" == run || "$mode" == collect ]]
control="$base/controller-v3-$label"
test -d "$control"
exec 9>"$control/lock"
flock -n 9
finish() { result=$?; printf '%s\n' "$result" > "$control/exit-code"; exit "$result"; }
trap finish EXIT
cd "$root"
if pgrep -x ARCH >/dev/null || pgrep -x nvcc >/dev/null || pgrep -x ptxas >/dev/null; then
 echo 'Other application/build is active; no competing work started' >&2
 exit 2
fi
test -z "$(nvidia-smi --query-compute-apps=pid --format=csv,noheader)"
if [[ "$mode" == run ]]; then
 test ! -e "$base/$label"
 # Save this orchestration recipe separately from the frozen scientific recipe.
 cp "$0" "$base/$label-controller-recipe-v3.sh"
 sha256sum "$0" > "$base/$label-controller-recipe-v3.sha256"
 vmstat 1 3 > "$base/$label-idle-preflight.log"
 awk 'NR>3 { n++; if ($15<90 || $7!=0 || $8!=0) bad=1 } END { exit (n!=2 || bad) }' \
  "$base/$label-idle-preflight.log"
 printf 'S5_FORMAL_PHASE_BEGIN %s\n' "$module"
 execution=(bash "$recipes/replay-fixed-timing-20260914.sh" formal "$module")
 wall=1800
 if [[ "$long_wall" == true ]]; then
  wall=3600
  execution=(bash "$0" "$module" internal-sample-wall3600)
 fi
 printf '{"module":"%s","runtime_wall_timeout_seconds":%s,"outer_guard_hours":24,"physics_changed":false,"samples_changed":false,"policy":"v3.1 prospective wall-budget adjustment; original first phase and prior failures retained"}\n' \
  "$module" "$wall" > "$base/$label-wall-policy-v3.json"
 timeout --signal=INT --kill-after=30s 24h "$python" tools/run_memory_guarded.py \
  --min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard --gpu-memory-device 0 \
  --log "$base/$label-child.log" -- "${execution[@]}" \
  > "$base/$label-guard.log" 2>&1
 printf 'S5_FORMAL_PHASE_SAMPLES_PASS %s\n' "$module"
fi
if [[ -e "$base/$label-archive" ]]; then
 # A lost connection may have left the original controller to archive normally.
 # Accept only its fully completed archive, never partially produced outputs.
 grep -Fx "FORMAL_PHASE_ARCHIVE_PASS $module" "$base/collection-$module-v1.log"
 test -f "$base/$label-archive/compact/raw-archive.json"
 test -f "$root/build/fixed-formal-$module-v1.tar.zst"
 test -f "$root/build/fixed-formal-$module-v1-compact.tar.zst"
else
 "$python" "$recipes/archive-formal-timing-v2-20260914.py" "$module" \
  > "$base/collection-$module-v1.log" 2>&1
fi
printf 'S5_FORMAL_PHASE_ARCHIVE_PASS %s\n' "$module"
