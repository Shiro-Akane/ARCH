#!/usr/bin/env python3
"""Compare P1 CPU orchestration against a frozen pre-refactor ARCH binary.

Reuse the production checkpoint comparator and existing runtime/restart helpers.
This is behavior-preservation evidence, not self-gravity or CUDA acceptance.
"""
from __future__ import annotations

import argparse
import copy
import json
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import validate_backend_results as runtime
import validate_cuda_amr_restart as restart
import validation_provenance as provenance

POLICY = {"rtol": 0.0, "atol": 0.0, "enuc_scale_rtol": 0.0, "dt_burn_rtol": 0.0}


def cases():
    coupled = json.loads((ROOT / "validation/gravity/coupled_cases.json").read_text())["cases"]
    for method in ("Euler", "RK2", "RK3"):
        yield {"id": f"external_{method.lower()}", "problem": "ExternalGravity",
               "input": f"validation/gravity/inputs/{method.lower()}.par", "overrides": {}}
        case = copy.deepcopy(coupled[0])
        case.update(id=f"none_amr_{method.lower()}")
        case["overrides"].update(time_integrator=method, gravity_type="none")
        yield case
    yield coupled[0]
    for order in (1, 2):
        case = copy.deepcopy(coupled[2])
        case["id"] = f"external_amr_rkl{order}"
        case["overrides"]["diff_integrator"] = f"RKL{order}"
        yield case
    yield {"id": "burn_enuc_amr", "problem": "BurnGradient",
           "input": "validation/amr/inputs/burn_enuc_amr.par", "overrides": {
               "lrefinemin": "0", "lrefinemax": "1", "regrid_interval": "1"}}


def run_case(binary, validator, output, case):
    lane = output / case["id"]
    parameter = lane / "input.par"
    runtime.render_parameter_file(ROOT / case["input"], parameter,
        backend="cpu", output_dir=lane, base_name="P1", accepted_steps=5,
        scientific_overrides={**case["overrides"], "chk_dstep": "2"})
    completed = runtime.run_arch_with_logs([str(binary), case["problem"], str(parameter)],
        source_root=ROOT, lane_root=lane, timeout=600)
    if completed.returncode or "Simulation Done. Total Steps: 5" not in completed.stdout:
        raise RuntimeError(f"{lane}: simulation failed; inspect retained logs")
    plan = runtime.validate_resolved_plan(lane / "P1_backend_plan.txt", "cpu")
    checkpoints = []
    for path in sorted(lane.glob("P1_chk_*.h5")):
        metadata = runtime.checkpoint_metadata(validator=validator, checkpoint=path,
                                              parameters=parameter, expected_steps=None)
        checkpoints.append({"path": str(path), **metadata})
    if [item["step"] for item in checkpoints] != [0, 2, 4, 5]:
        raise RuntimeError(f"{lane}: initial/intermediate/terminal checkpoint schedule drifted")
    if [item["resume_after_regrid"] for item in checkpoints] != [False, True, True, False]:
        raise RuntimeError(f"{lane}: checkpoint continuation phases drifted")
    return {"parameter": str(parameter), "parameter_sha256": provenance.sha256(parameter),
            "resolved_plan": plan, "checkpoints": checkpoints}


def compare(validator, left, right):
    result = runtime.compare_hdf5_checkpoints(Path(left), Path(right), POLICY, validator)
    if not result["passed"]:
        raise RuntimeError(f"{left} vs {right}: {result}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--checkpoint-validator", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    binaries = {name: getattr(args, name).resolve() for name in ("baseline", "candidate")}
    validator, output = args.checkpoint_validator.resolve(), args.output.resolve()
    runtime.require_empty_output_root(output)
    # Identical fixed thread count on both sides; this is not a speed benchmark.
    os.environ["OMP_NUM_THREADS"] = "2"
    os.environ["OMP_DYNAMIC"] = "FALSE"
    evidence = {"scope": "P1 CPU refactor; no self-gravity solver or CUDA execution",
        "threads": 2, "build": "Debug; OpenMP ON; CUDA OFF; KLU OFF",
        "binaries": {k: provenance.file_identity(v) for k, v in binaries.items()},
        "checkpoint_validator": provenance.file_identity(validator),
        "comparison_policy": POLICY, "cases": [], "restarts": []}
    report = output / "evidence.json"
    for case in cases():
        lanes = {name: run_case(binary, validator, output / name, case)
                 for name, binary in binaries.items()}
        comparisons = [compare(validator, a["path"], b["path"])
            for a, b in zip(lanes["baseline"]["checkpoints"], lanes["candidate"]["checkpoints"])]
        evidence["cases"].append({"id": case["id"], "input": case["input"],
            "input_sha256": provenance.sha256(ROOT / case["input"]),
            "overrides": case["overrides"], "lanes": lanes, "comparisons": comparisons})
        print(f"PASS {case['id']}: 4 checkpoints, zero field tolerance", flush=True)
        report.write_text(json.dumps(evidence, indent=2) + "\n")
        if case["id"] not in {"external_amr_rkl2", "burn_enuc_amr"}:
            continue
        # Baseline-produced checkpoint is restored by both binaries, exercising
        # backward compatibility as well as intermediate/terminal phase handling.
        canonical = Path(lanes["baseline"]["parameter"])
        source = restart.run_lane(arch=binaries["baseline"], source_root=ROOT,
            canonical_input=canonical, output_root=output / "restart", name=case["id"] + "_source",
            problem=case["problem"], backend="cpu", max_steps=3, checkpoint_validator=validator)
        for phase, checkpoint, step in (
            (True, source["checkpoint"], 2), (False, source["terminal_checkpoint"], 3)):
            restored = {name: restart.run_lane(arch=binary, source_root=ROOT,
                canonical_input=canonical, output_root=output / "restart",
                name=f"{case['id']}_{name}_{'intermediate' if phase else 'terminal'}",
                problem=case["problem"], backend="cpu", max_steps=5,
                checkpoint_validator=validator, restart_file=Path(checkpoint),
                restart_parameters=Path(source["parameter"]), restart_step=step,
                restart_phase=phase) for name, binary in binaries.items()}
            parity = compare(validator, restored["baseline"]["checkpoint"],
                                       restored["candidate"]["checkpoint"])
            # Intermediate resumes preserve counters; terminal forced-output
            # counters use the comparator's existing explicit phase adjustment.
            terminal_pair = None if phase else (Path(source["checkpoint"]),
                                                Path(source["terminal_checkpoint"]))
            uninterrupted = runtime.compare_hdf5_checkpoints(
                Path(lanes["candidate"]["checkpoints"][-1]["path"]),
                Path(restored["candidate"]["checkpoint"]), POLICY, validator,
                terminal_source_pair=terminal_pair)
            if not uninterrupted["passed"]:
                raise RuntimeError(f"uninterrupted/restart mismatch: {uninterrupted}")
            evidence["restarts"].append({"id": case["id"], "source_phase": phase,
                "source": source, "lanes": restored, "parity": parity,
                "uninterrupted": uninterrupted})
            print(f"PASS {case['id']} restart phase={phase}", flush=True)
            report.write_text(json.dumps(evidence, indent=2) + "\n")
    evidence["passed"] = True
    report.write_text(json.dumps(evidence, indent=2) + "\n")


if __name__ == "__main__":
    main()
