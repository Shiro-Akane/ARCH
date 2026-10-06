"""Reproduce private RZ producer mutations without altering public source."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    owned = (ROOT / "studio/.local/integration").resolve()
    if not source.is_relative_to(owned) or not output.is_relative_to(owned) or output.exists():
        raise RuntimeError("private source and fresh owned output required")
    output.mkdir(parents=True)
    header = (source / "src/physics/gravity/ExternalGravity.h").read_text()
    patterns = {
        "wrong_W_momentum_for_V_work": (
            "const double mphi_v=mean.mom_w+dmphi*(rv-rw);",
            "const double mphi_v=mean.mom_w;"),
        "wrong_V_density_for_W_torque": (
            "const double rho_w=mean.rho+drho*(rw-rv);",
            "const double rho_w=mean.rho;"),
        "missing_actual_ledger_read_check": (
            "ledger.require_readable({identity.block,identity.slot},\n"
            "            {arch::state::ExecutionSide::Host,identity.version,true,true});",
            "/* mutation: no ledger check */"),
    }
    results = []
    for name, replacement in {"correct": None, **patterns}.items():
        folder = output / name
        include = folder / "include/physics/gravity"
        include.mkdir(parents=True)
        edited = header
        if replacement:
            old, new = replacement
            if header.count(old) != 1:
                raise RuntimeError(name + ": candidate shape changed")
            edited = header.replace(old, new)
        candidate = include / "ExternalGravity.h"
        candidate.write_text(edited)
        executable = folder / "test"
        built = subprocess.run(["g++", "-std=c++20", "-O2",
            "-I" + str(folder / "include"), "-I" + str(source / "src"),
            "-I" + str(source / "include"),
            str(ROOT / "validation/core_contracts/rz/test_candidate_patch_producer.cpp"),
            "-o", str(executable)], capture_output=True, text=True, timeout=120)
        (folder / "build.log").write_text(built.stdout + built.stderr)
        if built.returncode:
            raise RuntimeError(name + ": compilation failure is not a rejected mutation")
        checked = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
        (folder / "test.log").write_text(checked.stdout + checked.stderr)
        if (checked.returncode == 0) != (name == "correct"):
            raise RuntimeError(name + ": unexpected acceptance/rejection")
        results.append({"name": name, "compiled": True,
            "exit_code": checked.returncode,
            "header_sha256": hashlib.sha256(candidate.read_bytes()).hexdigest(),
            "elf_sha256": hashlib.sha256(executable.read_bytes()).hexdigest()})
    (output / "summary.json").write_text(json.dumps(
        {"status": "PASS_PRIVATE_PRODUCER_MUTATION_SENSITIVITY", "results": results},
        indent=2) + "\n")
    print("correct=PASS compiled_mutants_rejected=3")
if __name__ == "__main__":
    main()
