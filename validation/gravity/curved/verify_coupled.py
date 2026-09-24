"""Audit existing P11 CPU four-module AMR runs without launching simulations.

Usage: verify_coupled.py --run label:directory:expected_steps [--run ...]
The check reads the actual Driver outputs, not a private case implementation.
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


def verify(label, directory, expected_steps, expect_mixed=True):
    """Require completed four-module stepping and the requested AMR topology."""
    plots = sorted(directory.glob("*_plt_*.h5"))
    if len(plots) != 2:
        raise ValueError(f"{label}: expected initial and final plots")
    initial, final = [leaf_levels(path) for path in plots]
    if expect_mixed:
        if not ("0" in initial["leaves_by_level"] and
                "1" in initial["leaves_by_level"] and
                "0" in final["leaves_by_level"] and
                "1" in final["leaves_by_level"]):
            raise ValueError(f"{label}: not a coarse/fine mixed AMR run")
    elif (set(initial["leaves_by_level"]) != {"0"}
          or set(final["leaves_by_level"]) != {"0"}):
        raise ValueError(f"{label}: expected a regular root grid")
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
    if completion is None or int(completion.group(1)) != expected_steps:
        raise ValueError(f"{label}: wrong accepted step count")
    step_lines = [line for line in log.splitlines()
                  if re.match(r"^\s*\d+\s+\S+", line)]
    if len(step_lines) != expected_steps:
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
    return {"directory": str(directory), "steps": expected_steps,
            "initial": initial, "final": final, "state_repairs": 0,
            "gravity_solves": len(solves),
            "maximum_residual_over_target": max(ratios),
            "maximum_iterations": max(int(row["iterations"]) for row in solves),
            "minimum_diffusion_dt_seconds": min(diffusion_dt),
            "topology_changes": sum(row["topology_changed"] == "1" for row in regrids)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", action="append", required=True,
                        help="label:output-directory:expected-steps")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    results = {}
    for specification in args.run:
        label, path, steps = specification.split(":", 2)
        results[label] = verify(label, Path(path), int(steps))
    report = json.dumps(results, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(report)
    else:
        print(report, end="")


if __name__ == "__main__":
    main()
