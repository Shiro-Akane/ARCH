#!/usr/bin/env bash
# Serial, separately archived physical profiles; never launches a next profile.
set -euo pipefail
profile=${1:?focused/capacity/long required}
case "$profile" in
  focused) outer=2h ;;
  capacity) outer=12h ;;
  long) outer=48h ;;
  *) exit 2 ;;
esac
root=/home/ubuntu/projects/ARCH-native-wave-v4-20260916
control="$root/batch-launch-$profile-control-v1"
python=/home/ubuntu/projects/ARCH-microphysics-20260914/build/p12-20260914/validation-python/bin/python
test -d "$control"
test ! -e "$control/exit-code"
exec 9>"$control/lock"
flock -n 9
finish() { result=$?; printf '%s\n' "$result" > "$control/exit-code"; exit "$result"; }
trap finish EXIT
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
export PYTHONDONTWRITEBYTECODE=1 OMP_NUM_THREADS=8 OMP_DYNAMIC=FALSE OMP_PLACES=cores OMP_PROC_BIND=close CUDA_VISIBLE_DEVICES=0
test -z "${LD_PRELOAD:-}"
test -z "${ARCH_NATIVE_WAVE_WORK_OBSERVER:-}"
test ! -e "$root/batch-launch-$profile-v1"
cd "$root/source"
for name in source-files network-files vendor artifacts; do
  sha256sum -c "$root/factory-control-v2/$name.sha256" > "$control/$name-check-before.log"
done
date -u > "$control/started-utc.txt"
sha256sum "$root/batch-trajectory-input-v1/"* > "$control/recipes.sha256"
set +e
timeout --signal=INT --kill-after=30s "$outer" "$python" tools/run_memory_guarded.py \
  --min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard --gpu-memory-device 0 \
  --log "$control/$profile-child.log" -- \
  "$python" "$root/batch-trajectory-input-v1/run_trajectories.py" --profile "$profile" \
  > "$control/$profile-guard.log" 2>&1
runtime=$?
set -e
printf '%s\n' "$runtime" > "$control/runtime-exit-code"
for name in source-files network-files vendor artifacts; do
  sha256sum -c "$root/factory-control-v2/$name.sha256" > "$control/$name-check-after.log"
done
sha256sum -c "$control/recipes.sha256" > "$control/recipes-check-after.log"
if test "$runtime" -ne 0; then exit "$runtime"; fi
printf 'BATCH_LAUNCH_NUCLEAR_PROFILE_PASS profile=%s not_helm_or_performance_qualified\n' "$profile"
