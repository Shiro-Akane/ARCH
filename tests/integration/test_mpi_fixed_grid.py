"""Compare distributed production runs with an independently built serial ARCH.
All inputs, rank output and logs stay below the explicitly supplied evidence path.
Requires numpy/h5py. No numerical tolerance: sampled native output bits must match.
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path
import h5py
import numpy as np


def parameters(text, values):
    for key, value in values.items():
        line = f"{key} = {value}"
        pattern = rf"(?m)^\s*{re.escape(key)}\s*=.*$"
        text = (
            re.sub(pattern, lambda m: line, text)
            if re.search(pattern, text)
            else text + "\n" + line + "\n"
        )
    return text


def run(executable, problem, par, log, ranks, mpiexec, threads=1):
    command = ([*mpiexec, "-n", str(ranks)] if ranks else []) + [str(executable), problem, str(par)]
    environment = dict(os.environ, OMP_NUM_THREADS=str(threads), OPENBLAS_NUM_THREADS="1")
    with log.open("w") as stream:
        result = subprocess.run(
            command,
            stdin=subprocess.DEVNULL,
            stdout=stream,
            stderr=subprocess.STDOUT,
            env=environment,
            timeout=120,
        )
    if result.returncode:
        raise RuntimeError(f"run failed ({result.returncode}): {log}")


def snapshots(directory):
    # Multi-rank files are shards; never confuse one shard with a full domain.
    pieces = sorted(directory.glob("rank-*")) or [directory]
    groups = [sorted(p.glob("*_plt_*.h5")) for p in pieces]
    assert groups and groups[0], f"no plots: {directory}"
    assert all(len(g) == len(groups[0]) for g in groups), "rank output count mismatch"
    assembled = []
    for frame in zip(*groups):
        data, coordinates, measures, roots, times = {}, [], [], [], []
        for filename in frame:
            with h5py.File(filename) as f:
                assert f.attrs["plot_publication_state"] == "complete"
                times.append(float(f.attrs["time"]))
                coordinates.append(np.column_stack([f["Grid/" + axis][:].reshape(-1) for axis in ("x", "y", "z")]))
                measures.append(f["NativeGrid/cell_measure"][:])
                roots.extend(zip(*(f["NativeGrid/logical_x" + str(axis)][:].tolist() for axis in (1, 2, 3))))
                fields = set(f["Data"].keys())
                if data:
                    assert fields == set(data), "rank field-set mismatch"
                for name in fields:
                    data.setdefault(name, []).append(f["Data/" + name][:].reshape(-1))
        assert len(set(times)) == 1, "ranks advanced with different clocks"
        assert len(roots) == len(set(roots)), "duplicate owned roots"
        root_extent = [max(key[axis] for key in roots) + 1 for axis in range(3)]
        assert len(roots) == int(np.prod(root_extent)), "missing owned roots"
        xyz = np.concatenate(coordinates)
        order = np.lexsort((xyz[:, 0], xyz[:, 1], xyz[:, 2]))
        assert np.all(np.any(xyz[order][1:] != xyz[order][:-1], axis=1)), "overlapping or duplicate cells"
        values = {name: np.concatenate(parts)[order] for name, parts in data.items()}
        values["coordinate"] = xyz[order, 0]
        values["coordinate_y"] = xyz[order, 1]
        values["coordinate_z"] = xyz[order, 2]
        values["cell_measure"] = np.concatenate(measures)[order]
        assembled.append((times[0], values))
    return assembled


def compare(reference, candidate):
    assert len(reference) == len(candidate), "output schedule differs"
    fields = set(reference[0][1])
    for (t0, a), (t1, b) in zip(reference, candidate):
        assert np.float64(t0).tobytes() == np.float64(t1).tobytes(), "global timestep differs"
        assert set(a) == set(b) == fields
        for name in fields:
            assert a[name].shape == b[name].shape
            assert np.all(np.isfinite(a[name])) and np.all(np.isfinite(b[name]))
            assert a[name].tobytes() == b[name].tobytes(), f"field differs: {name} at t={t0}"
    return {
        "snapshots": len(reference),
        "fields": sorted(fields),
        "bitwise_equal": True,
        "final_time": reference[-1][0],
        "cells": len(reference[-1][1]["coordinate"]),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--serial-candidate", type=Path)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--mpiexec", default="mpiexec")
    parser.add_argument("--mpiexec-arg", action="append", default=[])
    args = parser.parse_args()
    mpi_command = [args.mpiexec, *args.mpiexec_arg]
    root = Path(__file__).resolve().parents[2]
    args.evidence.mkdir(parents=True, exist_ok=False)
    report = {
        "baseline_sha256": hashlib.sha256(args.baseline.read_bytes()).hexdigest(),
        "candidate_sha256": hashlib.sha256(args.candidate.read_bytes()).hexdigest(),
        "cases": [],
        "mpiexec": mpi_command,
    }
    if args.serial_candidate:
        report["serial_candidate_sha256"] = hashlib.sha256(
            args.serial_candidate.read_bytes()
        ).hexdigest()
    cases = [
        ("Sod", "Euler", "HLLC", None),
        ("Sod", "RK2", "HLLC", None),
        ("Sod", "RK3", "HLL", None),
        ("SmoothAdvection", "RK2", "HLLC", None),
        ("SmoothAdvection", "RK3", "Roe", None),
        ("Sod", "RK2", "HLLC", "reflecting"),
    ]
    for problem, integrator, solver, boundary in cases:
        name = f"{problem}-{integrator}-{solver}" + (f"-{boundary}" if boundary else "")
        template = root / (
            "simulation/Sod/Sod.par"
            if problem == "Sod"
            else "validation/amr/inputs/smooth_uniform80.par"
        )
        source = parameters(
            template.read_text(),
            {
                "nblockx1": 7,
                "max_blocks": 32,
                "max_steps": 16,
                "time_integrator": integrator,
                "solver": solver,
                "plt_dstep": 1,
                "plt_dt": -1,
                "chk_dstep": -1,
                "chk_dt": -1,
                "min_eint": "1e-10",
                "hll_wave_speed": "roe",
                "network_name": "none",
                "plt_variables": "DENS, PRES, VELX, ENER, TEMP, ENTR, ENUC, SPECIES",
            },
        )
        if solver == "Roe":
            source = parameters(source, {"EntropyFix": "true", "EntropyFixCoefficient": 0.2})
        if boundary:
            # Nonzero velocities exercise reflecting faces immediately.
            source = parameters(
                source,
                {
                    "x1l_boundary_type": boundary,
                    "x1r_boundary_type": boundary,
                    "u_left": 1.0,
                    "u_right": -0.5,
                },
            )
        reference = None
        for ranks in (0, 1, 2, 4):
            lane = args.evidence / name / ("baseline" if not ranks else f"mpi-{ranks}")
            lane.mkdir(parents=True)
            par = lane / "run.par"
            par.write_text(parameters(source, {"out_dir": lane / "output"}))
            run(
                args.candidate if ranks else args.baseline,
                problem,
                par,
                lane / "run.log",
                ranks,
                mpi_command,
            )
            state = snapshots(lane / "output")
            if not ranks:
                reference = state
            else:
                assert reference is not None
                result = compare(reference, state)
                if ranks > 1:
                    assert not list(
                        (lane / "output").rglob("*_chk_*.h5")
                    ), "partial serial checkpoint emitted"
                    for rank in range(ranks):
                        metadata = dict(
                            line.split("=", 1)
                            for line in (lane / "output" / f"rank-{rank}" / "partition.txt")
                            .read_text()
                            .splitlines()
                        )
                        assert int(metadata["root_begin"]) == 7 * rank // ranks
                        assert int(metadata["root_end"]) == 7 * (rank + 1) // ranks
                        assert int(metadata["local_pool_capacity"]) < 32
                report["cases"].append({"case": name, "ranks": ranks, **result})
                print(
                    f"PASS {name}: ranks={ranks}, frames={result['snapshots']}, fields={len(result['fields'])}",
                    flush=True,
                )
        if args.serial_candidate:
            lane = args.evidence / name / "mpi-disabled"
            lane.mkdir()
            par = lane / "run.par"
            par.write_text(parameters(source, {"out_dir": lane / "output"}))
            run(args.serial_candidate, problem, par, lane / "run.log", 0, mpi_command)
            report["cases"].append(
                {
                    "case": name,
                    "ranks": 1,
                    "mpi_enabled": False,
                    **compare(reference, snapshots(lane / "output")),
                }
            )
        # One hybrid check uses the same physical trajectory, with two OMP workers per rank.
        if problem == "Sod" and integrator == "RK2" and boundary is None:
            lane = args.evidence / name / "mpi-2-omp-2"
            lane.mkdir()
            par = lane / "run.par"
            par.write_text(parameters(source, {"out_dir": lane / "output"}))
            run(args.candidate, problem, par, lane / "run.log", 2, mpi_command, threads=2)
            report["cases"].append(
                {
                    "case": name,
                    "ranks": 2,
                    "omp_threads": 2,
                    **compare(reference, snapshots(lane / "output")),
                }
            )
    # Seven roots with only eight total slots expose capacity over-allocation
    # when five ranks split roots and pool capacity independently.
    lane = args.evidence / "Sod-Euler-HLLC" / "mpi-5-tight-pool"
    lane.mkdir()
    reference_lane = args.evidence / "Sod-Euler-HLLC" / "baseline"
    par = lane / "run.par"
    par.write_text(
        parameters(
            (reference_lane / "run.par").read_text(), {"out_dir": lane / "output", "max_blocks": 8}
        )
    )
    run(args.candidate, "Sod", par, lane / "run.log", 5, mpi_command)
    capacities = []
    for rank in range(5):
        metadata = dict(
            line.split("=", 1)
            for line in (lane / "output" / f"rank-{rank}" / "partition.txt")
            .read_text()
            .splitlines()
        )
        owned = int(metadata["root_end"]) - int(metadata["root_begin"])
        capacity = int(metadata["local_pool_capacity"])
        assert capacity >= owned > 0
        capacities.append(capacity)
    assert sum(capacities) == 8, "distributed pool exceeded the global max_blocks budget"
    report["cases"].append(
        {
            "case": "Sod-Euler-HLLC",
            "ranks": 5,
            "global_pool_capacity": 8,
            "rank_pool_capacities": capacities,
            **compare(snapshots(reference_lane / "output"), snapshots(lane / "output")),
        }
    )
    # Unsupported configurations must end promptly and emit no scientific files.
    original = (root / "simulation/Sod/Sod.par").read_text()
    report["rejections"] = []
    for name, changes in {
        "amr": {"lrefinemax": 1},
        "empty-rank": {"nblockx1": 1},
        "checkpoint": {"chk_dstep": 1},
        "cuda": {"compute_backend": "cuda"},
        "one-sided-periodic": {"x1l_boundary_type": "periodic"},
    }.items():
        lane = args.evidence / "rejected" / name
        lane.mkdir(parents=True)
        par = lane / "run.par"
        par.write_text(parameters(original, {"out_dir": lane / "output", **changes}))
        with (lane / "run.log").open("w") as stream:
            result = subprocess.run(
                [*mpi_command, "-n", "2", str(args.candidate), "Sod", str(par)],
                stdin=subprocess.DEVNULL,
                stdout=stream,
                stderr=subprocess.STDOUT,
                timeout=30,
            )
        assert result.returncode != 0, f"unsupported configuration accepted: {name}"
        assert not list(lane.rglob("*.h5"))
        report["rejections"].append({"case": name, "returncode": result.returncode})
    for name in ("rank-input-mismatch", "one-rank-missing-input"):
        lane = args.evidence / "rejected" / name
        lane.mkdir(parents=True)
        first = lane / "first.par"
        second = lane / "second.par"
        first.write_text(parameters(original, {"out_dir": lane / "output", "cfl": 0.3}))
        if name == "rank-input-mismatch":
            second.write_text(parameters(original, {"out_dir": lane / "output", "cfl": 0.4}))
        with (lane / "run.log").open("w") as stream:
            result = subprocess.run(
                [
                    *mpi_command,
                    "-n",
                    "1",
                    str(args.candidate),
                    "Sod",
                    str(first),
                    ":",
                    "-n",
                    "1",
                    str(args.candidate),
                    "Sod",
                    str(second),
                ],
                stdin=subprocess.DEVNULL,
                stdout=stream,
                stderr=subprocess.STDOUT,
                timeout=30,
            )
        assert result.returncode != 0, f"rank disagreement accepted: {name}"
        assert not list(lane.rglob("*.h5"))
        report["rejections"].append({"case": name, "returncode": result.returncode})
    (args.evidence / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    print("ALL MPI APPLICATION COMPARISONS PASSED", flush=True)


if __name__ == "__main__":
    main()
