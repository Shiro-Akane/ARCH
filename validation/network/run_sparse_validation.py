"""Archive a real generated-network CPU KLU / CUDA cuDSS trajectory matrix.

Uses the production typed factory test, not an independent reaction oracle or a
whole-application certificate. All three ODEs and both storage generations must
pass; a selected-method diagnostic cannot qualify this focused matrix.
"""
from datetime import datetime, timezone
import argparse
import csv
import io
import json
import math
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import validation_provenance as provenance
from validate_backend_results import require_empty_output_root, run_arch_with_logs

METHODS = (1, 2, 3)  # The test transcript's registered BE_NR / BD / ROS4 IDs.
STORAGE_SIZES = (2, 3)


def finite(values):
    result = [float(value) for value in values]
    if not all(math.isfinite(value) for value in result):
        raise ValueError("nonfinite sparse trajectory evidence")
    return result


def parse_transcript(text, metadata, controls, steps):
    species = len(metadata["species"])
    equations = species + 1 + metadata["auxiliary_equations"]
    records = {key: [] for key in ("controls", "cpu_step", "gpu_step", "state", "metrics")}
    passed = 0
    for row in csv.reader(io.StringIO(text)):
        if not row:
            continue
        if row == ["GENERATED_SPARSE_BURN_PARITY_PASS"]:
            passed += 1
        elif row[0] in records:
            records[row[0]].append(row[1:])
        else:
            raise ValueError("unknown or failed sparse trajectory record")
    if passed != 1 or len(records["controls"]) != 1:
        raise ValueError("missing/duplicate sparse matrix completion or controls")
    control = records["controls"][0]
    if len(control) != 10 or control[0] != metadata["runtime_name"] \
            or int(control[1]) != equations or equations <= 31 \
            or finite(control[2:7]) != list(controls.values()) \
            or int(control[7]) != steps or control[8:] != ["selected_ode", "-1"]:
        raise ValueError("sparse trajectory input/network/method identity mismatch")
    expected = {(method, cells, step) for method in METHODS
                for cells in STORAGE_SIZES for step in range(steps)}
    summaries = {}
    for kind in ("cpu_step", "gpu_step"):
        observed = {}
        for row in records[kind]:
            if len(row) != 6:
                raise ValueError("incomplete step counters")
            key = tuple(int(value) for value in row[:3])
            first, second = map(int, row[3:5])
            seconds, = finite(row[5:])
            if key not in expected or key in observed or first <= 0 or second < 0 or seconds < 0 \
                    or (kind == "cpu_step" and second >= first):
                raise ValueError("invalid/duplicate step counters")
            observed[key] = (first, second, seconds)
        if set(observed) != expected:
            raise ValueError("incomplete ODE/storage/step coverage")
        summaries[kind] = observed
    metrics = {}
    for row in records["metrics"]:
        if len(row) != 8:
            raise ValueError("incomplete method metrics")
        method, attempts, rejects = map(int, row[:3])
        field, limiter, evolution = finite(row[3:6])
        capacity, lane_bytes = map(int, row[6:])
        if method not in METHODS or method in metrics or not 0 <= field <= 2.e-10 \
                or not 0 <= limiter <= 2.e-8 or evolution <= 64 * sys.float_info.epsilon \
                or not 0 < capacity <= min(STORAGE_SIZES) or lane_bytes <= 0:
            raise ValueError("invalid or failed method metrics")
        rows = [value for key, value in summaries["cpu_step"].items() if key[0] == method]
        if attempts != sum(value[0] for value in rows) or rejects != sum(value[1] for value in rows):
            raise ValueError("method totals disagree with observed step counters")
        gpu_rows = [value for key, value in summaries["gpu_step"].items() if key[0] == method]
        metrics[method] = dict(attempts=attempts, rejections=rejects,
            max_field_error=field, max_limiter_error=limiter, max_evolution=evolution,
            pool_capacity=capacity, workspace_bytes_per_lane=lane_bytes,
            cpu_seconds=sum(value[2] for value in rows), gpu_seconds=sum(value[2] for value in gpu_rows),
            kernels=sum(value[0] for value in gpu_rows), synchronizations=sum(value[1] for value in gpu_rows))
    if set(metrics) != set(METHODS):
        raise ValueError("incomplete method metrics")
    states = {}
    for row in records["state"]:
        if len(row) != 2 + 6 + species:
            raise ValueError("final state extent mismatch")
        key = tuple(int(value) for value in row[:2])
        state = finite(row[2:])
        if key not in {(method, cells) for method in METHODS for cells in STORAGE_SIZES} or key in states \
                or state[0] != controls["rho"] or abs(sum(state[6:]) - 1.0) > 2.e-10:
            raise ValueError("invalid/duplicate final state")
        states[key] = state
    if len(states) != len(METHODS) * len(STORAGE_SIZES):
        raise ValueError("incomplete final state coverage")
    return {"methods": metrics, "steps": steps, "storage_sizes": STORAGE_SIZES,
            "field_budget": 2.e-10, "limiter_budget": 2.e-8,
            "timing_scope": "diagnostic per-step wall time; not a controlled speedup/build benchmark"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--network-id", required=True)
    for name in ("rho", "temperature", "interval", "cv", "rtol"):
        parser.add_argument("--" + name, type=float, required=True)
    parser.add_argument("--steps", type=int, required=True)
    parser.add_argument("--composition", nargs="+", required=True, metavar="SPECIES=FRACTION")
    parser.add_argument("--timeout", type=float, default=600.0)
    args = parser.parse_args()
    controls = {name: getattr(args, name) for name in ("rho", "temperature", "interval", "cv", "rtol")}
    if any(not math.isfinite(value) or value <= 0 for value in (*controls.values(), args.timeout)) or args.steps <= 0:
        raise ValueError("physical, tolerance, timeout and step controls must be positive")
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    executable = build / f"arch_cuda_generated_sparse_burn_{args.network_id}"
    def identity():
        return provenance.capture_focused(source_root=ROOT, build_dir=build, artifacts={"trajectory": executable})
    before = identity()
    registered = [record["manifest"] for record in before["build"]["registered_networks"]
                  if record["manifest"]["network_id"] == args.network_id]
    if len(registered) != 1:
        raise ValueError("trajectory network is not uniquely registered by this build")
    require_empty_output_root(output)
    output.mkdir(parents=True, exist_ok=True)
    lane = output / "trajectory"
    lane.mkdir()
    command = [str(executable), *(str(value) for value in controls.values()),
               str(args.steps), *args.composition]
    started = datetime.now(timezone.utc).isoformat()
    process = run_arch_with_logs(command, source_root=ROOT, lane_root=lane, timeout=args.timeout)
    if process.returncode != 0:
        raise RuntimeError(f"sparse matrix failed with code {process.returncode}; logs retained at {lane}")
    transcript = lane / "arch.stdout"
    summary = parse_transcript(transcript.read_text(), registered[0], controls, args.steps)
    provenance.require_unchanged(before, identity())
    evidence = {"schema": 1, "scope": "real-generated-sparse-typed-factory-trajectories",
        "release_qualified": False, "focused_gate_pass": True,
        "qualification_note": "Backend/provider parity with shared physics, not an independent reaction oracle or whole-application qualification.",
        "started_utc": started, "finished_utc": datetime.now(timezone.utc).isoformat(),
        "identity": before, "identity_verified_after_run": True, "command": command,
        "controls": controls, "composition": args.composition, "summary": summary,
        "transcript": provenance.file_identity(transcript)}
    (output / "evidence.json").write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"focused_gate_pass": True, "release_qualified": False,
                      "evidence": str(output / "evidence.json")}, indent=2))


if __name__ == "__main__":
    main()
