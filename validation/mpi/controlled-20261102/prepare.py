#!/usr/bin/env python3
"""Prepare a fresh server evidence directory without copying the repository."""
import argparse, hashlib, json, os, shutil, subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("--repository", type=Path, default=Path(__file__).resolve().parents[3])
p.add_argument("--evidence", type=Path, required=True)
p.add_argument("--binary", type=Path, required=True)
p.add_argument("--baseline", type=Path, required=True)
p.add_argument("--candidate-revision", required=True, help="Source commit used to build --binary")
p.add_argument("--baseline-revision", required=True)
p.add_argument("--before-binary", type=Path, help="Optional frozen before executable for paired suites")
p.add_argument("--before-revision", help="Source revision of --before-binary")
p.add_argument("--toolchain", type=Path, default=Path("/home/ubuntu/projects/ARCH-mpi-toolchain/env"))
a = p.parse_args()
if bool(a.before_binary) != bool(a.before_revision):
    p.error("--before-binary and --before-revision must be supplied together")
if a.before_binary and not a.before_binary.is_file():
    p.error("--before-binary must be an existing executable")
root, evidence, toolchain = a.repository.resolve(), a.evidence.resolve(), a.toolchain.resolve()
assert a.binary.is_file() and a.baseline.is_file()
assert (toolchain/"bin/mpicc").is_file()
evidence.mkdir(parents=True, exist_ok=False)
for part in ("tools", "inputs", "bin"):
    (evidence/part).mkdir()
for name in ("measure.py", "rank_wrapper.py"):
    shutil.copy2(root/"validation/mpi/scaling-20261101"/name, evidence/"tools"/name)
shutil.copy2(root/"tests/integration/test_mpi_fixed_grid.py", evidence/"tools/mpi_compare.py")
shutil.copy2(Path(__file__).with_name("run.py"), evidence/"tools/run.py")
shutil.copy2(root/"validation/amr/inputs/smooth_uniform80.par", evidence/"inputs/smooth.par")
shutil.copy2(root/"simulation/Sedov/Sedov.par", evidence/"inputs/sedov.par")
shutil.copy2(a.binary, evidence/"bin/ARCH")
shutil.copy2(a.baseline, evidence/"bin/ARCH-baseline")
if a.before_binary:
    shutil.copy2(a.before_binary, evidence/"bin/ARCH-before")
    shutil.copy2(a.binary, evidence/"bin/ARCH-concurrent")
env = dict(os.environ, PATH=str(toolchain/"bin")+":"+os.environ["PATH"],
           LD_LIBRARY_PATH=str(toolchain/"lib"))
subprocess.run([str(toolchain/"bin/mpicc"), "-O2", "-shared", "-fPIC",
    str(Path(__file__).with_name("mpi_trace.c")), "-o", str(evidence/"bin/libmpi_trace.so")],
    env=env, stdin=subprocess.DEVNULL, check=True)
source = {"commit": a.candidate_revision, "baseline": a.baseline_revision,
          "toolchain": str(toolchain),
          "binary_sha256": {f.name: hashlib.sha256(f.read_bytes()).hexdigest()
                            for f in (evidence/"bin").iterdir()}}
if a.before_binary:
    source["before_commit"] = a.before_revision
(evidence/"source.json").write_text(json.dumps(source,indent=2)+"\n")
print(json.dumps({"evidence":str(evidence),"source":source}))
