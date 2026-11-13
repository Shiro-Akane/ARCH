#!/usr/bin/env python3
"""Fixed Cartesian MPI slabs against independently built serial execution.

The original CPU route remains bitwise. Optional CUDA lanes compare bitwise
against MPI OFF CUDA and use the existing native 2e-13 gate against CPU.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import time
import numpy as np

from test_mpi_fixed_grid import parameters, run, snapshots, compare, verify_backend_identity


def compare_backends(reference, candidate):
    assert len(reference) == len(candidate), "output schedule differs"
    worst = 0.
    for (time_a, fields_a), (time_b, fields_b) in zip(reference, candidate):
        assert np.float64(time_a).tobytes() == np.float64(time_b).tobytes(), "physical clock differs"
        assert set(fields_a) == set(fields_b), "field set differs"
        for name, a in fields_a.items():
            b = fields_b[name]
            assert a.shape == b.shape and np.all(np.isfinite(a)) and np.all(np.isfinite(b))
            if name.startswith("coordinate") or name == "cell_measure":
                assert a.tobytes() == b.tobytes(), "grid changed: " + name
            else:
                error = np.abs(a-b) / np.maximum(1., np.maximum(np.abs(a), np.abs(b)))
                worst = max(worst, float(np.max(error, initial=0.)))
                assert np.all(error <= 2.e-13), (name, worst)
    return {"snapshots": len(reference), "fields": sorted(reference[0][1]),
            "maximum_scaled_error": worst, "gate": "2e-13 * max(1, abs(a), abs(b))",
            "final_time": reference[-1][0], "cells": len(reference[-1][1]["coordinate"])}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--serial-candidate", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--mpiexec", required=True)
    parser.add_argument("--backend", choices=("cpu", "cuda"), default="cpu")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    args.evidence.mkdir(parents=True, exist_ok=False)
    report = {
        "state": "running", "cases": [], "rejections": [],
        "binaries": {name: {"path": str(binary), "sha256": hashlib.sha256(binary.read_bytes()).hexdigest()}
                     for name, binary in (("baseline", args.baseline), ("mpi", args.candidate),
                                          ("serial", args.serial_candidate))},
        "scope": f"single-host {args.backend} Cartesian fixed grid; correctness, not scaling",
        "concurrent_candidate_lanes": 1 if args.backend == "cuda" else 4,
        "mpiexec_arguments": ["--bind-to", "none"],
    }

    def save():
        (args.evidence / "summary.json").write_text(json.dumps(report, indent=2) + "\n")

    # Unequal X slabs, multiple transverse blocks, and only one spare pool slot.
    # Sedov varies in all active axes. Axis-selected waves exercise X/Y/Z transport.
    cases = [
        ("Sedov", 2, "RK3", "HLLC", "ppm", 0, "outflow"),
        ("Sedov", 3, "RK3", "HLLC", "ppm", 0, "outflow"),
        ("Sedov", 2, "RK2", "HLLC", "muscl", 0, "reflecting"),
        ("SmoothAdvection", 2, "RK2", "HLLC", "muscl", 1, "periodic"),
        ("SmoothAdvection", 3, "RK3", "HLLC", "muscl", 1, "periodic"),
        ("SmoothAdvection", 2, "RK3", "Roe", "muscl", 2, "periodic"),
        ("SmoothAdvection", 3, "RK3", "HLL", "muscl", 3, "periodic"),
    ]
    if args.backend == "cuda":
        cases.insert(0, ("Sod", 1, "RK3", "HLLC", "ppm", 0, "outflow"))
    try:
        for problem, dim, integrator, solver, reconstruction, axis, boundary in cases:
            name = f"{problem}-{dim}d-{integrator}-{solver}-axis{axis}-{boundary}"
            template = root / (f"simulation/{problem}/{problem}.par" if problem in ("Sod", "Sedov")
                               else "validation/amr/inputs/smooth_uniform80.par")
            transverse = 3 if dim >= 2 else 1
            roots = 5 * transverse * (2 if dim == 3 else 1)
            values = {
                "nblockx1": 5, "nblockx2": 3 if dim >= 2 else 0, "nblockx3": 2 if dim == 3 else 0,
                "max_blocks": roots + 1, "lrefinemin": 0, "lrefinemax": 0,
                "tmax": 10, "max_steps": 16, "plt_dt": -1, "plt_dstep": 4,
                "chk_dt": -1, "chk_dstep": -1, "restart": "false",
                "solver": solver, "time_integrator": integrator, "reconstruct": reconstruction,
                "min_eint": "1e-10", "hll_wave_speed": "roe", "network_name": "none",
                "plt_variables": "DENS, PRES, VELX, VELY, VELZ, ENER, TEMP, ENTR, ENUC, SPECIES",
            }
            for direction in (1, 2, 3):
                for side in ("l", "r"):
                    values[f"x{direction}{side}_boundary_type"] = boundary
            if axis:
                values["mode_axis"] = axis
            elif problem == "Sedov":
                values.update(deposit_radius=0.12, center_x=0.4, center_y=0.5, center_z=0.5)
            if solver == "Roe":
                values.update(EntropyFix="true", EntropyFixCoefficient=0.2)
            source = parameters(template.read_text(), values)
            case = {"case": name, "root_shape": [5, transverse, 2 if dim == 3 else 1],
                    "steps": 16, "global_pool_capacity": roots + 1, "comparisons": []}
            report["cases"].append(case)
            reference = None
            cuda_reference = None
            def execute_lane(label, binary, ranks):
                nonlocal cuda_reference
                lane = args.evidence / name / label
                lane.mkdir(parents=True)
                par = lane / "run.par"
                par.write_text(parameters(source, {"out_dir": lane / "output", "base_name": "MPI",
                    "compute_backend": "cpu" if label == "baseline" else args.backend}))
                started = time.monotonic()
                run(binary, problem, par, lane / "run.log", ranks,
                    [args.mpiexec, "--bind-to", "none"], 1)
                completed = re.findall(r"Simulation Done\. Total Steps:\s+(\d+)", (lane / "run.log").read_text())
                assert completed == ["16"] * max(1, ranks), "one or more ranks did not complete 16 steps"
                expected = report["binaries"]["baseline" if label == "baseline" else
                                              "serial" if label == "serial" else "mpi"]
                verify_backend_identity(lane / "output",
                    "cpu" if label == "baseline" else args.backend, expected["sha256"])
                data = snapshots(lane / "output")
                assert len(data) == 5, "did not reach 16 steps / five scheduled snapshots"
                assert len(data[-1][1]["coordinate"]) == roots * 16 ** dim, "wrong global cell count"
                if label == "baseline":
                    return data
                else:
                    if args.backend == "cuda":
                        cross_backend = compare_backends(reference, data)
                        if label == "serial":
                            cuda_reference = data
                            result = {"lane": label, **cross_backend}
                        else:
                            assert cuda_reference is not None
                            result = {"lane": label, **compare(cuda_reference, data),
                                      "cpu_reference": cross_backend}
                    else:
                        result = {"lane": label, **compare(reference, data)}
                    result["elapsed_seconds"] = time.monotonic() - started
                    if ranks > 1:
                        capacities = []
                        for rank in range(ranks):
                            metadata = dict(line.split("=", 1) for line in
                                (lane / "output" / f"rank-{rank}" / "partition.txt").read_text().splitlines())
                            assert int(metadata["root_begin"]) == 5 * rank // ranks
                            assert int(metadata["root_end"]) == 5 * (rank + 1) // ranks
                            assert int(metadata["global_root_blocks"]) == roots
                            assert int(metadata["transverse_root_blocks"]) == roots // 5
                            owned = (5 * (rank + 1) // ranks - 5 * rank // ranks) * (roots // 5)
                            capacity = int(metadata["local_pool_capacity"])
                            assert capacity >= owned
                            capacities.append(capacity)
                        assert sum(capacities) == roots + 1, "global pool budget changed"
                        result["pool_capacities"] = capacities
                        assert not list((lane / "output").rglob("*chk*.h5")), "partial checkpoint"
                    return result

            reference = execute_lane("baseline", args.baseline, 0)
            # Independent output directories and read-only baseline arrays.
            # Eight application workers in total; affinity is left to the OS.
            if args.backend == "cuda":
                case["comparisons"].append(execute_lane("serial", args.serial_candidate, 0))
                save()
            with ThreadPoolExecutor(max_workers=report["concurrent_candidate_lanes"]) as executor:
                futures = [executor.submit(execute_lane, f"mpi-{ranks}", args.candidate, ranks)
                           for ranks in (1, 2, 4)]
                if args.backend == "cpu":
                    futures.append(executor.submit(execute_lane, "serial", args.serial_candidate, 0))
                for future in futures:
                    case["comparisons"].append(future.result())
                    save()
            print(name + ": all four comparisons passed", flush=True)

        template = parameters((root / "simulation/Sedov/Sedov.par").read_text(), {
            "nblockx1": 5, "nblockx2": 3, "nblockx3": 2, "lrefinemin": 0, "lrefinemax": 0,
            "max_blocks": 31, "max_steps": 1, "deposit_radius": 0.12,
            "chk_dt": -1, "chk_dstep": -1})
        for name, changes in {
            "unpaired-y": {"x2l_boundary_type": "periodic"},
            "unpaired-z": {"x3r_boundary_type": "periodic"},
            "pool-smaller-than-global-grid": {"max_blocks": 29},
        }.items():
            lane = args.evidence / "rejected" / name
            lane.mkdir(parents=True)
            par = lane / "run.par"
            par.write_text(parameters(template, {"out_dir": lane / "output", **changes}))
            env = dict(os.environ, OMP_NUM_THREADS="1", OPENBLAS_NUM_THREADS="1")
            with (lane / "run.log").open("w") as log:
                result = subprocess.run([args.mpiexec, "-n", "2", str(args.candidate), "Sedov", str(par)],
                    env=env, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT, timeout=30)
            assert result.returncode != 0, "unsupported input was accepted"
            expected = ("MPI global max_blocks is smaller than the root mesh"
                        if name == "pool-smaller-than-global-grid" else "MPI requires built-in boundaries and paired periodic faces")
            assert expected in (lane / "run.log").read_text(), "failure was not the intended guard"
            assert not list(lane.rglob("*.h5")), "rejected input emitted scientific files"
            report["rejections"].append({"case": name, "exit_code": result.returncode})
        report["state"] = "passed"
    except Exception as error:
        report.update(state="failed", error=str(error))
        raise
    finally:
        save()


if __name__ == "__main__":
    main()
