"""Reject heterogeneous native MPI launch identities before output acquisition.

Uses actual processes and explicit server evidence. The modified executable
remains a byte-identical code image with an appended identity marker; it is
never a production build. Device visibility checks require --cuda explicitly.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time

from test_mpi_fixed_grid import parameters


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--mpiexec", required=True)
    parser.add_argument("--cuda", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    args.evidence.mkdir(parents=True, exist_ok=False)
    binary = args.candidate.resolve()
    alternate = args.evidence / "identity-marker-ARCH"
    shutil.copy2(binary, alternate)
    with alternate.open("ab") as stream:
        stream.write(b"ARCH MPI executable identity negative fixture\n")
    assert hashlib.sha256(binary.read_bytes()).digest() != hashlib.sha256(alternate.read_bytes()).digest()
    report = {"candidate_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
              "alternate_sha256": hashlib.sha256(alternate.read_bytes()).hexdigest(),
              "rejections": [], "status": "running"}
    def save():
        (args.evidence / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    cases = [("executable", "cpu", binary, alternate, {}, {},
              "different executable, problem or parameter contents")]
    if args.cuda:
        cases.extend([
            ("auto-device", "auto", binary, binary, {"CUDA_VISIBLE_DEVICES": ""},
             {}, "effective configuration, EOS or resolved backend differs"),
            ("explicit-device", "cuda", binary, binary, {"CUDA_VISIBLE_DEVICES": ""},
             {}, "CUDA runtime unavailable")])
    save()
    try:
        for name, backend, left, right, left_env, right_env, reason in cases:
            lane = args.evidence / name
            lane.mkdir()
            output = lane / "output"
            par = lane / "run.par"
            par.write_text(parameters((root / "simulation/Sedov/Sedov.par").read_text(), {
                "compute_backend": backend, "nblockx1": 7, "nblockx2": 2,
                "max_blocks": 512, "lrefinemin": 0, "lrefinemax": 2,
                "max_steps": 2, "out_dir": output}))
            command = [args.mpiexec]
            for executable, environment in ((left, left_env), (right, right_env)):
                if len(command) > 1:
                    command.append(":")
                command += ["-n", "1", "/usr/bin/env",
                            *(f"{key}={value}" for key, value in environment.items()),
                            str(executable), "Sedov", str(par)]
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
                    raise AssertionError("MPI startup rejection hung: " + name)
            text = (lane / "run.log").read_text()
            assert code != 0 and reason in text, (name, code, text[-2000:])
            assert not output.exists(), "rejected startup acquired output: " + name
            report["rejections"].append({"case": name, "returncode": code,
                "seconds": time.monotonic() - started, "reason": reason,
                "output_acquired": False})
            save()
            print("PASS rejected", name, flush=True)
        report["status"] = "passed"
    except BaseException as error:
        report["status"] = "failed"
        report["error"] = str(error)
        raise
    finally:
        save()


if __name__ == "__main__":
    main()
