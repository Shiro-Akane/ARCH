#!/usr/bin/env python3
"""Run reproducible CPU/CUDA curved four-module pairs with one Release binary.

Usage: run_cuda_matrix.py --arch build-ci/cuda-focused/bin/ARCH \
    --pair polar:validation/gravity/curved/inputs/p12_polar_origin.par:3 \
    --pair regular:validation/gravity/curved/inputs/p13_polar_origin_4x4_regular.par:3:regular \
    --output /tmp/arch-p13-matrix --cpu-threads 16 --repeats 3

Each run has its own input and output directory. The existing coupled verifier
and compare_backends.py enforce the physical and parity budgets. The optional
regular mode requires a root-only grid; the default requires actual mixed AMR.
"""

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import time

from compare_backends import compare_pair

ROOT = Path(__file__).resolve().parents[3]


def changed_input(source, backend, output, steps):
    """Override only execution identity, step limit, and output ownership."""
    changes = {
        "compute_backend": backend, "out_dir": str(output),
        "max_steps": str(steps),
    }
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


def one_run(executable, source, destination, backend, steps, threads):
    """Run one immutable input and retain the full log on failure."""
    destination.mkdir(parents=True, exist_ok=False)
    input_path = destination / "input.par"
    input_path.write_text(changed_input(source, backend, destination, steps))
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
    return {
        "elapsed_seconds": elapsed,
        "driver_seconds": driver_seconds(destination),
        "gravity": gravity_times(destination),
        "regrid": regrid_metrics(destination),
        "threads": threads,
        "directory": str(destination),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", type=Path, required=True)
    parser.add_argument("--pair", action="append", required=True,
                        help="label:input.par:accepted-steps[:amr|regular]")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cpu-threads", type=int, default=8)
    parser.add_argument("--repeats", type=int, default=1)
    args = parser.parse_args()
    if args.cpu_threads < 1 or args.repeats < 1:
        parser.error("threads and repeats must be positive")
    executable = args.arch.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = {
        "executable": str(executable),
        "sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        "pairs": {},
        "speedups": {},
    }
    for specification in args.pair:
        parts = specification.split(":")
        if len(parts) not in (3, 4):
            parser.error("pair must be label:input.par:steps[:amr|regular]")
        label, input_name, count = parts[:3]
        mode = parts[3] if len(parts) == 4 else "amr"
        if mode not in ("amr", "regular"):
            parser.error("pair mode must be amr or regular")
        source = Path(input_name).resolve()
        steps = int(count)
        if not source.is_file() or steps < 1:
            raise ValueError(f"{label}: invalid input or step limit")
        trials = []
        for repeat in range(args.repeats):
            cpu_dir = output / label / f"repeat-{repeat}" / "cpu"
            gpu_dir = output / label / f"repeat-{repeat}" / "cuda"
            cpu = one_run(executable, source, cpu_dir, "cpu", steps,
                          args.cpu_threads)
            cuda = one_run(executable, source, gpu_dir, "cuda", steps, 1)
            parity = compare_pair(label, cpu_dir, gpu_dir, steps,
                                  expect_mixed=(mode == "amr"))
            trials.append({"cpu": cpu, "cuda": cuda, "parity": parity})
            report["pairs"][label] = trials
            (output / "summary.json").write_text(
                json.dumps(report, indent=2, sort_keys=True) + "\n")
            print(label, repeat, "driver speedup",
                  cpu["driver_seconds"] / cuda["driver_seconds"], flush=True)
        report["speedups"][label] = {
            "mode": mode,
            "cpu_threads": args.cpu_threads,
            "driver": (statistics.median(run["cpu"]["driver_seconds"]
                                         for run in trials)
                       / statistics.median(run["cuda"]["driver_seconds"]
                                           for run in trials)),
            "end_to_end": (statistics.median(run["cpu"]["elapsed_seconds"]
                                             for run in trials)
                           / statistics.median(run["cuda"]["elapsed_seconds"]
                                               for run in trials)),
        }
    (output / "summary.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
