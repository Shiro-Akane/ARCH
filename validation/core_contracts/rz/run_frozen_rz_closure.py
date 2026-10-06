"""Execute the frozen private CPU RZ source subset; preserve raw data locally."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def fields(line):
    return dict(re.findall(r"(\w+)=(.*?)(?= \w+=|$)", line))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source, build, output = args.source.resolve(), args.build.resolve(), args.output.resolve()
    owned = (ROOT / "studio/.local/integration").resolve()
    if any(not path.is_relative_to(owned) for path in (source, build, output)) or output.exists():
        raise RuntimeError("private source/build and new local output required")
    if "CMAKE_HOME_DIRECTORY:INTERNAL=" + str(source) not in (build / "CMakeCache.txt").read_text():
        raise RuntimeError("source/build identity mismatch")
    output.mkdir(parents=True)
    elf = build / "arch_curvilinear_metrics"
    records = []
    for name, arguments, expected in [
        ("owner", ["rz-source-owner-audit"], 0),
        ("numerical-rejection", ["rz-source-rejection-audit"], 0),
        ("frozen-rk-checkpoint", ["rz-applied-torque-restart-audit", str(output / "checkpoints")], 0),
        ("curvilinear-regression", [], 0),
        ("axis-rhs", ["rz-equilibrium-audit"], 2),
    ]:
        started = time.monotonic()
        run = subprocess.run([str(elf), *arguments], capture_output=True, text=True, timeout=120,
                             env={**os.environ, "OMP_NUM_THREADS": "1"})
        (output / (name + ".log")).write_text(run.stdout + run.stderr)
        record = {"name": name, "exit_code": run.returncode,
                  "elapsed_seconds": time.monotonic() - started,
                  "status": "FAILED_ORIGINAL_AXIS_RHS_GATE" if name == "axis-rhs" else "PASS",
                  "diagnostics": [line for line in run.stdout.splitlines()
                      if line.startswith(("RZ_PRODUCTION_OWNER_PASS", "RZ_NUMERICAL_REJECTION_PASS",
                                          "RZ_EQUILIBRIUM_SPATIAL_GATE"))]}
        records.append(record)
        if run.returncode != expected:
            record["status"] = "FAILED"
            (output / "partial-summary.json").write_text(json.dumps(records, indent=2) + "\n")
            raise RuntimeError(name + ": failed; do not continue or relabel")
        if name == "frozen-rk-checkpoint":
            evolution = [fields(line) for line in run.stdout.splitlines() if line.startswith("RZ_ROTATING_BUDGET")]
            restart = [fields(line) for line in run.stdout.splitlines() if line.startswith("RZ_FROZEN_CHECKPOINT_PASS")]
            if len(evolution) != 24 or len(restart) != 24:
                raise RuntimeError("incomplete frozen case set")
            for row in restart:
                if float(row["split_time"]) != 5e-4 or float(row["final_time"]) != 1e-3 \
                        or row["split_step"] != "5" or row["final_step"] != "10" or row["bit_words"] != "10240":
                    raise RuntimeError("actual checkpoint endpoint/state extent differs")
            for row in evolution:
                for key in ("J_error", "mass_error", "E_error", "species_error"):
                    value = float(row[key])
                    if not __import__("math").isfinite(value) or value > 1e-12:
                        raise RuntimeError("frozen physical budget failed")
            record.update({"evolution": evolution, "restart": restart,
                "maximum_errors": {key: max(float(row[key]) for row in evolution)
                    for key in ("J_error", "mass_error", "E_error", "species_error")}})
        if name == "axis-rhs":
            if "RZ_EQUILIBRIUM_SPATIAL_GATE=NOT_CLEARED" not in run.stdout:
                raise RuntimeError("axis failure reason changed; review required")
            record["rows"] = [fields(line) for line in run.stdout.splitlines()
                              if line.startswith("RZ_EQUILIBRIUM inner=")]
        print(name + " " + record["status"], flush=True)
    input_file = source / "tests/host/grid/test_curvilinear_metrics.cpp"
    summary = {
        "status": "PRIVATE_EXTERNAL_SUBSET_PASS_FULL_RZ_NOT_SIGNED",
        "delivery_base": "1f743efd7cf7d0793a766f3c2cd4fdca1bc9cadd",
        "snapshot_base": "326626cb1f51dc82ad78c5d59238b07e84d1cbbc",
        "core_contract": "4639774fe3c94ae27b3e831d0f0b9d6340de34fd",
        "closure_ref": "b466ce0216928fee56878afe43aae9e8a1614f25",
        "elf_sha256": digest(elf), "fixture_sha256": digest(input_file),
        "frozen_input": {"r": [1, 3], "z": [-1, 1], "dt": 1e-4, "steps": 10,
            "g_phi": [-0.025, 0.025], "schemes": ["Euler", "RK2", "RK3"],
            "interface_directions": [0, 1], "open_boundary": [False, True],
            "field_count": 5, "species_count": 2, "leaves": 5},
        "public_gates": "UNCHANGED", "records": records,
        "raw_output_root": str(output), "raw_output_bytes": sum(
            f.stat().st_size for f in output.rglob("*") if f.is_file()),
        "benchmark_2d": {"status": "NOT_RUN", "reason":
            "physical-core/type association and owned allocation/next-write resource guards are not established"},
    }
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")

if __name__ == "__main__":
    main()
