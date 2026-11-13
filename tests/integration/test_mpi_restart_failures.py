"""Reject physically incomplete collections even after checksums are recomputed."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time

import h5py
from test_mpi_fixed_grid import parameters


def refresh_manifest(directory):
    lines = (directory / "manifest.txt").read_text().splitlines()
    ranks = int(next(line.split()[1] for line in lines if line.startswith("ranks ")))
    hashes, rows = [], {}
    for rank in range(ranks):
        path = directory / f"rank-{rank}" / "state.h5"
        with h5py.File(path) as file:
            keys = sorted(zip(file["Blocks/level"][:].tolist(),
                *(file[f"Blocks/logical_x{i}"][:].tolist() for i in (1, 2, 3))))
            dim = int(file.attrs["dim"])
        record = "arch-output-local-topology-1\n" + "".join(
            f"{dim} {level} {x} {y} {z}\n" for level, x, y, z in keys)
        hashes.append(hashlib.sha256(record.encode()).hexdigest())
        rows[rank] = f"shard {rank} {len(keys)} {path.stat().st_size} {hashlib.sha256(path.read_bytes()).hexdigest()}"
    topology = hashlib.sha256("".join(hashes).encode()).hexdigest()
    result = ["topology " + topology if line.startswith("topology ") else
              rows[int(line.split()[1])] if line.startswith("shard ") else line
              for line in lines]
    (directory / "manifest.txt").write_text("\n".join(result) + "\n")
    return ranks


def main():
    parser = argparse.ArgumentParser()
    for name in ("candidate", "checkpoint", "config", "evidence"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--mpiexec", required=True)
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=False)
    original = hashlib.sha256((args.checkpoint / "manifest.txt").read_bytes()).hexdigest()
    report = {"status": "running", "cases": [], "original_manifest_sha256": original,
              "candidate_sha256": hashlib.sha256(args.candidate.read_bytes()).hexdigest()}
    def save():
        (args.evidence / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    save()
    try:
        for name in ("missing-leaf", "outside-domain", "overlapping-ancestor", "duplicate-owner", "rank-count"):
            lane = args.evidence / name
            lane.mkdir()
            copied = lane / "altered.collection"
            shutil.copytree(args.checkpoint, copied)
            first, last = copied / "rank-0/state.h5", copied / "rank-1/state.h5"
            with h5py.File(first, "r+") as file:
                levels = file["Blocks/level"][:]
                if name == "missing-leaf":
                    assert len(levels) > 1
                    for key in list(file["Blocks"]):
                        values = file["Blocks/" + key][:][1:]
                        del file["Blocks/" + key]
                        file["Blocks/" + key] = values
                    for key in list(file["Data"]):
                        values = file["Data/" + key][:]
                        values = values[:, 1:, :] if key in ("X", "rhoX") else values[1:, :]
                        del file["Data/" + key]
                        file["Data/" + key] = values
                elif name == "outside-domain":
                    file["Blocks/logical_x1"][0] = 100000
                elif name == "overlapping-ancestor":
                    index = next(i for i, level in enumerate(levels) if level == 2)
                    file["Blocks/level"][index] = 1
                    for axis in (1, 2, 3):
                        field = file[f"Blocks/logical_x{axis}"]
                        field[index] = int(field[index]) // 2
                first_key = [int(file["Blocks/" + key][0]) for key in
                             ("level", "logical_x1", "logical_x2", "logical_x3")]
            if name == "duplicate-owner":
                with h5py.File(last, "r+") as file:
                    for key, value in zip(("level", "logical_x1", "logical_x2", "logical_x3"), first_key):
                        file["Blocks/" + key][0] = value
            ranks = refresh_manifest(copied)
            if name == "rank-count":
                ranks += 1
            output = lane / "output"
            par = lane / "run.par"
            par.write_text(parameters(args.config.read_text(), {
                "restart": "true", "restart_file": copied, "out_dir": output, "max_steps": 28}))
            command = [args.mpiexec, "-n", str(ranks), str(args.candidate), "Sedov", str(par)]
            started = time.monotonic()
            with (lane / "run.log").open("w") as log:
                child = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                    stdin=subprocess.DEVNULL, start_new_session=True,
                    env=dict(os.environ, OMP_NUM_THREADS="1", OMP_DYNAMIC="FALSE"))
                try:
                    code = child.wait(timeout=60)
                except subprocess.TimeoutExpired:
                    os.killpg(child.pid, signal.SIGKILL)
                    child.wait()
                    raise AssertionError("MPI restart rejection hung: " + name)
            text = (lane / "run.log").read_text()
            assert code != 0, "invalid native topology was accepted: " + name
            assert "[IO] Saved" not in text and "Simulation Done" not in text
            assert not list(output.rglob("*.h5")), "restart published invalid scientific state"
            failures = [line for line in text.splitlines() if "[Fatal Error]" in line]
            assert failures, text[-2000:]
            expected = {
                "missing-leaf": "distributed root coverage is incomplete",
                "outside-domain": "neighbor leaf outside global domain",
                "overlapping-ancestor": "distributed leaves overlap or leave a gap",
                "duplicate-owner": "topology snapshot has duplicate logical identity",
                "rank-count": "Checkpoint collection kind/rank count differs from launch",
            }[name]
            assert expected in text, "restart failed for the wrong reason: " + text[-2000:]
            report["cases"].append({"case": name, "returncode": code,
                "seconds": time.monotonic() - started, "errors": failures})
            save()
            print("PASS rejected", name, failures[0], flush=True)
        assert hashlib.sha256((args.checkpoint / "manifest.txt").read_bytes()).hexdigest() == original
        report["status"] = "passed"
    except BaseException as error:
        report["status"] = "failed"
        report["error"] = str(error)
        raise
    finally:
        save()


if __name__ == "__main__":
    main()
