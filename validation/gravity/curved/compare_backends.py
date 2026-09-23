#!/usr/bin/env python3
"""Compare P13 CPU/CUDA curved four-module AMR runs at the same physical time.

Usage: compare_backends.py --pair name:cpu-directory:cuda-directory:steps
The existing coupled verifier checks each run first. This comparator then
matches active leaves by (level, Morton key), not by output order, and checks
the same physical fields at the initial and accepted final state.
"""

import argparse
import json
from pathlib import Path

import h5py
import numpy as np

from verify_coupled import verify

# The normalized Linf envelope is 2e-7 for bulk physical fields. Acceleration
# is measured as one native-basis vector against its vector peak: an angular
# component can vanish at a pole even when the physical force is finite.
# Trace isotopes are checked in absolute mass fraction as well.
RELATIVE_BUDGET = {
    "DENS": 2e-7, "ENER": 2e-7,
    "PRES": 2e-7, "TEMP": 2e-7,
    "VELX": 2e-7, "VELY": 2e-7, "VELZ": 2e-7,
    "GPOT": 2e-7,
    "ENUC": 1e-5,
}
ACCELERATION_RELATIVE_BUDGET = 2e-7
ACCELERATION_FIELDS = ("GACX", "GACY", "GACZ")
SPECIES_ABSOLUTE_BUDGET = 1e-9


def ordered_fields(path):
    """Read all active plot fields in a canonical AMR leaf order."""
    with h5py.File(path) as plot:
        keys = list(zip(plot["Grid/level"][()].tolist(),
                        plot["Grid/morton"][()].tolist()))
        if len(set(keys)) != len(keys):
            raise ValueError(f"{path}: duplicate AMR leaf key")
        order = np.array(sorted(range(len(keys)), key=keys.__getitem__))
        fields = {name: np.asarray(dataset[()])[order]
                  for name, dataset in plot["Data"].items()}
        return {
            "time": float(plot.attrs["time"]),
            "dimension": int(plot.attrs["dim"]),
            "leaf_keys": [keys[index] for index in order],
            "fields": fields,
        }


def compare_plot(cpu_path, cuda_path, label):
    """Check topology, physical time, finite fields, and fixed parity budgets."""
    cpu = ordered_fields(cpu_path)
    gpu = ordered_fields(cuda_path)
    if cpu["dimension"] != gpu["dimension"] or cpu["leaf_keys"] != gpu["leaf_keys"]:
        raise ValueError(f"{label}: CPU/CUDA AMR topology differs")
    scale = max(abs(cpu["time"]), abs(gpu["time"]), 1e-30)
    if abs(cpu["time"] - gpu["time"]) > max(1e-20, 2e-10 * scale):
        raise ValueError(f"{label}: CPU/CUDA physical time differs")
    if cpu["fields"].keys() != gpu["fields"].keys():
        raise ValueError(f"{label}: CPU/CUDA plot fields differ")
    errors = {}
    for name, expected in cpu["fields"].items():
        actual = gpu["fields"][name]
        if expected.shape != actual.shape or not np.all(np.isfinite(actual)):
            raise ValueError(f"{label}: {name} shape or finiteness differs")
        maximum = float(np.max(np.abs(expected - actual)))
        if name in ACCELERATION_FIELDS:
            errors[name] = {"absolute_linf": maximum}
        elif name in RELATIVE_BUDGET:
            amplitude = max(float(np.max(np.abs(expected))),
                            float(np.max(np.abs(actual))), 1e-100)
            error = maximum / amplitude
            if error > RELATIVE_BUDGET[name]:
                raise ValueError(
                    f"{label}: {name} normalized Linf {error:.4g} "
                    f"exceeds {RELATIVE_BUDGET[name]:.4g}")
            errors[name] = {"normalized_linf": error, "absolute_linf": maximum}
        else:
            if maximum > SPECIES_ABSOLUTE_BUDGET:
                raise ValueError(
                    f"{label}: {name} fraction Linf {maximum:.4g} "
                    f"exceeds {SPECIES_ABSOLUTE_BUDGET:.4g}")
            errors[name] = {"absolute_linf": maximum}
    acceleration = [name for name in ACCELERATION_FIELDS
                    if name in cpu["fields"]]
    if not acceleration:
        raise ValueError(f"{label}: no gravity acceleration field")
    expected_norm2 = sum(np.square(cpu["fields"][name])
                         for name in acceleration)
    actual_norm2 = sum(np.square(gpu["fields"][name])
                       for name in acceleration)
    difference_norm2 = sum(np.square(cpu["fields"][name]
                                     - gpu["fields"][name])
                           for name in acceleration)
    peak = max(float(np.sqrt(np.max(expected_norm2))),
               float(np.sqrt(np.max(actual_norm2))), 1e-100)
    maximum = float(np.sqrt(np.max(difference_norm2)))
    relative = maximum / peak
    if relative > ACCELERATION_RELATIVE_BUDGET:
        raise ValueError(
            f"{label}: acceleration-vector normalized Linf {relative:.4g} "
            f"exceeds {ACCELERATION_RELATIVE_BUDGET:.4g}")
    errors["GAC_VECTOR"] = {"normalized_linf": relative,
                            "absolute_linf": maximum}
    return {"time_seconds": cpu["time"], "leaves": len(cpu["leaf_keys"]),
            "errors": errors}


def compare_pair(label, cpu_dir, cuda_dir, steps, expect_mixed=True):
    """Require completed four-module runs, then compare both plot epochs."""
    cpu = verify(label + "-cpu", cpu_dir, steps, expect_mixed)
    cuda = verify(label + "-cuda", cuda_dir, steps, expect_mixed)
    plan_files = list(cuda_dir.glob("*_backend_plan.txt"))
    if len(plan_files) != 1 or "resolved=cuda\n" not in plan_files[0].read_text():
        raise ValueError(f"{label}: CUDA run did not resolve to the device")
    cpu_plots = sorted(cpu_dir.glob("*_plt_*.h5"))
    cuda_plots = sorted(cuda_dir.glob("*_plt_*.h5"))
    return {
        "cpu": cpu, "cuda": cuda,
        "initial": compare_plot(cpu_plots[0], cuda_plots[0], label + "-initial"),
        "final": compare_plot(cpu_plots[1], cuda_plots[1], label + "-final"),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pair", action="append", required=True,
                        help="label:cpu-output-directory:cuda-output-directory:steps")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    results = {}
    for specification in args.pair:
        label, cpu_path, cuda_path, step_text = specification.split(":", 3)
        results[label] = compare_pair(
            label, Path(cpu_path), Path(cuda_path), int(step_text))
    result = json.dumps(results, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(result)
    else:
        print(result, end="")


if __name__ == "__main__":
    main()
