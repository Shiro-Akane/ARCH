#!/usr/bin/env python3
"""Small real-ARCH integration smoke; deliberately NOT release/scientific evidence.

Uses the ordinary problem registry, parameter parser, and checkpoint restart.
No fields, checkpoints, or physical source implementation are modified.
"""
from __future__ import annotations

import argparse
import csv
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time
from typing import Any

import validate_backend_results as validation
from validation_provenance import sha256

ROOT = Path(__file__).resolve().parents[1]


def cases_from(path: Path) -> list[dict[str, Any]]:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if manifest.get("schema") != 1 or manifest.get("kind") != "development-smoke":
        raise ValueError("expected a development-smoke manifest")
    cases = manifest.get("cases", [])
    identifiers: set[str] = set()
    for case in cases:
        identifier = case.get("id", "")
        if not re.fullmatch(r"[a-z0-9_]+", identifier) or identifier in identifiers:
            raise ValueError("invalid or duplicate smoke case identifier")
        identifiers.add(identifier)
        if not case.get("problem") or not case.get("input") or int(case.get("steps", 0)) < 1:
            raise ValueError("incomplete smoke case")
        if int(case.get("restart_steps", 0)) < 0:
            raise ValueError("negative restart continuation length")
    if not cases:
        raise ValueError("empty smoke case set")
    return cases


def trace_summary(path: Path, expected_steps: int, diffusion: bool) -> dict[str, Any]:
    result: dict[str, Any] = validation.validate_cuda_trace(path, expected_steps)
    with path.open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    # BackendOperation values from the shared ordinary-C++ trace contract.
    for operation, label in ((4, "hydro"), (7, "diffusion")):
        kernels = sum(int(row["kernel_count"]) for row in rows
                      if int(row["operation"]) == operation)
        result[f"{label}_kernels"] = kernels
        if (operation == 4 or diffusion) and kernels <= 0:
            raise RuntimeError(f"CUDA smoke did not execute {label} kernels")
    result["epochs"] = sorted({int(row["epoch"]) for row in rows})
    result["device_topology_changed"] = len(result["epochs"]) > 1
    return result


checkpoint_metadata = validation.checkpoint_metadata


def run_lane(*, arch: Path, checkpoint_validator: Path, source_root: Path, output_root: Path,
             case: dict[str, Any], backend: str, timeout: float,
             threads: int, restart: Path | None = None,
             restart_parameters: Path | None = None) -> dict[str, Any]:
    suffix = "resume" if restart else "source"
    name = f"{case['id']}_{backend}_{suffix}"
    lane = output_root / name
    lane.mkdir()  # No overwrite/reuse of a previous run's output.
    expected_steps = int(case["steps"]) + (int(case.get("restart_steps", 0)) if restart else 0)
    parameter = lane / "run.par"
    canonical = (source_root / case["input"]).resolve()
    record: dict[str, Any] = {
        "name": name, "backend": backend, "status": "failed", "returncode": None,
        "expected_total_steps": expected_steps, "parameter": str(parameter),
        "canonical_input": str(canonical), "restart_from": str(restart) if restart else None,
        "output_directory": str(lane), "command": [str(arch), case["problem"], str(parameter)],
    }
    started = time.monotonic()
    try:
        record["canonical_sha256"] = sha256(canonical)
        overrides = {str(key): str(value) for key, value in case.get("overrides", {}).items()}
        overrides.update({
            "compute_backend": backend, "out_dir": str(lane), "log_dir": str(lane),
            "base_name": name, "max_steps": str(expected_steps), "tmax": "1e99",
            "lrefinemin": "0", "lrefinemax": "1", "regrid_interval": "1",
            "plt_dt": "-1", "plt_dstep": "-1", "chk_dt": "-1", "chk_dstep": "-1",
            "restart": "true" if restart else "false",
        })
        if restart:
            if not restart.is_file():
                raise RuntimeError("restart checkpoint is missing")
            if restart_parameters is None or not restart_parameters.is_file():
                raise RuntimeError("restart checkpoint requires the source lane's actual parameters")
            record["restart_metadata"] = checkpoint_metadata(
                validator=checkpoint_validator, checkpoint=restart,
                parameters=restart_parameters, expected_steps=int(case["steps"]))
            overrides["restart_file"] = str(restart.resolve())
            record["restart_sha256"] = record["restart_metadata"]["checkpoint_sha256"]
            record["restart_parameters"] = str(restart_parameters)
        validation._render_parameter_overrides(canonical, parameter, overrides)
        record["effective_parameters"] = validation.read_parameter_map(parameter)
        record["parameter_sha256"] = sha256(parameter)
        environment = os.environ.copy()
        environment["OMP_NUM_THREADS"] = str(threads)
        environment["OMP_DYNAMIC"] = "FALSE"
        record["OMP_NUM_THREADS"] = threads
        with (lane / "stdout.log").open("w", encoding="utf-8") as stdout, \
             (lane / "stderr.log").open("w", encoding="utf-8") as stderr:
            completed = subprocess.run(record["command"], cwd=source_root, env=environment,
                                       stdout=stdout, stderr=stderr, timeout=timeout, check=False)
        record["returncode"] = completed.returncode
        if completed.returncode != 0:
            # In particular, CUDA unavailable/77 is a failure, never a pass/skip.
            raise RuntimeError(f"ARCH exited {completed.returncode}")
        if record["parameter_sha256"] != sha256(parameter):
            raise RuntimeError("actual smoke parameter file changed during ARCH execution")
        stdout_text = (lane / "stdout.log").read_text(encoding="utf-8")
        match = validation.STEP_RE.search(stdout_text)
        if match is None or int(match.group(1)) != expected_steps:
            raise RuntimeError("ARCH did not reach the requested total accepted steps")
        record["resolved_plan"] = validation.validate_resolved_plan(
            lane / f"{name}_backend_plan.txt", backend)
        if case.get("require_initial_refinement") and not restart:
            if "Refining initial condition (Pass" not in stdout_text:
                raise RuntimeError("curvilinear smoke did not actually create refined leaves")
        if backend == "cuda":
            record["trace"] = trace_summary(lane / f"{name}_backend_trace.tsv",
                                            expected_steps, bool(case.get("diffusion")))
            if case.get("require_device_topology_change") and not restart \
                    and not record["trace"]["device_topology_changed"]:
                raise RuntimeError("dynamic smoke did not exercise device regrid publication")
        checkpoints = sorted(lane.glob(f"{name}_chk_[0-9][0-9][0-9][0-9].h5"))
        if not checkpoints:
            raise RuntimeError("terminal checkpoint is missing")
        record["checkpoint"] = str(checkpoints[-1])
        record["checkpoint_metadata"] = checkpoint_metadata(
            validator=checkpoint_validator, checkpoint=checkpoints[-1],
            parameters=parameter, expected_steps=expected_steps)
        record["checkpoint_sha256"] = record["checkpoint_metadata"]["checkpoint_sha256"]
        if record["canonical_sha256"] != sha256(canonical):
            raise RuntimeError("canonical input changed while smoke was running")
        record["status"] = "passed"
    except (OSError, RuntimeError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
        record["error"] = str(error)
        if isinstance(error, subprocess.TimeoutExpired):
            record["timed_out"] = True
    record["elapsed_seconds"] = time.monotonic() - started
    return record


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", type=Path, required=True)
    parser.add_argument("--checkpoint-validator", type=Path, required=True,
                        help="current build's arch_cuda_single_level_validation")
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--manifest", type=Path, default=ROOT / "tests/smoke/cuda_amr_cases.json")
    parser.add_argument("--output-root", type=Path, help="must not exist, or be an empty directory")
    parser.add_argument("--case", action="append", default=[])
    parser.add_argument("--backend", choices=("cpu", "cuda"), action="append")
    parser.add_argument("--timeout", type=float, default=30.0, help="seconds per ARCH process")
    parser.add_argument("--threads", type=int, default=1)
    args = parser.parse_args(argv)
    if args.timeout <= 0 or args.threads < 1:
        parser.error("timeout and thread count must be positive")
    cases = cases_from(args.manifest.resolve())
    if args.case:
        requested = set(args.case)
        cases = [case for case in cases if case["id"] in requested]
        if {case["id"] for case in cases} != requested:
            parser.error("unknown smoke case")
    output = args.output_root.resolve() if args.output_root else Path(tempfile.mkdtemp(prefix="arch-amr-smoke-"))
    if output.exists() and any(output.iterdir()):
        parser.error("output root is not empty; previous outputs will not be overwritten")
    output.mkdir(parents=True, exist_ok=True)
    arch = args.arch.resolve()
    checkpoint_validator = args.checkpoint_validator.resolve()
    backends = list(dict.fromkeys(args.backend or ["cpu", "cuda"]))
    report: dict[str, Any] = {
        "schema": 1, "kind": "development-smoke", "scientific_validation": False,
        "scope": "process completion, explicit backend, device work/AMR trace, checkpoint restart",
        "arch": str(arch), "arch_sha256": sha256(arch) if arch.is_file() else None,
        "checkpoint_validator": str(checkpoint_validator),
        "checkpoint_validator_sha256": (sha256(checkpoint_validator)
                                          if checkpoint_validator.is_file() else None),
        "manifest_sha256": sha256(args.manifest.resolve()), "backends": backends,
        "cases": [case["id"] for case in cases], "source_root": str(args.source_root.resolve()),
        "status": "running", "lanes": [],
    }
    report_path = output / "smoke-report.json"
    def save() -> None:
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    save()
    for case in cases:
        for backend in backends:
            source = run_lane(arch=arch, checkpoint_validator=checkpoint_validator,
                              source_root=args.source_root.resolve(), output_root=output,
                              case=case, backend=backend, timeout=args.timeout, threads=args.threads)
            report["lanes"].append(source)
            save()
            print(f"{source['status']}: {source['name']}", flush=True)
            if int(case.get("restart_steps", 0)) > 0:
                if source["status"] == "passed":
                    resumed = run_lane(arch=arch, checkpoint_validator=checkpoint_validator,
                        source_root=args.source_root.resolve(), output_root=output,
                        case=case, backend=backend, timeout=args.timeout, threads=args.threads,
                        restart=Path(source["checkpoint"]),
                        restart_parameters=Path(source["parameter"]))
                else:
                    resumed = {"name": f"{case['id']}_{backend}_resume", "backend": backend,
                        "status": "failed", "returncode": None,
                        "error": "source lane failed; no verified checkpoint can be resumed"}
                report["lanes"].append(resumed)
                save()
                print(f"{resumed['status']}: {resumed['name']}", flush=True)
    binary_unchanged = arch.is_file() and report["arch_sha256"] == sha256(arch)
    report["binary_unchanged"] = binary_unchanged
    validator_unchanged = (checkpoint_validator.is_file()
        and report["checkpoint_validator_sha256"] == sha256(checkpoint_validator))
    report["checkpoint_validator_unchanged"] = validator_unchanged
    report["status"] = "passed" if binary_unchanged and validator_unchanged and all(
        lane["status"] == "passed" for lane in report["lanes"]) else "failed"
    save()
    print(f"Development smoke only: {report['status']}; report: {report_path}", flush=True)
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
