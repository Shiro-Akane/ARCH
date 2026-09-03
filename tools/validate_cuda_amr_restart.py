#!/usr/bin/env python3
"""Validate dynamic-AMR checkpoint continuity across CPU and CUDA backends."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Any

import validate_backend_results as backend_validation


STEP_RE = re.compile(r"Simulation Done\. Total Steps:\s*([0-9]+)")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_empty(directory: Path) -> None:
    if directory.exists() and any(directory.iterdir()):
        raise RuntimeError(f"restart evidence directory is not empty: {directory}")
    directory.mkdir(parents=True, exist_ok=True)


def run_lane(
    *, arch: Path, source_root: Path, canonical_input: Path,
    output_root: Path, name: str, problem: str, backend: str, max_steps: int,
    restart_file: Path | None = None,
) -> dict[str, Any]:
    lane = output_root / name
    lane.mkdir(parents=True)
    base_name = f"RestartAMR_{name}"
    parameter = lane / f"{base_name}.par"
    overrides = {
        "compute_backend": backend,
        "out_dir": str(lane),
        "base_name": base_name,
        "max_steps": str(max_steps),
        "tmax": "1e99",
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
        overrides["restart_file"] = str(restart_file.resolve())
    backend_validation._render_parameter_overrides(
        canonical_input, parameter, overrides)
    completed = subprocess.run(
        [str(arch), problem, str(parameter)],
        cwd=source_root, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, timeout=1200, check=False)
    (lane / "arch.stdout").write_text(completed.stdout, encoding="utf-8")
    (lane / "arch.stderr").write_text(completed.stderr, encoding="utf-8")
    if completed.returncode != 0:
        raise RuntimeError(f"restart lane {name} failed: {completed.returncode}")
    match = STEP_RE.search(completed.stdout)
    if match is None or int(match.group(1)) != max_steps:
        raise RuntimeError(f"restart lane {name} accepted-step count drifted")
    plan = lane / f"{base_name}_backend_plan.txt"
    backend_validation.validate_resolved_plan(plan, backend)
    expected_index = 1 if max_steps == 3 else 2
    checkpoint = lane / f"{base_name}_chk_{expected_index:04d}.h5"
    if not checkpoint.is_file():
        raise RuntimeError(f"restart lane {name} checkpoint is missing")
    return {
        "name": name,
        "backend": backend,
        "steps": max_steps,
        "parameter": str(parameter),
        "checkpoint": str(checkpoint),
        "checkpoint_sha256": sha256(checkpoint),
        "plan": str(plan),
    }


def compare(
    validator: Path, reference: dict[str, Any], candidate: dict[str, Any]
) -> dict[str, Any]:
    # ENUC is a differenced diagnostic: a few ulps in two large internal
    # energies are divided by a very small burn half-step. Gate it against the
    # field peak while retaining tight pointwise budgets for every conserved
    # field and an exact topology check in the C++ validator.
    policy = {
        "rtol": 5.0e-9,
        "atol": 5.0e-12,
        "enuc_scale_rtol": 1.0e-3,
        "dt_burn_rtol": 1.0e-3,
        "fields": [
            "rho", "mom_u", "mom_v", "mom_w", "eng", "enuc_rate",
            "rhoX",
        ],
    }
    result = backend_validation.compare_hdf5_checkpoints(
        Path(reference["checkpoint"]), Path(candidate["checkpoint"]),
        policy,
        validator)
    if not result["passed"]:
        raise RuntimeError(
            f"restart mismatch {candidate['name']}: {result['first_mismatch']}")
    if int(result.get("max_level", 0)) <= int(result.get("min_level", 0)):
        raise RuntimeError(
            f"restart lane {candidate['name']} did not retain mixed AMR levels")
    result["tolerance"] = policy
    return result


def lane_contract() -> list[tuple[str, str, str | None]]:
    return [
        ("cpu_to_cpu", "cpu", "cpu_source"),
        ("cuda_to_cuda", "cuda", "cuda_source"),
        ("cpu_to_cuda", "cuda", "cpu_source"),
        ("cuda_to_cpu", "cpu", "cuda_source"),
    ]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--arch", type=Path)
    parser.add_argument("--checkpoint-validator", type=Path)
    parser.add_argument("--source-root", type=Path, default=Path.cwd())
    parser.add_argument(
        "--input", type=Path,
        default=Path("validation/amr/inputs/smooth_amr80_l1.par"))
    parser.add_argument("--problem", default="SmoothAdvection")
    parser.add_argument("--output-root", type=Path)
    parser.add_argument("--unit-test", action="store_true")
    args = parser.parse_args(argv)
    if args.unit_test:
        print(json.dumps({"schema": 1, "routes": len(lane_contract())}))
        return 0
    if args.arch is None or args.checkpoint_validator is None \
            or args.output_root is None:
        parser.error("runtime validation requires binaries and an output root")

    source_root = args.source_root.resolve()
    canonical_input = (source_root / args.input).resolve()
    output_root = args.output_root.resolve()
    require_empty(output_root)
    continuous = {
        backend: run_lane(
            arch=args.arch.resolve(), source_root=source_root,
            canonical_input=canonical_input, output_root=output_root,
            name=f"{backend}_continuous", problem=args.problem,
            backend=backend, max_steps=4)
        for backend in ("cpu", "cuda")
    }
    sources = {
        backend: run_lane(
            arch=args.arch.resolve(), source_root=source_root,
            canonical_input=canonical_input, output_root=output_root,
            name=f"{backend}_source", problem=args.problem,
            backend=backend, max_steps=3)
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
        lane = run_lane(
            arch=args.arch.resolve(), source_root=source_root,
            canonical_input=canonical_input, output_root=output_root,
            name=name, problem=args.problem,
            backend=destination, max_steps=4,
            restart_file=Path(sources[source_backend]["checkpoint"]))
        resumed.append(lane)
        metrics = compare(
            args.checkpoint_validator.resolve(), continuous[destination], lane)
        comparisons.append({"route": name, **metrics})

    evidence = {
        "schema": 1,
        "problem": args.problem,
        "input": str(canonical_input),
        "input_sha256": sha256(canonical_input),
        "binary_sha256": sha256(args.arch.resolve()),
        "continuous": continuous,
        "sources": sources,
        "resumed": resumed,
        "comparisons": comparisons,
    }
    evidence_path = output_root / "restart-validation-evidence.json"
    evidence_path.write_text(
        json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": "pass", "evidence": str(evidence_path)}))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"CUDA AMR restart validation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
