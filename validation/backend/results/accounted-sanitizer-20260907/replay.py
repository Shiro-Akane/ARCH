"""Replay this focused sanitizer campaign using the configured CTest commands.

Run under tools/run_memory_guarded.py. The shared provenance and subprocess
helpers own identity and logging; this recipe adds no physical implementation.
Complete ARCH/restart paths and sustained capacity are separate evidence.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "tools"))
import validation_provenance as provenance
from validate_backend_results import require_empty_output_root, run_arch_with_logs

CASES = (
    "cuda_regrid_transaction", "cuda_regrid_migration", "cuda_amr_composition",
    "cuda_amr_exchange", "cuda_curvilinear_geometry_smoke",
    "cuda_refinement_indicators", "cuda_single_level_smoke",
    "cuda_cudss_sparse_solver", "cuda_sparse_be_nr_batch",
    "cuda_burn_eos_failure", "cuda_hydro_eos_failure",
    "eos_host_device_parity", "network_nse_device",
    "cuda_generated_math_audit31", "cuda_generated_math_weak_urca",
    "cuda_backend_burn_aprox19_be_nr", "cuda_backend_burn_aprox19_bd",
    "cuda_backend_burn_aprox19_ros4",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--sanitizer", type=Path, required=True)
    parser.add_argument("--tool", choices=("memcheck", "racecheck"), required=True)
    args = parser.parse_args()
    build, output, sanitizer = (p.resolve() for p in
                                (args.build_dir, args.output_dir, args.sanitizer))
    require_empty_output_root(output)
    output.mkdir(parents=True, exist_ok=True)
    query = run_arch_with_logs(["ctest", "--test-dir", str(build), "--show-only=json-v1"],
        source_root=ROOT, lane_root=output, timeout=60)
    if query.returncode:
        raise RuntimeError("cannot resolve configured test commands")
    tests = {t["name"]: t["command"] for t in json.loads(query.stdout)["tests"]}
    commands = {name: tests[name] for name in CASES}
    artifacts = {name: Path(command[0]) for name, command in commands.items()}
    def identity():
        return provenance.capture_focused(artifacts=artifacts, source_root=ROOT, build_dir=build)
    before, tool_identity = identity(), provenance.file_identity(sanitizer)
    recipe_identity = provenance.file_identity(Path(__file__).resolve())
    started, records = datetime.now(timezone.utc).isoformat(), []
    for name, command in commands.items():
        lane = output / name
        lane.mkdir()
        report = lane / "sanitizer.log"
        prefix = [str(sanitizer), "--tool", args.tool, "--print-session-details",
                  "--require-cuda-init", "yes", "--error-exitcode", "86",
                  "--log-file", str(report)]
        if args.tool == "memcheck":
            prefix += ["--leak-check", "full"]
        result = run_arch_with_logs(prefix + command, source_root=ROOT,
                                   lane_root=lane, timeout=1200)
        if result.returncode:
            raise RuntimeError(f"{name}: sanitizer/application exit {result.returncode}; see {lane}")
        text = report.read_text()
        if len(re.findall(r"^========= Process ID:\s+\d+\s*$", text, re.M)) != 1:
            raise RuntimeError(f"{name}: missing unique instrumented process")
        expected = (["ERROR SUMMARY: 0 errors", "LEAK SUMMARY: 0 bytes leaked in 0 allocations"]
                    if args.tool == "memcheck" else
                    ["RACECHECK SUMMARY: 0 hazards displayed (0 errors, 0 warnings)"])
        summaries = [line.removeprefix("========= ") for line in text.splitlines()
                     if "SUMMARY:" in line]
        if sorted(summaries) != sorted(expected):
            raise RuntimeError(f"{name}: sanitizer warnings/errors or incomplete summaries: {summaries}")
        records.append(dict(name=name, command=prefix + command, returncode=result.returncode,
            report=provenance.file_identity(report),
            stdout=provenance.file_identity(lane / "arch.stdout"),
            stderr=provenance.file_identity(lane / "arch.stderr"), summaries=summaries))
        print(f"{args.tool} PASS: {name}", flush=True)
    provenance.require_unchanged(before, identity())
    if tool_identity != provenance.file_identity(sanitizer) \
            or recipe_identity != provenance.file_identity(Path(__file__).resolve()):
        raise RuntimeError("sanitizer or replay recipe changed during execution")
    evidence = dict(schema=1, scope="focused-backend-sanitizer", tool=args.tool,
        focused_gate_pass=True, release_qualified=False, identity=before,
        identity_verified_after_run=True, sanitizer=tool_identity, recipe=recipe_identity,
        started_utc=started, finished_utc=datetime.now(timezone.utc).isoformat(), cases=records)
    (output / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"{args.tool}: all {len(records)} focused checks PASS; {output / 'evidence.json'}")


if __name__ == "__main__":
    main()
