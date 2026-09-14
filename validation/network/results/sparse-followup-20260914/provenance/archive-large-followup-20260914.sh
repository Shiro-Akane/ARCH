#!/usr/bin/env bash
# Preserve successful AND timed-out focused trajectories; not timing evidence.
set -euo pipefail
root=/home/ubuntu/projects/ARCH-microphysics-20260914
old=/home/ubuntu/projects/ARCH-perf-20260909/build/large-network-20260909/release
python="$root/build/p12-20260914/validation-python/bin/python"
cd "$root"
base=build/p12-20260914/factor-cache
out="$base/followup-archive-v1"
test ! -e "$out"
test ! -e build/large-focused-followup-v1.tar.zst
test ! -e build/large-focused-followup-compact-v1.tar.zst
"$python" -c 'import json,pathlib,sys; p=pathlib.Path(sys.argv[1]); expected={"be-original-v2":2,"long-bd-ros-v1":8,"long-be-small-v1":2}; assert all((r:=json.load(open(p/n/"record.json")))["status"]=="passed" and len(r["runs"])==count for n,count in expected.items()); r=json.load(open(p/"long-duration-v2/record.json")); assert r["status"]=="failed" and "TimeoutExpired" in r["error"]' "$base"
mkdir -p "$out/compact/records" "$out/compact/provenance"
paths=()
for phase in be-original-v2 long-duration-v2 long-bd-ros-v1 long-be-small-v1; do
  paths+=("$base/$phase" "$base/$phase-child.log" "$base/$phase-guard.log")
  cp "$base/$phase/record.json" "$out/compact/records/$phase.json"
  cp "$base/$phase-child.log" "$base/$phase-guard.log" "$out/compact/records/"
done
paths+=("$base/candidate-v2" "$base/overlay-v2" tests/cuda/test_generated_sparse_burn.cpp validation/network/run_sparse_capacity.py)
cp "$old/CMakeCache.txt" "$old/compile_commands.json" "$out/compact/provenance/"
cp build/p12-20260914/replay-large-long-followup-20260914.sh build/p12-20260914/archive-large-followup-20260914.sh "$out/compact/provenance/"
git rev-parse HEAD > "$out/compact/provenance/source-head.txt"
git diff -- tests/cuda/test_generated_sparse_burn.cpp validation/network/run_sparse_capacity.py src/cuda > "$out/compact/provenance/source-patch.diff"
sha256sum "$old"/CMakeFiles/arch_cuda_generated_sparse_burn_audit{150,200}.dir/tests/cuda/test_generated_sparse_burn_factory.cu.o > "$out/compact/provenance/factory-objects.sha256"
find "${paths[@]}" -type f -print0 | sort -z | xargs -0 sha256sum > "$out/compact/raw-files.sha256"
tar --use-compress-program='zstd -T2 -3' -cf build/large-focused-followup-v1.tar.zst "${paths[@]}" "$out/compact"
sha256sum -c "$out/compact/raw-files.sha256" > "$out/after-archive-identity.log"
sha256sum build/large-focused-followup-v1.tar.zst > "$out/compact/raw-archive.sha256"
tar --use-compress-program='zstd -T2 -3' -cf build/large-focused-followup-compact-v1.tar.zst -C "$out" compact
sha256sum build/large-focused-followup{,-compact}-v1.tar.zst
printf 'LARGE_FOCUSED_FOLLOWUP_ARCHIVE_PASS\n'
