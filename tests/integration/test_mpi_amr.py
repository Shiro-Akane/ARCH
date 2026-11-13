"""Production root startup, AMR, complete output and same-rank continuation.
Uses native checkpoints and the existing field/conservation gates. All evidence
stays under the explicitly supplied server directory; no result is deleted.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

import h5py
import numpy as np
from test_mpi_fixed_grid import parameters, verify_backend_identity

FIELDS = ("rho", "mom_u", "mom_v", "mom_w", "eng", "enuc_rate", "X", "rhoX")


def published_shards(directory):
    lines = (directory / "manifest.txt").read_text().splitlines()
    assert lines[0] == "ARCH_OUTPUT_COLLECTION 1" and lines[-1] == "end"
    headers = dict(line.split(maxsplit=1) for line in lines[1:-1] if not line.startswith("shard "))
    records = [line.split() for line in lines if line.startswith("shard ")]
    assert int(headers["ranks"]) == len(records) >= 2
    paths = []
    for rank, row in enumerate(records):
        assert len(row) == 5 and int(row[1]) == rank and int(row[2]) > 0
        path = directory / f"rank-{rank}" / "state.h5"
        assert path.is_file() and not path.is_symlink()
        assert path.stat().st_size == int(row[3])
        assert hashlib.sha256(path.read_bytes()).hexdigest() == row[4]
        paths.append(path)
    return headers, paths


def checkpoints(directory):
    return sorted(directory.glob("*_chk_*.collection")) or sorted(directory.glob("*_chk_*.h5"))


def state(path):
    manifest, shards = published_shards(path) if path.is_dir() else (None, [path])
    keys, fields, clocks, repairs = [], {name: [] for name in FIELDS}, [], []
    dimensions = []
    for shard in shards:
        with h5py.File(shard) as file:
            dimension = int(file.attrs["dim"])
            assert dimension in (1, 2, 3), "invalid checkpoint dimension"
            dimensions.append(dimension)
            cells_per_block = 16 ** dimension
            clock = (int(file.attrs["step_count"] if "step_count" in file.attrs else file.attrs["step"]),
                     float(file.attrs["time"]), float(file.attrs["dt_old"]))
            clocks.append(clock)
            local = list(zip(file["Blocks/level"][:].tolist(),
                             *(file[f"Blocks/logical_x{i}"][:].tolist() for i in (1, 2, 3))))
            keys.extend(local)
            blocks = len(local)
            for name in FIELDS:
                value = file["Data/" + name][:]
                if name in ("X", "rhoX"):
                    value = value.reshape(-1, blocks, cells_per_block).transpose(1, 0, 2)
                else:
                    value = value.reshape(blocks, cells_per_block)
                fields[name].append(value)
            repairs.append(file["state_repairs"][:])
            if manifest:
                assert manifest["kind"] == "checkpoint"
                assert int(manifest["step"]) == clock[0]
                assert np.float64(clock[1]).view(np.uint64) == int(manifest["time_bits"])
                assert int(manifest["index"]) == int(file.attrs["chk_index"])
    assert len(set(clocks)) == 1 and len(keys) == len(set(keys))
    assert len(set(dimensions)) == 1, "shard dimensions differ"
    # Reject overlapping ancestors; the exact volume check also rejects gaps.
    owned = set(keys)
    for level, x, y, z in keys:
        for ancestor in range(level):
            shift = level - ancestor
            assert (ancestor, x >> shift, y >> shift, z >> shift) not in owned
    order = sorted(range(len(keys)), key=keys.__getitem__)
    values = {name: np.concatenate(parts, axis=0)[order] for name, parts in fields.items()}
    assert all(np.all(np.isfinite(value)) for value in values.values())
    return {"keys": [keys[i] for i in order], "fields": values, "clock": clocks[0],
            "repairs": np.sum(repairs, axis=0), "dimension": dimensions[0]}


def compare(left, right, exact=False):
    assert left["dimension"] == right["dimension"], "checkpoint dimension differs"
    assert left["keys"] == right["keys"], "accepted leaf topology differs"
    assert left["clock"] == right["clock"], "physical clock differs"
    worst = 0.
    for name in FIELDS:
        a, b = left["fields"][name], right["fields"][name]
        assert a.shape == b.shape, name
        if exact:
            assert a.tobytes() == b.tobytes(), "continuation changed native bits: " + name
        else:
            scale = np.maximum(1., np.maximum(np.abs(a), np.abs(b)))
            error = np.abs(a - b) / scale
            worst = max(worst, float(np.max(error, initial=0.)))
            assert np.all(error <= 2.e-13), (name, worst)
    assert np.array_equal(left["repairs"], right["repairs"]), "repair accounting differs"
    return {"native_bitwise_equal": exact, "maximum_scaled_error": worst,
            "leaves": len(left["keys"]), "step": left["clock"][0], "time": left["clock"][1]}


def totals(value, root_x, root_y):
    dimension = value["dimension"]
    cells_per_block = 16 ** dimension
    root_blocks = root_x * root_y
    volume = np.array([1. / (root_blocks * cells_per_block * 2 ** (dimension * key[0]))
                       for key in value["keys"]], dtype=np.longdouble)
    assert abs(float(np.sum(volume * cells_per_block)) - 1.) <= 2.e-14, "leaf domain has a gap"
    fields = value["fields"]
    result = [np.sum(fields[name].astype(np.longdouble) * volume[:, None])
              for name in ("rho", "mom_u", "mom_v", "mom_w", "eng")]
    for species in range(fields["rhoX"].shape[1]):
        result.append(np.sum(fields["rhoX"][:, species, :].astype(np.longdouble) * volume[:, None]))
    return np.array(result)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--backend", choices=("cpu", "cuda"), required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--mpiexec", required=True)
    parser.add_argument("--dimension", type=int, choices=(1, 2), default=2)
    parser.add_argument("--max-level", type=int, choices=(1, 2, 3), default=2)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--smoke", action="store_true")
    mode.add_argument("--long", action="store_true")
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=False)
    root = Path(__file__).resolve().parents[2]
    problem = "Sod" if args.dimension == 1 else "Sedov"
    report = {"state": "running", "backend": args.backend, "cases": [],
              "binaries": {name: {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                           for name, path in (("candidate", args.candidate), ("reference", args.reference))},
              "arithmetic_gate": "2e-13 * max(1, abs(a), abs(b))",
              "periodic_conservation_gate": "2e-11 absolute + 2e-12 relative",
               "scope": f"Cartesian {args.dimension}-D level 0..{args.max_level}, same-rank checkpoint continuation"}
    def save():
        (args.evidence / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    def execute(label, case_dir, source, binary, backend, ranks, changes=None):
        lane = case_dir / label
        lane.mkdir()
        text = parameters(source, {"compute_backend": backend, "out_dir": lane / "output", **(changes or {})})
        par = lane / "run.par"
        par.write_text(text)
        command = ([args.mpiexec, "-n", str(ranks)] if ranks > 1 else []) + [str(binary), problem, str(par)]
        started = time.monotonic()
        with (lane / "run.log").open("w") as log:
            child = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                   env=dict(os.environ, OMP_NUM_THREADS="1", OMP_DYNAMIC="FALSE"), timeout=600)
        assert child.returncode == 0, f"production run failed: {lane / 'run.log'}"
        expected_binary = next(value["sha256"] for value in report["binaries"].values()
                               if value["path"] == str(binary))
        verify_backend_identity(lane / "output", backend, expected_binary)
        return lane, time.monotonic() - started
    save()
    try:
        cases = [("RK3", "muscl", "periodic", 4)] if args.long else (
            [("RK2", "pcm", "periodic", 2)] if args.smoke else [
            (method, reconstruction, boundary, ranks)
            for method in ("Euler", "RK2", "RK3")
            for reconstruction in ("pcm", "muscl", "ppm")
            for boundary in ("outflow", "periodic", "reflecting") for ranks in (2, 4)])
        for method, reconstruction, boundary, ranks in cases:
            name = f"{problem}-{method}-{reconstruction}-{boundary}-r{ranks}"
            case_dir = args.evidence / name
            case_dir.mkdir()
            steps = 512 if args.long else 12 if args.smoke else 24
            output_stride = 32 if args.long else 4
            root_x, root_y = 7, 1 if args.dimension == 1 else 2
            source = parameters((root / f"simulation/{problem}/{problem}.par").read_text(), {
                "nblockx1": root_x, "nblockx2": 0 if args.dimension == 1 else root_y,
                "nblockx3": 0, "max_blocks": 1024 if args.max_level == 3 else 512,
                **({"center_x": 3. / 7.} if args.dimension == 2 else {}),
                "lrefinemin": 0, "lrefinemax": args.max_level,
                "refine_threshold": 0.05, "derefine_threshold": 0.01,
                "regrid_interval": 2, "max_steps": steps, "tmax": 100., "time_integrator": method,
                "reconstruct": reconstruction, "plt_dt": -1, "plt_dstep": output_stride,
                "chk_dt": -1, "chk_dstep": output_stride,
                "x1l_boundary_type": boundary, "x1r_boundary_type": boundary,
                "x2l_boundary_type": boundary, "x2r_boundary_type": boundary,
                "plt_variables": "DENS, PRES, VELX, VELY, VELZ, ENER, TEMP, ENUC, SPECIES",
                "restart": "false", "base_name": "AMR"})
            serial, _ = execute("serial-cpu", case_dir, source, args.reference, "cpu", 1)
            actual, seconds = execute("distributed", case_dir, source, args.candidate, args.backend, ranks)
            reference_paths, actual_paths = checkpoints(serial / "output"), checkpoints(actual / "output")
            reference_states = [state(path) for path in reference_paths]
            actual_states = [state(path) for path in actual_paths]
            assert len(reference_states) == len(actual_states) >= 4, "checkpoint schedule differs"
            comparisons = [compare(a, b) for a, b in zip(reference_states, actual_states)]
            assert actual_states[-1]["clock"][0] == steps
            distinct_topologies = len({tuple(value["keys"]) for value in actual_states})
            if args.long:
                assert distinct_topologies > 1, "long run did not change the initial leaf topology"
            assert max(key[0] for value in actual_states for key in value["keys"]) == args.max_level, "requested deepest level was not reached"
            for value in actual_states:
                totals(value, root_x, root_y)
            conservation = []
            if boundary == "periodic":
                initial = totals(actual_states[0], root_x, root_y)
                for value in actual_states[1:]:
                    drift = np.abs(totals(value, root_x, root_y) - initial)
                    assert np.all(drift <= 2.e-11 + 2.e-12 * np.abs(initial)), "periodic conservation failed"
                    conservation.append(float(np.max(drift)))
            index = len(actual_paths) // 2
            continuation, resumed_seconds = execute("continued", case_dir, source, args.candidate, args.backend, ranks,
                {"restart": "true", "restart_file": actual_paths[index]})
            resumed = state(checkpoints(continuation / "output")[-1])
            exact = compare(actual_states[-1], resumed, exact=True)
            plots = sorted((actual / "output").glob("*_plt_*.collection"))
            assert plots, "distributed plots did not publish collections"
            for collection in plots:
                manifest, shards = published_shards(collection)
                assert manifest["kind"] == "plot"
                for shard in shards:
                    with h5py.File(shard) as file:
                        assert file.attrs["plot_publication_state"] == "complete"
                        assert float(file.attrs["time"]) >= 0.
                        assert all(np.all(np.isfinite(dataset[:])) for dataset in file["Data"].values())
            record = {"case": name, "comparisons": comparisons, "continuation": exact,
                      "checkpoint_count": len(actual_paths), "plot_collections": len(plots),
                      "distinct_checkpoint_topologies": distinct_topologies,
                      "maximum_conservation_drift": max(conservation, default=None),
                      "run_seconds": seconds, "continuation_seconds": resumed_seconds}
            report["cases"].append(record)
            save()
            print("PASS", name, exact, flush=True)
        report["state"] = "passed"
    except BaseException as error:
        report["state"] = "failed"
        report["error"] = str(error)
        raise
    finally:
        save()


if __name__ == "__main__":
    main()
