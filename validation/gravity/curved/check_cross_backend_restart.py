#!/usr/bin/env python3
"""Check curved four-module AMR continuation across CPU and CUDA.

The same Release executable runs uninterrupted 3-step references, 2-step
checkpoint producers, and opposite-backend continuations to step 3. Compare
the physical final plot by AMR leaf identity and the fixed field budgets.

Usage: check_cross_backend_restart.py --arch build-cuda/bin/ARCH \
    --source validation/gravity/curved/inputs/p12_polar_origin.par \
    --output /tmp/arch-curved-cross-restart
"""

import argparse
import csv
import hashlib
import json
from pathlib import Path

from compare_backends import compare_plot
from run_cuda_matrix import one_run


def continued_source(source, checkpoint, destination):
    """Change only restart identity; one_run supplies device/output/step limit."""
    text = source.read_text()
    if "restart=false" not in text:
        raise ValueError("expected an uninterrupted source input")
    destination.write_text(text.replace(
        "restart=false", f"restart=true\nrestart_file={checkpoint}", 1))
    return destination


def checked_continuation(directory, steps):
    """Require the resumed solve to finish on the requested backend/state."""
    log_files = list(directory.glob("*_log.dat"))
    if len(log_files) != 1 or f"Simulation Done. Total Steps: {steps}" not in log_files[0].read_text():
        raise ValueError(f"{directory}: continuation did not reach step {steps}")
    repairs = dict(line.split("=", 1) for line in
                   (directory / "state_repairs.txt").read_text().splitlines()
                   if "=" in line)
    if int(repairs["events"]) != 0:
        raise ValueError(f"{directory}: state repair in continuation")
    with (directory / "gravity_solves.tsv").open() as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    if not rows or any(float(row["residual"]) > float(row["target"])
                       for row in rows):
        raise ValueError(f"{directory}: unpublished or nonconverged gravity")
    plots = sorted(directory.glob("*_plt_*.h5"))
    if not plots:
        raise ValueError(f"{directory}: continuation has no plot")
    return plots[-1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--steps", type=int, default=3)
    parser.add_argument("--split", type=int, default=2)
    parser.add_argument("--cpu-threads", type=int, default=16)
    args = parser.parse_args()
    if not 0 < args.split < args.steps or args.cpu_threads < 1:
        parser.error("require 0 < split < steps and positive CPU threads")
    executable = args.arch.resolve()
    source = args.source.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    runs = {}
    for backend in ("cpu", "cuda"):
        threads = args.cpu_threads if backend == "cpu" else 1
        for phase, steps in (("reference", args.steps), ("seed", args.split)):
            directory = output / f"{backend}-{phase}"
            runs[f"{backend}-{phase}"] = one_run(
                executable, source, directory, backend, steps, threads)
    comparisons = {}
    for source_backend, destination_backend in (("cpu", "cuda"),
                                                ("cuda", "cpu")):
        seed = output / f"{source_backend}-seed"
        checkpoints = sorted(seed.glob("*_chk_*.h5"))
        if len(checkpoints) < 2:
            raise ValueError(f"{seed}: missing final checkpoint")
        checkpoint = checkpoints[-1].resolve()
        overlay = continued_source(
            source, checkpoint, output / f"{source_backend}-to-{destination_backend}.par")
        directory = output / f"{source_backend}-to-{destination_backend}"
        runs[f"{source_backend}-to-{destination_backend}"] = one_run(
            executable, overlay, directory, destination_backend, args.steps,
            args.cpu_threads if destination_backend == "cpu" else 1)
        actual = checked_continuation(directory, args.steps)
        reference = sorted((output / f"{destination_backend}-reference")
                           .glob("*_plt_*.h5"))[-1]
        comparisons[f"{source_backend}-to-{destination_backend}"] = compare_plot(
            reference, actual, f"{source_backend}-to-{destination_backend}")
    report = {"executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
              "source": str(source), "split": args.split, "steps": args.steps,
              "runs": runs, "comparisons": comparisons}
    (output / "summary.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    for name, result in comparisons.items():
        print(name, "leaves", result["leaves"], "acceleration error",
              result["errors"]["GAC_VECTOR"]["normalized_linf"], flush=True)


if __name__ == "__main__":
    main()
