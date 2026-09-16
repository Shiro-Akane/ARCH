#!/usr/bin/env bash
# One-shot follow-up worker: disconnecting SSH does not restart/kill a trajectory.
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1
root=/home/ubuntu/projects/ARCH-microphysics-20260914
base="$root/build/p12-20260914"
recipes="$base/be-extended-wall-recipes-v1"
python="$base/validation-python/bin/python"
control="$base/be-extended-wall-controller-v1"
test -d "$control"
exec 9>"$control/lock"
flock -n 9
finish() { result=$?; printf '%s\n' "$result" > "$control/exit-code"; exit "$result"; }
trap finish EXIT
bash -n "$recipes/replay-large-be-extended-wall-20260914.sh"
"$python" -m unittest discover -s "$recipes/tests/tooling" -p test_sparse_capacity_recipe.py -v \
 > "$recipes/server-recipe-tests.log" 2>&1
set +e
bash "$recipes/replay-large-be-extended-wall-20260914.sh" \
 > "$base/factor-cache/long-be-extended-wall-v1-launcher.log" 2>&1
result=$?
set -e
printf 'BE_FOLLOWUP_RUNTIME_EXIT %s\n' "$result"
"$python" "$recipes/archive-large-be-extended-wall-20260914.py" \
 > "$base/factor-cache/long-be-extended-wall-v1-collection.log" 2>&1
printf 'BE_FOLLOWUP_ARCHIVE_COMPLETE\n'
