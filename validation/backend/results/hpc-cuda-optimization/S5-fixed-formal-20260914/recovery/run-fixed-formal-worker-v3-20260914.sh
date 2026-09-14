#!/usr/bin/env bash
# V3 changes process lifetime only; original physics/timing/archival recipes stay frozen.
set -euo pipefail
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
export PYTHONDONTWRITEBYTECODE=1
root=/home/ubuntu/projects/ARCH-multiphysics-fix-20260914
recipes=/home/ubuntu/projects/ARCH-microphysics-20260914/build/p12-20260914
python="$recipes/validation-python/bin/python"
base="$root/build/fix-20260914/timing"
module=${1:?module}
mode=${2:?run or collect}
case "$module" in
 diffusion_rkl1|diffusion_rkl2|burn_be_nr|burn_bd|burn_ros4|coupled_be_nr_rkl1_all_transport|coupled_be_nr_rkl2_all_transport|coupled_bd_rkl1_all_transport|coupled_bd_rkl2_all_transport|coupled_ros4_rkl1_all_transport|coupled_ros4_rkl2_all_transport) ;;
 *) exit 2 ;;
esac
[[ "$mode" == run || "$mode" == collect ]]
label="formal-$module-v1"
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
 timeout --signal=INT --kill-after=30s 24h "$python" tools/run_memory_guarded.py \
  --min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard --gpu-memory-device 0 \
  --log "$base/$label-child.log" -- bash "$recipes/replay-fixed-timing-20260914.sh" formal "$module" \
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
