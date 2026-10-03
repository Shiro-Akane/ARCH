#!/usr/bin/env python3
"""Run reproducible CPU/CUDA curved four-module pairs with one Release binary.

Usage: run_cuda_matrix.py --arch build-ci/cuda-focused/bin/ARCH \
    --pair polar:validation/gravity/curved/inputs/p12_polar_origin.par:3 \
    --pair regular:validation/gravity/curved/inputs/p13_polar_origin_4x4_regular.par:3:regular \
    --output /tmp/arch-p13-matrix --cpu-threads 16 --repeats 3

Endpoint mode: replace --pair with --endpoint-pair label:input.par:T_END[:amr|regular].
T_END must come from the owner's frozen plan; max_steps is disabled.
Endpoint completion is verified for each run before launching the other backend.
Endpoint mode requires --cpu-threads, --cuda-host-threads and --repeats >=3.
It warms each backend once and alternates measured pair order. Thread screening,
CPU-only baselines, manifests and frozen scientific inputs are still required.

Each run has its own input and output directory. The existing coupled verifier
and compare_backends.py enforce the physical and parity budgets. The optional
regular mode requires a root-only grid; the default requires actual mixed AMR.
"""

import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import time

from compare_backends import compare_pair
from verify_coupled import verify

ROOT = Path(__file__).resolve().parents[3]


def changed_input(source, backend, output, steps, *, end_time=None):
    """Override execution identity and the explicit stopping mode only."""
    if end_time is not None:
        if not math.isfinite(end_time) or end_time <= 0 or steps is not None:
            raise ValueError("endpoint mode requires finite positive time and no step quota")
    elif steps is None or steps < 1:
        raise ValueError("positive step quota required")
    changes = {
        "compute_backend": backend, "out_dir": str(output),
        "max_steps": "-1" if end_time is not None else str(steps),
    }
    if end_time is not None:
        changes["tmax"] = repr(end_time)
    result = []
    seen = set()
    for line in source.read_text().splitlines():
        if "=" in line and not line.lstrip().startswith("#"):
            key, _ = line.split("=", 1)
            key = key.strip()
            if key in changes:
                result.append(f"{key}={changes[key]}")
                seen.add(key)
                continue
        result.append(line)
    for key, value in changes.items():
        if key not in seen:
            result.append(f"{key}={value}")
    return "\n".join(result) + "\n"


def driver_seconds(directory):
    """Read the Driver's measured evolution span, excluding output time."""
    with (directory / "run_timings.tsv").open() as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    if len(rows) != 1:
        raise ValueError(f"{directory}: expected one timing row")
    return float(rows[0]["driver_seconds"])


def gravity_times(directory):
    """Summarize existing solve timings, iterations, launches and bus traffic."""
    with (directory / "gravity_solves.tsv").open() as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    result = {
        key: sum(float(row[key]) for row in rows)
        for key in ("setup_seconds", "source_boundary_seconds",
                    "poisson_seconds", "force_seconds", "solve_seconds")
    }
    result.update({key: sum(int(row[key]) for row in rows)
                   for key in ("kernels", "bytes_h2d", "bytes_d2h",
                               "synchronizations")})
    result["maximum_iterations"] = max(int(row["iterations"]) for row in rows)
    result["solve_count"] = len(rows)
    return result


def regrid_metrics(directory):
    """Report AMR transactions separately; their time overlaps Driver time."""
    paths = list(directory.glob("*_regrid.tsv"))
    if len(paths) != 1:
        raise ValueError(f"{directory}: expected one regrid trace")
    with paths[0].open() as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    result = {"wall_seconds": sum(float(row["wall_seconds"]) for row in rows)}
    result.update({key: sum(int(row[key]) for row in rows)
                   for key in ("bytes_h2d", "bytes_d2h", "kernel_count",
                               "stream_sync_count")})
    result["topology_changes"] = sum(row["topology_changed"] == "1"
                                     for row in rows)
    return result


def one_run(executable, source, destination, backend, steps, threads, *, end_time=None, expect_mixed=True):
    """Run one immutable input and retain the full log on failure."""
    destination.mkdir(parents=True, exist_ok=False)
    input_path = destination / "input.par"
    input_path.write_text(changed_input(source, backend, destination, steps, end_time=end_time))
    env = os.environ.copy()
    env["OMP_NUM_THREADS"] = str(threads)
    started = time.monotonic()
    completed = subprocess.run(
        [str(executable), "SNIaCoupled", str(input_path)],
        cwd=ROOT, env=env, capture_output=True, text=True)
    elapsed = time.monotonic() - started
    (destination / "run.log").write_text(completed.stdout + completed.stderr)
    if completed.returncode != 0:
        raise RuntimeError(
            f"{destination}: ARCH returned {completed.returncode}\n"
            + "\n".join((completed.stdout + completed.stderr).splitlines()[-35:]))
    plan = list(destination.glob("*_backend_plan.txt"))
    if len(plan) != 1 or f"resolved={backend}\n" not in plan[0].read_text():
        raise RuntimeError(f"{destination}: requested backend was not used")
    endpoint = (verify(destination.name, destination, None, expect_mixed,
                       expected_time=end_time) if end_time is not None else None)
    return {
        "endpoint_verification": endpoint,
        "requested_endpoint": end_time,
        "input_sha256": hashlib.sha256(input_path.read_bytes()).hexdigest(),
        "elapsed_seconds": elapsed,
        "driver_seconds": driver_seconds(destination),
        "gravity": gravity_times(destination),
        "regrid": regrid_metrics(destination),
        "threads": threads,
        "directory": str(destination),
    }


def paired_trials(executable, source, destination, steps, cpu_threads, cuda_threads,
                  repeats, *, end_time=None, expect_mixed=True, notify=lambda: None,
                  state=None):
    """Warm each backend once, then alternate measured pairs without dropping runs."""
    if repeats < 1 or cpu_threads < 1 or cuda_threads < 1:
        raise ValueError("positive repeats and explicit backend thread budgets required")
    if end_time is not None and repeats < 3:
        raise ValueError("physical endpoint measurements require at least three pairs")
    state = state if state is not None else {}
    state.update(warmup=None, trials=[], attempts=[], status="running")

    def pair(phase, repeat, order):
        record = {"phase": phase, "repeat": repeat, "order": list(order),
                  "runs": {}, "status": "running"}
        state["attempts"].append(record)
        notify()
        folder = destination / ("warmup" if phase == "warmup" else f"repeat-{repeat}")
        try:
            for backend in order:
                record["active_backend"] = backend
                notify()
                record["runs"][backend] = one_run(
                    executable, source, folder/backend, backend, steps,
                    cpu_threads if backend == "cpu" else cuda_threads,
                    end_time=end_time, expect_mixed=expect_mixed)
                notify()
            record["parity"] = compare_pair(
                destination.name, folder/"cpu", folder/"cuda", steps,
                expect_mixed=expect_mixed, expected_time=end_time)
            record.pop("active_backend", None)
            record["status"] = "passed"
            notify()
            return {"cpu": record["runs"]["cpu"], "cuda": record["runs"]["cuda"],
                    "parity": record["parity"], "order": record["order"]}
        except Exception as error:
            record["status"] = state["status"] = "failed"
            record["error"] = str(error)
            notify()
            raise

    if end_time is not None:
        state["warmup"] = pair("warmup", None, ("cpu", "cuda"))
        notify()
    for repeat in range(repeats):
        order = ("cpu", "cuda") if repeat % 2 == 0 else ("cuda", "cpu")
        state["trials"].append(pair("measurement", repeat, order))
        notify()
    state["status"] = "passed"
    notify()
    return state


def timing_summary(trials):
    """Keep all measured samples; warmup is stored separately."""
    result = {}
    for backend in ("cpu", "cuda"):
        result[backend] = {}
        for metric in ("elapsed_seconds", "driver_seconds"):
            values = [trial[backend][metric] for trial in trials]
            if not values or any(not math.isfinite(value) or value <= 0 for value in values):
                raise ValueError("nonpositive or invalid timing sample")
            result[backend][metric] = {"samples": values, "count": len(values),
                                      "median": statistics.median(values),
                                      "minimum": min(values), "maximum": max(values)}
    result["end_to_end_speedup"] = (result["cpu"]["elapsed_seconds"]["median"] /
                                    result["cuda"]["elapsed_seconds"]["median"])
    result["driver_speedup"] = (result["cpu"]["driver_seconds"]["median"] /
                               result["cuda"]["driver_seconds"]["median"])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", type=Path, required=True)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--pair", action="append",
                        help="label:input.par:accepted-steps[:amr|regular]")
    modes.add_argument("--endpoint-pair", action="append",
                       help="label:input.par:owner-frozen-t-end[:amr|regular]; max_steps=-1")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cpu-threads", type=int, help="explicit screened CPU thread budget; legacy default 8")
    parser.add_argument("--cuda-host-threads", type=int, help="explicit CUDA Host thread budget; legacy default 1")
    parser.add_argument("--repeats", type=int, default=1)
    args = parser.parse_args()
    if args.endpoint_pair and (args.cpu_threads is None or args.cuda_host_threads is None or args.repeats < 3):
        parser.error("endpoint mode requires explicit CPU/CUDA Host threads and at least three repeats")
    args.cpu_threads = 8 if args.cpu_threads is None else args.cpu_threads
    args.cuda_host_threads = 1 if args.cuda_host_threads is None else args.cuda_host_threads
    if min(args.cpu_threads, args.cuda_host_threads, args.repeats) < 1:
        parser.error("threads and repeats must be positive")
    executable = args.arch.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = {
        "executable": str(executable),
        "sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        "pairs": {},
        "schedules": {},
        "speedups": {},
        "qualified_benchmark": False,
        "pending": ["owner-frozen inputs/budgets", "thread/affinity screening",
                    "CPU-only baseline", "hardware/build/effective-input manifest",
                    "resource sampling"],
    }
    for specification in args.pair or args.endpoint_pair:
        parts = specification.split(":")
        if len(parts) not in (3, 4):
            parser.error("pair must be label:input.par:steps-or-t-end[:amr|regular]")
        label, input_name, count = parts[:3]
        mode = parts[3] if len(parts) == 4 else "amr"
        if mode not in ("amr", "regular"):
            parser.error("pair mode must be amr or regular")
        source = Path(input_name).resolve()
        try:
            end_time = float(count) if args.endpoint_pair else None
            steps = None if args.endpoint_pair else int(count)
        except ValueError:
            parser.error("invalid step quota or physical endpoint")
        if not source.is_file() or (steps is not None and steps < 1) or (
                end_time is not None and (not math.isfinite(end_time) or end_time <= 0)):
            raise ValueError(f"{label}: invalid input or stopping condition")
        schedule = {}
        report["schedules"][label] = schedule
        def publish():
            (output / "summary.json").write_text(
                json.dumps(report, indent=2, sort_keys=True) + "\n")
        paired_trials(executable, source, output/label, steps, args.cpu_threads,
                      args.cuda_host_threads, args.repeats, end_time=end_time,
                      expect_mixed=(mode == "amr"), notify=publish, state=schedule)
        report["pairs"][label] = schedule["trials"]
        report["speedups"][label] = timing_summary(schedule["trials"])
        publish()
    (output / "summary.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
