"""Audit existing CPU four-module AMR runs without launching simulations.

Usage: verify_coupled.py --run label:directory:expected_steps [--run ...]
       verify_coupled.py --endpoint-run label:directory:physical_time [--endpoint-run ...]
At least one run of either form is required. The check reads the actual Driver
outputs, not a private case implementation.
"""

import argparse
import csv
import json
import math
import re
from pathlib import Path

import h5py
import numpy as np


def leaf_levels(plot):
    """Return the distribution of active AMR levels in one plot."""
    with h5py.File(plot) as handle:
        levels = handle["Grid/level"][()]
        counts = {str(int(level)): int(count) for level, count in
                  zip(*np.unique(levels, return_counts=True))}
        time = float(handle.attrs["time"])
        fields = {}
        for name in ("DENS", "PRES", "TEMP", "ENER", "ENUC", "GPOT",
                     "GACX", "GACY", "c12", "o16"):
            values = handle[f"Data/{name}"][()]
            if not np.all(np.isfinite(values)):
                raise ValueError(f"Nonfinite {name} in {plot}")
            fields[name] = [float(values.min()), float(values.max())]
        if int(handle.attrs["dim"]) == 3:
            values = handle["Data/GACZ"][()]
            if not np.all(np.isfinite(values)):
                raise ValueError(f"Nonfinite GACZ in {plot}")
            fields["GACZ"] = [float(values.min()), float(values.max())]
    if fields["DENS"][0] <= 0 or fields["TEMP"][0] <= 0:
        raise ValueError(f"Nonpositive physical state in {plot}")
    return {"time_seconds": time, "leaves_by_level": counts,
            "field_min_max": fields}


def physical_times_agree(left, right):
    """Retain the existing coupled endpoint budget, rejecting invalid times."""
    if not all(math.isfinite(value) for value in (left, right)):
        return False
    scale = max(abs(left), abs(right), 1e-30)
    return abs(left - right) <= max(1e-20, 2e-10 * scale)


def verify(label, directory, expected_steps, expect_mixed=True, *, expected_time=None):
    """Require completed four-module stepping and the requested AMR topology."""
    if expected_time is not None:
        if not math.isfinite(expected_time) or expected_time <= 0 or expected_steps is not None:
            raise ValueError(f"{label}: endpoint mode requires positive finite time and no step quota")
    elif expected_steps is None or expected_steps < 1:
        raise ValueError(f"{label}: positive expected steps required")
    plots = sorted(directory.glob("*_plt_*.h5"))
    if expected_time is None:
        if len(plots) != 2:
            raise ValueError(f"{label}: expected initial and final plots")
        initial, final = [leaf_levels(path) for path in plots]
        samples = None
    else:
        if len(plots) < 2:
            raise ValueError(f"{label}: expected at least initial and final plots")
        # Physical ordering comes from the stored HDF5 time, never filenames.
        states = [leaf_levels(path) for path in plots]
        times = [state["time_seconds"] for state in states]
        if not all(math.isfinite(value) for value in times):
            raise ValueError(f"{label}: nonfinite plot time")
        states.sort(key=lambda state: state["time_seconds"])
        ordered = [state["time_seconds"] for state in states]
        if any(later <= earlier for earlier, later in zip(ordered, ordered[1:])):
            raise ValueError(f"{label}: plot times are not unique and strictly increasing")
        initial, final = states[0], states[-1]
        samples = [{"time_seconds": state["time_seconds"],
                    "leaves_by_level": state["leaves_by_level"],
                    "field_min_max": state["field_min_max"]}
                   for state in states[1:-1]]
    if expect_mixed:
        if not ("0" in initial["leaves_by_level"] and
                "1" in initial["leaves_by_level"] and
                "0" in final["leaves_by_level"] and
                "1" in final["leaves_by_level"]):
            raise ValueError(f"{label}: not a coarse/fine mixed AMR run")
    elif (set(initial["leaves_by_level"]) != {"0"}
          or set(final["leaves_by_level"]) != {"0"}):
        raise ValueError(f"{label}: expected a regular root grid")
    if expected_time is not None and not physical_times_agree(final["time_seconds"], expected_time):
        raise ValueError(f"{label}: requested physical endpoint was not reached")
    if not final["time_seconds"] > initial["time_seconds"]:
        raise ValueError(f"{label}: time did not advance")
    if final["field_min_max"]["ENUC"][1] <= 0:
        raise ValueError(f"{label}: nuclear energy rate not active")
    repairs = dict(line.split("=", 1) for line in
                   (directory / "state_repairs.txt").read_text().splitlines()
                   if "=" in line)
    if int(repairs["events"]) != 0:
        raise ValueError(f"{label}: state repairs occurred")
    logs = list(directory.glob("*_log.dat"))
    if len(logs) != 1:
        raise ValueError(f"{label}: expected one Driver log")
    log = logs[0].read_text()
    completion = re.search(r"Simulation Done\. Total Steps: (\d+)", log)
    if completion is None or (expected_steps is not None and int(completion.group(1)) != expected_steps):
        raise ValueError(f"{label}: wrong accepted step count")
    accepted_steps = int(completion.group(1))
    if accepted_steps < 1:
        raise ValueError(f"{label}: no accepted evolution steps")
    step_lines = [line for line in log.splitlines()
                  if re.match(r"^\s*\d+\s+\S+", line)]
    if len(step_lines) != accepted_steps:
        raise ValueError(f"{label}: step rows missing")
    diffusion_dt = [float(line.split()[5]) for line in step_lines]
    if not all(math.isfinite(value) and value > 0 for value in diffusion_dt):
        raise ValueError(f"{label}: invalid diffusion timestep evidence")
    with (directory / "gravity_solves.tsv").open() as stream:
        solves = list(csv.DictReader(stream, delimiter="\t"))
    if not solves:
        raise ValueError(f"{label}: no gravity solves")
    ratios = []
    for row in solves:
        residual, target = float(row["residual"]), float(row["target"])
        if not all(math.isfinite(value) for value in (residual, target)) or target <= 0:
            raise ValueError(f"{label}: invalid gravity residual")
        ratio = residual / target
        if ratio > 1:
            raise ValueError(f"{label}: gravity solve missed target")
        ratios.append(ratio)
    with next(iter(directory.glob("*_regrid.tsv"))).open() as stream:
        regrids = list(csv.DictReader(stream, delimiter="\t"))
    if expect_mixed and not any(row["topology_changed"] == "1" for row in regrids):
        raise ValueError(f"{label}: no actual AMR refinement")
    if not expect_mixed and any(row["topology_changed"] == "1" for row in regrids):
        raise ValueError(f"{label}: regular grid changed topology")
    result = {"directory": str(directory), "steps": accepted_steps,
              "requested_endpoint": expected_time,
              "initial": initial, "final": final, "state_repairs": 0,
              "gravity_solves": len(solves),
              "maximum_residual_over_target": max(ratios),
              "maximum_iterations": max(int(row["iterations"]) for row in solves),
              "minimum_diffusion_dt_seconds": min(diffusion_dt),
              "topology_changes": sum(row["topology_changed"] == "1" for row in regrids)}
    if expected_time is not None:
        result["samples"] = samples
    return result


def split_specification(specification, parameter, parser):
    """Split label:directory:parameter, reporting malformed command lines."""
    parts = specification.split(":", 2)
    if len(parts) != 3:
        parser.error(f"expected label:directory:{parameter} in {specification!r}")
    return parts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", action="append", default=[],
                        help="label:output-directory:expected-steps")
    parser.add_argument("--endpoint-run", action="append", default=[],
                        help="label:output-directory:physical-time-seconds")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not args.run and not args.endpoint_run:
        parser.error("at least one --run or --endpoint-run is required")
    jobs = []
    labels = set()
    for specification in args.run:
        label, path, steps = split_specification(specification, "expected-steps", parser)
        if label in labels:
            parser.error(f"duplicate run label {label!r}")
        labels.add(label)
        try:
            jobs.append((label, Path(path), int(steps), None))
        except ValueError:
            parser.error(f"expected an integer step count in {specification!r}")
    for specification in args.endpoint_run:
        label, path, endpoint = split_specification(specification,
                                                    "physical-time-seconds", parser)
        if label in labels:
            parser.error(f"duplicate run label {label!r}")
        labels.add(label)
        try:
            jobs.append((label, Path(path), None, float(endpoint)))
        except ValueError:
            parser.error(f"expected a numeric physical time in {specification!r}")
    results = {}
    for label, path, steps, endpoint in jobs:
        results[label] = verify(label, path, steps, expected_time=endpoint)
    report = json.dumps(results, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(report)
    else:
        print(report, end="")


if __name__ == "__main__":
    main()
