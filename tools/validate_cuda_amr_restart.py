#!/usr/bin/env python3
"""Validate dynamic-AMR checkpoint continuity across CPU and CUDA backends."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import re
import sys
from typing import Any

import validate_backend_results as backend_validation
import validation_provenance as provenance
import validation_sanitizer


STEP_RE = re.compile(r"Simulation Done\. Total Steps:\s*([0-9]+)")


def sha256(path: Path) -> str:
    return provenance.sha256(path)


def require_empty(directory: Path) -> None:
    if directory.exists() and any(directory.iterdir()):
        raise RuntimeError(f"restart evidence directory is not empty: {directory}")
    directory.mkdir(parents=True, exist_ok=True)


def select_checkpoint(records: list[tuple[Path, dict[str, Any]]], step: int,
                      phase: bool) -> tuple[Path, dict[str, Any]]:
    matches = [(path, metadata) for path, metadata in records
               if metadata["step"] == step and metadata["resume_after_regrid"] is phase]
    if len(matches) != 1:
        raise RuntimeError(f"expected one checkpoint at step {step}, phase {phase}; found {len(matches)}")
    return matches[0]


def output_index_offsets(source: dict[str, Any] | None) -> dict[str, int]:
    if source is None:
        return {"checkpoint": 0, "plot": 0}
    before, after = source["checkpoint_metadata"], source["terminal_metadata"]
    offsets = {}
    for key, field in (("checkpoint", "chk_file_index"), ("plot", "plt_file_index")):
        if any(type(item.get(field)) is not int or item[field] < 0 for item in (before, after)):
            raise RuntimeError("restart source output counters are missing or invalid")
        offsets[key] = after[field] - before[field]
    # The source protocol adds one forced terminal CHK/PLT, not a free offset.
    if offsets != {"checkpoint": 1, "plot": 1}:
        raise RuntimeError("terminal checkpoint must add exactly one forced CHK/PLT")
    return offsets


def run_lane(
    *, arch: Path, source_root: Path, canonical_input: Path,
    output_root: Path, name: str, problem: str, backend: str, max_steps: int,
    checkpoint_validator: Path,
    restart_file: Path | None = None,
    restart_parameters: Path | None = None,
    restart_step: int | None = None,
    restart_phase: bool | None = None,
    terminal_time: float | None = None,
    sanitizer: validation_sanitizer.CudaSanitizer | None = None,
) -> dict[str, Any]:
    if terminal_time is not None and (
            not math.isfinite(terminal_time) or terminal_time <= 0.0):
        raise ValueError("restart terminal time must be finite and positive")
    if max_steps <= 0 and not (max_steps == -1 and terminal_time is not None):
        raise ValueError("restart needs a positive step count or a prescribed terminal time")
    lane = output_root / name
    lane.mkdir(parents=True)
    base_name = f"RestartAMR_{name}"
    parameter = lane / f"{base_name}.par"
    overrides = {
        "compute_backend": backend,
        "out_dir": str(lane),
        "base_name": base_name,
        "max_steps": str(max_steps),
        "tmax": "1e99" if terminal_time is None else repr(terminal_time),
        "chk_dt": "-1",
        "chk_dstep": "2",
        "plt_dt": "-1",
        "plt_dstep": "-1",
        "lrefinemin": "0",
        "lrefinemax": "1",
        "regrid_interval": "1",
        "restart": "true" if restart_file else "false",
    }
    if restart_file:
        if restart_parameters is None or restart_step is None or type(restart_phase) is not bool:
            raise RuntimeError("restart requires actual source parameters, step and phase")
        restored = backend_validation.checkpoint_metadata(validator=checkpoint_validator,
            checkpoint=restart_file, parameters=restart_parameters, expected_steps=restart_step)
        if restored["resume_after_regrid"] is not restart_phase:
            raise RuntimeError("restart source checkpoint phase mismatch")
        if terminal_time is not None and terminal_time <= restored["time"]:
            raise ValueError("restart terminal time must follow its source checkpoint")
        overrides["restart_file"] = str(restart_file.resolve())
    backend_validation._render_parameter_overrides(
        canonical_input, parameter, overrides)
    completed = backend_validation.run_arch_with_logs(
        [str(arch), problem, str(parameter)],
        source_root=source_root, lane_root=lane, timeout=1200,
        sanitizer=sanitizer if backend == "cuda" else None)
    if completed.returncode != 0:
        raise RuntimeError(f"restart lane {name} failed: {completed.returncode}")
    match = STEP_RE.search(completed.stdout)
    if match is None:
        raise RuntimeError(f"restart lane {name} has no completed-step count")
    completed_steps = int(match.group(1))
    if terminal_time is None and completed_steps != max_steps:
        raise RuntimeError(f"restart lane {name} accepted-step count drifted")
    if restart_file:
        witness = f"[IO] Restored CHK: {restart_file.resolve()} at step {restart_step} with "
        if witness not in completed.stdout:
            raise RuntimeError("restart lane did not restore its declared checkpoint")
    plan = lane / f"{base_name}_backend_plan.txt"
    resolved_plan = backend_validation.validate_resolved_plan(plan, backend)
    intermediate_source = terminal_time is None and max_steps == 3
    checkpoint_step = 2 if intermediate_source else completed_steps
    records = [(path, backend_validation.checkpoint_metadata(validator=checkpoint_validator,
        checkpoint=path, parameters=parameter, expected_steps=None))
        for path in sorted(lane.glob(f"{base_name}_chk_*.h5"))]
    checkpoint, metadata = select_checkpoint(records, checkpoint_step, intermediate_source)
    if terminal_time is not None and metadata["time"] != terminal_time:
        raise RuntimeError(f"restart lane {name} did not reach its prescribed physical time")
    result = {
        "name": name,
        "backend": backend,
        "steps": checkpoint_step,
        "run_completed_steps": completed_steps,
        "checkpoint_metadata": metadata,
        "parameter": str(parameter),
        "checkpoint": str(checkpoint),
        "checkpoint_sha256": sha256(checkpoint),
        "plan": str(plan),
        "resolved_plan": resolved_plan,
        "sanitizer": sanitizer.evidence(lane) if sanitizer and backend == "cuda" else None,
    }
    if intermediate_source:
        terminal, terminal_metadata = select_checkpoint(records, max_steps, False)
        if metadata["resume_after_regrid"] is not True or terminal_metadata["resume_after_regrid"] is not False:
            raise RuntimeError("source must contain intermediate and terminal restart phases")
        result["terminal_checkpoint"] = str(terminal)
        result["terminal_metadata"] = terminal_metadata
    if restart_file:
        if restored["checkpoint_sha256"] != sha256(restart_file):
            raise RuntimeError("restart source changed during execution")
        result["restored_from"] = {"checkpoint": str(restart_file.resolve()),
                                   "parameters": str(restart_parameters), **restored}
        result["restore_confirmed"] = True
    return result


def comparison_policy() -> dict[str, Any]:
    # ENUC is a differenced diagnostic: a few ulps in two large internal
    # energies are divided by a very small burn half-step. Gate it against the
    # field peak while retaining tight pointwise budgets for every conserved
    # field and an exact topology check in the C++ validator.
    return {
        "rtol": 5.0e-9,
        "atol": 5.0e-12,
        "enuc_scale_rtol": 1.0e-3,
        "dt_burn_rtol": 1.0e-3,
        "fields": [
            "rho", "mom_u", "mom_v", "mom_w", "eng", "enuc_rate",
            "rhoX", "X",
        ],
    }


def compare(
    validator: Path, reference: dict[str, Any], candidate: dict[str, Any],
    terminal_source: dict[str, Any] | None = None,
) -> dict[str, Any]:
    policy = comparison_policy()
    result = backend_validation.compare_hdf5_checkpoints(
        Path(reference["checkpoint"]), Path(candidate["checkpoint"]),
        policy,
        validator, terminal_source_pair=None if terminal_source is None else (
            Path(terminal_source["checkpoint"]), Path(terminal_source["terminal_checkpoint"])))
    if result.get("passed") and result.get("output_index_offsets") != output_index_offsets(terminal_source):
        raise RuntimeError("restart comparator output history differs from actual source")
    if not result["passed"]:
        raise RuntimeError(
            f"restart mismatch {candidate['name']}: {result['first_mismatch']}")
    if int(result.get("max_level", 0)) <= int(result.get("min_level", 0)):
        raise RuntimeError(
            f"restart lane {candidate['name']} did not retain mixed AMR levels")
    result["tolerance"] = policy
    return result


def lane_contract() -> list[tuple[str, str, str | None]]:
    intermediate = [
        ("cpu_to_cpu", "cpu", "cpu_source"),
        ("cuda_to_cuda", "cuda", "cuda_source"),
        ("cpu_to_cuda", "cuda", "cpu_source"),
        ("cuda_to_cpu", "cpu", "cuda_source"),
    ]
    return intermediate + [(name + "_terminal", backend, source)
                           for name, backend, source in intermediate]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--arch", type=Path)
    parser.add_argument("--checkpoint-validator", type=Path)
    parser.add_argument("--source-root", type=Path, default=Path.cwd())
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--configuration", help="required for multi-config builds")
    parser.add_argument(
        "--input", type=Path,
        default=Path("validation/amr/inputs/smooth_amr80_l1.par"))
    parser.add_argument("--problem", default="SmoothAdvection")
    parser.add_argument("--output-root", type=Path)
    parser.add_argument("--unit-test", action="store_true")
    validation_sanitizer.add_arguments(parser)
    args = parser.parse_args(argv)
    if args.unit_test:
        print(json.dumps({"schema": 1, "routes": len(lane_contract())}))
        return 0
    if args.arch is None or args.checkpoint_validator is None \
            or args.output_root is None or args.build_dir is None:
        parser.error("runtime validation requires binaries, --build-dir and an output root")

    source_root = args.source_root.resolve()
    canonical_input = (source_root / args.input).resolve()
    output_root = args.output_root.resolve()
    require_empty(output_root)
    identity_arguments = {
        "arch": args.arch.resolve(),
        "checkpoint_validator": args.checkpoint_validator.resolve(),
        "source_root": source_root,
        "build_dir": args.build_dir.resolve(),
        "configuration": args.configuration,
    }
    identity = provenance.capture(**identity_arguments)
    sanitizer = validation_sanitizer.from_arguments(args)
    input_sha256 = sha256(canonical_input)
    runtime_inputs = provenance.runtime_inputs(
        parameter_file=canonical_input, working_directory=source_root,
        parameter_reader=backend_validation.read_parameter_map)
    continuous = {
        backend: run_lane(
            arch=args.arch.resolve(), source_root=source_root,
            canonical_input=canonical_input, output_root=output_root,
            name=f"{backend}_continuous", problem=args.problem,
            backend=backend, max_steps=4, checkpoint_validator=args.checkpoint_validator.resolve(),
            sanitizer=sanitizer)
        for backend in ("cpu", "cuda")
    }
    sources = {
        backend: run_lane(
            arch=args.arch.resolve(), source_root=source_root,
            canonical_input=canonical_input, output_root=output_root,
            name=f"{backend}_source", problem=args.problem,
            backend=backend, max_steps=3, checkpoint_validator=args.checkpoint_validator.resolve(),
            sanitizer=sanitizer)
        for backend in ("cpu", "cuda")
    }

    comparisons: list[dict[str, Any]] = []
    cpu_cuda = compare(
        args.checkpoint_validator.resolve(), continuous["cpu"],
        continuous["cuda"])
    comparisons.append({"route": "cpu_vs_cuda_continuous", **cpu_cuda})
    resumed: list[dict[str, Any]] = []
    for name, destination, source_name in lane_contract():
        source_backend = source_name.removesuffix("_source")
        source = sources[source_backend]
        terminal = name.endswith("_terminal")
        lane = run_lane(
            arch=args.arch.resolve(), source_root=source_root,
            canonical_input=canonical_input, output_root=output_root,
            name=name, problem=args.problem,
            backend=destination, max_steps=4,
            checkpoint_validator=args.checkpoint_validator.resolve(),
            restart_file=Path(source["terminal_checkpoint" if terminal else "checkpoint"]),
            restart_parameters=Path(source["parameter"]), restart_step=3 if terminal else 2,
            restart_phase=not terminal, sanitizer=sanitizer)
        resumed.append(lane)
        metrics = compare(
            args.checkpoint_validator.resolve(), continuous[destination], lane,
            source if terminal else None)
        comparisons.append({"route": name, **metrics})

    evidence = {
        "schema": 1,
        "problem": args.problem,
        "input": str(canonical_input),
        "input_sha256": input_sha256,
        "runtime_inputs": runtime_inputs,
        "continuous": continuous,
        "sources": sources,
        "resumed": resumed,
        "comparisons": comparisons,
    }
    evidence_path = output_root / "restart-validation-evidence.json"
    if input_sha256 != sha256(canonical_input):
        raise RuntimeError("restart input changed during execution")
    if runtime_inputs != provenance.runtime_inputs(
        parameter_file=canonical_input, working_directory=source_root,
        parameter_reader=backend_validation.read_parameter_map
    ):
        raise RuntimeError("restart runtime dependencies changed during execution")
    provenance.write_evidence(evidence_path, evidence, identity, **identity_arguments)
    print(json.dumps({"status": "pass", "evidence": str(evidence_path)}))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"CUDA AMR restart validation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
