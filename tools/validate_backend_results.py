#!/usr/bin/env python3
"""Run and compare canonical CPU/CUDA backend validation cases.

Synthetic JSON is deliberately confined to ``--unit-test``.  Release evidence
always comes from ARCH plan/trace sidecars and project HDF5 checkpoints.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import sys
from typing import Any


PARAMETER_RE = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$")
STEP_RE = re.compile(r"Simulation Done\. Total Steps:\s*([0-9]+)")


def load_manifest(path: Path) -> dict[str, Any]:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if manifest.get("schema") != 1 or not isinstance(manifest.get("cases"), list):
        raise RuntimeError("unsupported backend validation manifest")
    identifiers: set[str] = set()
    for case in manifest["cases"]:
        identifier = case.get("id")
        if not isinstance(identifier, str) or not identifier or identifier in identifiers:
            raise RuntimeError("backend validation case IDs are invalid or duplicated")
        identifiers.add(identifier)
        if "input" not in case or "cpu_input" in case or "cuda_input" in case:
            raise RuntimeError("each case must own one canonical input")
        policy = case.get("reduction_policy")
        if not isinstance(policy, dict) or "rtol" not in policy or "atol" not in policy:
            raise RuntimeError("case reduction policy is incomplete")
    return manifest


def select_cases(manifest: dict[str, Any], requested: list[str]) -> list[dict[str, Any]]:
    selected = set(requested)
    cases = [
        case for case in manifest["cases"]
        if not selected or case["id"] in selected
    ]
    if selected and selected != {case["id"] for case in cases}:
        raise RuntimeError("requested backend validation case is absent")
    return cases


def read_parameter_map(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.split("#", 1)[0]
        match = PARAMETER_RE.match(stripped)
        if match:
            result[match.group(1)] = match.group(2)
    return result


def render_parameter_file(
    source: Path,
    destination: Path,
    *,
    backend: str,
    output_dir: Path,
    base_name: str,
    accepted_steps: int,
    scientific_overrides: dict[str, str] | None = None,
) -> None:
    if backend not in {"cpu", "cuda"} or accepted_steps <= 0:
        raise RuntimeError("invalid backend execution override")
    overrides = {
        "compute_backend": backend,
        "out_dir": str(output_dir),
        "base_name": base_name,
        "max_steps": str(accepted_steps),
        "tmax": "1e99",
        "chk_dt": "-1",
        "chk_dstep": str(accepted_steps),
        "plt_dt": "-1",
        "plt_dstep": "-1",
        "restart": "false",
        "lrefinemin": "0",
        "lrefinemax": "0",
    }
    if scientific_overrides:
        overrides.update({key: str(value) for key, value in scientific_overrides.items()})
    _render_parameter_overrides(source, destination, overrides)


def render_terminal_parameter_file(
    source: Path,
    destination: Path,
    *,
    backend: str,
    output_dir: Path,
    base_name: str,
    terminal_time: float,
    scientific_overrides: dict[str, str] | None = None,
) -> None:
    if backend not in {"cpu", "cuda"} or not math.isfinite(terminal_time) \
            or terminal_time <= 0.0:
        raise RuntimeError("invalid terminal scientific execution override")
    value = repr(terminal_time)
    overrides = {
        "compute_backend": backend,
        "out_dir": str(output_dir),
        "base_name": base_name,
        "max_steps": "-1",
        "tmax": value,
        "chk_dt": value,
        "chk_dstep": "-1",
        "plt_dt": "-1",
        "plt_dstep": "-1",
        "restart": "false",
        "lrefinemin": "0",
        "lrefinemax": "0",
    }
    if scientific_overrides:
        overrides.update({key: str(value) for key, value in scientific_overrides.items()})
    _render_parameter_overrides(source, destination, overrides)


def _render_parameter_overrides(
    source: Path, destination: Path, overrides: dict[str, str]
) -> None:
    seen: set[str] = set()
    output: list[str] = []
    for line in source.read_text(encoding="utf-8").splitlines():
        match = PARAMETER_RE.match(line.split("#", 1)[0])
        if match and match.group(1) in overrides:
            key = match.group(1)
            output.append(f"{key} = {overrides[key]}")
            seen.add(key)
        else:
            output.append(line)
    for key, value in overrides.items():
        if key not in seen:
            output.append(f"{key} = {value}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("\n".join(output) + "\n", encoding="utf-8")


def read_plan(path: Path) -> dict[str, str]:
    if not path.is_file():
        raise RuntimeError(f"missing resolved backend plan: {path}")
    result: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            result[key.strip()] = value.strip()
    return result


def validate_resolved_plan(path: Path, expected_backend: str) -> dict[str, str]:
    plan = read_plan(path)
    requested = plan.get("requested")
    resolved = plan.get("resolved")
    fallback = plan.get("fallback_reason", "")
    if requested != expected_backend or resolved != expected_backend:
        raise RuntimeError(
            f"resolved CUDA/backend mismatch: requested={requested}, resolved={resolved}")
    if expected_backend == "cuda" and fallback:
        raise RuntimeError(f"CUDA lane used fallback: {fallback}")
    return plan


def read_synthetic_checkpoint(path: Path, *, unit_test: bool) -> dict[str, Any]:
    if not unit_test:
        raise RuntimeError("synthetic checkpoints require explicit unit-test mode")
    payload = json.loads(path.read_text(encoding="utf-8"))
    if payload.get("synthetic") is not True or not isinstance(payload.get("fields"), dict):
        raise RuntimeError("malformed synthetic checkpoint")
    return payload["fields"]


def compare_numeric_fields(
    reference_fields: dict[str, list[float]],
    candidate_fields: dict[str, list[float]],
    policy: dict[str, Any],
) -> dict[str, Any]:
    required = policy.get("fields") or sorted(reference_fields)
    rtol = float(policy["rtol"])
    atol = float(policy["atol"])
    result: dict[str, Any] = {
        "passed": True,
        "first_mismatch": None,
        "max_abs": {},
        "max_rel": {},
    }
    for field in required:
        if field not in reference_fields or field not in candidate_fields:
            result["passed"] = False
            result["first_mismatch"] = {"field": field, "reason": "missing"}
            return result
        left = [float(value) for value in reference_fields[field]]
        right = [float(value) for value in candidate_fields[field]]
        if len(left) != len(right) or not all(map(math.isfinite, left + right)):
            result["passed"] = False
            result["first_mismatch"] = {"field": field, "reason": "shape/nonfinite"}
            return result
        max_abs = 0.0
        max_rel = 0.0
        for index, (reference, candidate) in enumerate(zip(left, right)):
            absolute = abs(reference - candidate)
            scale = max(abs(reference), abs(candidate))
            relative = 0.0 if scale == 0.0 else absolute / scale
            max_abs = max(max_abs, absolute)
            max_rel = max(max_rel, relative)
            if absolute > atol + rtol * abs(reference):
                result["passed"] = False
                result["first_mismatch"] = {
                    "field": field,
                    "index": index,
                    "reference": reference,
                    "candidate": candidate,
                }
                return result
        result["max_abs"][field] = max_abs
        result["max_rel"][field] = max_rel
    return result


def compare_hdf5_checkpoints(
    reference: Path, candidate: Path, policy: dict[str, Any],
    checkpoint_validator: Path
) -> dict[str, Any]:
    completed = subprocess.run(
        [str(checkpoint_validator), "--compare", str(reference), str(candidate),
         str(policy["rtol"]), str(policy["atol"])],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=120, check=False)
    if completed.returncode != 0:
        return {
            "passed": False,
            "first_mismatch": {
                "field": "checkpoint",
                "reason": completed.stderr.strip() or completed.stdout.strip(),
            },
        }
    lines = [line for line in completed.stdout.splitlines() if line.startswith("{")]
    if not lines:
        raise RuntimeError("checkpoint validator produced no JSON summary")
    result = json.loads(lines[-1])
    result["passed"] = result.get("status") == "pass"
    return result


def validate_qualification_metrics(
    metrics: dict[str, Any], qualification: dict[str, Any], mode: str,
    *, scientific: bool = False
) -> None:
    if metrics["min_rho"] <= 0.0 or metrics["min_eng"] <= 0.0:
        raise RuntimeError("scientific positivity gate failed")
    if "l1_max" in qualification and metrics["l1"] > qualification["l1_max"]:
        raise RuntimeError(
            f"scientific L1 gate failed: {metrics['l1']} > {qualification['l1_max']}")
    if mode == "smooth":
        drift = abs(metrics["mean"] - 1.0)
        if drift > qualification.get("mass_relative_drift_max", math.inf):
            raise RuntimeError(f"hydro mass drift gate failed: {drift}")
        expected = {
            "mass": 1.0,
            "mom_u": 1.0,
            "mom_v": 0.0,
            "mom_w": 0.0,
            "eng_total": 3.0,
        }
        limits = {
            "mass": qualification.get("mass_absolute_drift_max", math.inf),
            "mom_u": qualification.get("momentum_absolute_drift_max", math.inf),
            "mom_v": qualification.get("momentum_absolute_drift_max", math.inf),
            "mom_w": qualification.get("momentum_absolute_drift_max", math.inf),
            "eng_total": qualification.get("energy_absolute_drift_max", math.inf),
        }
        for field, reference in expected.items():
            drift = abs(float(metrics[field]) - reference)
            if drift > limits[field]:
                raise RuntimeError(
                    f"hydro conservation gate failed for {field}: {drift}")
    elif mode == "diffusion":
        drift = abs(metrics["mean"] - 0.5)
        if drift > qualification["mean_relative_drift_max"]:
            raise RuntimeError(f"diffusion mean drift gate failed: {drift}")
        lower, upper = qualification["fraction_bounds"]
        if metrics.get("min_fraction", lower) < lower or metrics.get("max_fraction", upper) > upper:
            raise RuntimeError("diffusion fraction bound gate failed")
    elif mode == "sod":
        if scientific:
            if metrics["l2"] > qualification["l2_max"]:
                raise RuntimeError(
                    f"Sod L2 gate failed: {metrics['l2']} > {qualification['l2_max']}")
            if metrics["shock_position_error_cells"] \
                    > qualification["shock_position_cells_max"]:
                raise RuntimeError("Sod shock-position gate failed")
    elif metrics["species_sum_error"] > qualification["species_sum_atol"]:
        raise RuntimeError("burn species normalization gate failed")


def validate_resolution_groups(
    cases: list[dict[str, Any]], results: list[dict[str, Any]]
) -> dict[str, Any]:
    """Validate pre-frozen scientific convergence gates across resolutions."""
    by_id = {result["id"]: result for result in results}
    grouped: dict[str, list[dict[str, Any]]] = {}
    for case in cases:
        group = case.get("qualification_group")
        if group is not None:
            grouped.setdefault(str(group), []).append(case)
    summaries: dict[str, Any] = {}
    for group, members in grouped.items():
        members.sort(key=lambda item: int(item["resolution"]))
        resolutions = [int(item["resolution"]) for item in members]
        if len(members) < 3 or any(
            fine <= coarse for coarse, fine in zip(resolutions, resolutions[1:])
        ):
            raise RuntimeError(f"{group} convergence resolutions are invalid")
        qualification = members[0]["qualification"]
        minimum_l1 = float(qualification["minimum_l1_order"])
        minimum_l2 = float(qualification["minimum_l2_order"])
        orders: dict[str, dict[str, list[float]]] = {}
        for backend in ("cpu", "cuda"):
            metrics = [
                by_id[item["id"]]["scientific"][f"{backend}_qualification"]
                for item in members
            ]
            backend_orders = {"l1": [], "l2": []}
            for field, minimum in (("l1", minimum_l1), ("l2", minimum_l2)):
                errors = [float(metric[field]) for metric in metrics]
                if not all(math.isfinite(error) and error > 0.0 for error in errors):
                    raise RuntimeError(f"{group} {backend} {field.upper()} is invalid")
                for index in range(len(errors) - 1):
                    order = math.log(errors[index] / errors[index + 1]) / math.log(
                        resolutions[index + 1] / resolutions[index])
                    backend_orders[field].append(order)
                    if order < minimum:
                        raise RuntimeError(
                            f"{group} {backend} {field.upper()} convergence gate failed: "
                            f"{order} < {minimum}")
            orders[backend] = backend_orders
        summaries[group] = {"resolutions": resolutions, "orders": orders}
    return summaries


def qualify_checkpoint(
    checkpoint_validator: Path, checkpoint: Path, case: dict[str, Any],
    *, scientific: bool = False
) -> dict[str, Any]:
    qualification = case.get("qualification")
    if not qualification:
        return {"status": "not-requested"}
    reference = qualification["reference"]
    if reference == "periodic_entropy_wave":
        mode = "smooth"
    elif reference == "periodic_diffusion_mode":
        mode = "diffusion"
    elif reference == "sod_exact_riemann":
        mode = "sod"
    elif reference == "cpu_and_network_conservation":
        mode = "burn"
    else:
        raise RuntimeError(f"unknown independent reference: {reference}")
    completed = subprocess.run(
        [str(checkpoint_validator), "--qualify", str(checkpoint), mode],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=120, check=False)
    if completed.returncode != 0:
        raise RuntimeError(
            f"scientific qualification failed: {completed.stderr.strip()}")
    lines = [line for line in completed.stdout.splitlines() if line.startswith("{")]
    if not lines:
        raise RuntimeError("scientific qualifier produced no JSON summary")
    metrics = json.loads(lines[-1])
    validate_qualification_metrics(
        metrics, qualification, mode, scientific=scientific)
    return metrics


def validate_cuda_trace(path: Path, expected_steps: int) -> dict[str, int]:
    if not path.is_file():
        raise RuntimeError("CUDA lane did not produce a backend trace")
    lines = [line for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]
    if len(lines) <= 1:
        raise RuntimeError("CUDA backend trace is empty")
    header = lines[0].split("\t")
    required = {"macro_step", "operation", "interior_version", "ghost_version",
                "ghost_source_version", "interior_pending", "ghost_pending"}
    if not required.issubset(header):
        raise RuntimeError("CUDA backend trace schema drifted")
    rows = [dict(zip(header, line.split("\t"))) for line in lines[1:]]
    if max(int(row["macro_step"]) for row in rows) < expected_steps:
        raise RuntimeError("CUDA backend trace did not reach the accepted-step checkpoint")
    for row in rows:
        if row["interior_pending"] != "0" or row["ghost_pending"] != "0":
            raise RuntimeError("CUDA backend trace contains an unfinished transfer")
        ghost = int(row["ghost_version"])
        if ghost and ghost != int(row["ghost_source_version"]):
            raise RuntimeError("CUDA backend trace contains a stale ghost publication")
    return {"records": len(rows)}


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_empty_output_root(path: Path) -> None:
    if path.exists() and (not path.is_dir() or any(path.iterdir())):
        raise RuntimeError(f"release evidence root must be empty: {path}")


def run_arch_lane(
    arch: Path, source_root: Path, case: dict[str, Any], backend: str,
    steps: int, output_root: Path
) -> dict[str, Any]:
    lane_root = output_root / case["id"] / f"step-{steps}" / backend
    lane_root.mkdir(parents=True, exist_ok=True)
    base_name = f"{case['id']}_{backend}_s{steps}"
    parameter = lane_root / f"{base_name}.par"
    render_parameter_file(
        source_root / case["input"], parameter, backend=backend,
        output_dir=lane_root, base_name=base_name, accepted_steps=steps,
        scientific_overrides=case.get("overrides", {}))
    completed = subprocess.run(
        [str(arch), case["problem"], str(parameter)], cwd=source_root,
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=int(case.get("timeout_seconds", 600)), check=False)
    (lane_root / "arch.stdout").write_text(completed.stdout, encoding="utf-8")
    (lane_root / "arch.stderr").write_text(completed.stderr, encoding="utf-8")
    if completed.returncode != 0:
        raise RuntimeError(
            f"{case['id']} {backend} step {steps} failed: {completed.returncode}")
    match = STEP_RE.search(completed.stdout)
    if match is None or int(match.group(1)) != steps:
        raise RuntimeError(f"{case['id']} {backend} accepted-step count drifted")
    prefix = lane_root / base_name
    plan = prefix.with_name(prefix.name + "_backend_plan.txt")
    checkpoint = prefix.with_name(prefix.name + "_chk_0001.h5")
    validate_resolved_plan(plan, backend)
    trace = prefix.with_name(prefix.name + "_backend_trace.tsv")
    trace_summary = validate_cuda_trace(trace, steps) if backend == "cuda" else None
    if not checkpoint.is_file():
        raise RuntimeError(f"missing HDF5 checkpoint: {checkpoint}")
    return {
        "backend": backend,
        "steps": steps,
        "checkpoint": checkpoint,
        "checkpoint_sha256": _sha256(checkpoint),
        "plan": plan,
        "trace": trace if backend == "cuda" else None,
        "trace_summary": trace_summary,
    }


def run_arch_terminal_lane(
    arch: Path, source_root: Path, case: dict[str, Any], backend: str,
    terminal_time: float, output_root: Path
) -> dict[str, Any]:
    lane_root = output_root / case["id"] / "scientific" / backend
    lane_root.mkdir(parents=True, exist_ok=True)
    base_name = f"{case['id']}_{backend}_scientific"
    parameter = lane_root / f"{base_name}.par"
    render_terminal_parameter_file(
        source_root / case["input"], parameter, backend=backend,
        output_dir=lane_root, base_name=base_name,
        terminal_time=terminal_time,
        scientific_overrides=case.get("overrides", {}))
    completed = subprocess.run(
        [str(arch), case["problem"], str(parameter)], cwd=source_root,
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=int(case.get("timeout_seconds", 600)), check=False)
    (lane_root / "arch.stdout").write_text(completed.stdout, encoding="utf-8")
    (lane_root / "arch.stderr").write_text(completed.stderr, encoding="utf-8")
    if completed.returncode != 0:
        raise RuntimeError(
            f"{case['id']} {backend} scientific run failed: {completed.returncode}")
    match = STEP_RE.search(completed.stdout)
    if match is None or int(match.group(1)) <= 0:
        raise RuntimeError(f"{case['id']} {backend} scientific step count is invalid")
    accepted_steps = int(match.group(1))
    prefix = lane_root / base_name
    plan = prefix.with_name(prefix.name + "_backend_plan.txt")
    checkpoint = prefix.with_name(prefix.name + "_chk_0001.h5")
    validate_resolved_plan(plan, backend)
    trace = prefix.with_name(prefix.name + "_backend_trace.tsv")
    trace_summary = (
        validate_cuda_trace(trace, accepted_steps) if backend == "cuda" else None)
    if not checkpoint.is_file():
        raise RuntimeError(f"missing scientific HDF5 checkpoint: {checkpoint}")
    return {
        "backend": backend,
        "steps": accepted_steps,
        "terminal_time": terminal_time,
        "checkpoint": checkpoint,
        "checkpoint_sha256": _sha256(checkpoint),
        "plan": plan,
        "trace": trace if backend == "cuda" else None,
        "trace_summary": trace_summary,
    }


def validate_scientific_steps(
    cpu: dict[str, Any], cuda: dict[str, Any], case: dict[str, Any]
) -> None:
    if int(cpu["steps"]) != int(cuda["steps"]):
        raise RuntimeError(
            f"{case['id']} scientific accepted-step count drifted")
    minimum = int(case.get("minimum_scientific_steps", 0))
    if int(cpu["steps"]) < minimum:
        raise RuntimeError(
            f"{case['id']} scientific minimum accepted steps not reached")


def run_case(
    arch: Path, checkpoint_validator: Path, source_root: Path,
    case: dict[str, Any], output_root: Path
) -> dict[str, Any]:
    result = {"id": case["id"], "checkpoints": []}
    for steps in case["accepted_steps"]:
        cpu = run_arch_lane(arch, source_root, case, "cpu", int(steps), output_root)
        cuda = run_arch_lane(arch, source_root, case, "cuda", int(steps), output_root)
        parity = compare_hdf5_checkpoints(
            cpu["checkpoint"], cuda["checkpoint"], case["reduction_policy"],
            checkpoint_validator)
        if not parity["passed"]:
            raise RuntimeError(
                f"{case['id']} step {steps} first mismatch: {parity['first_mismatch']}")
        cpu_qualification = qualify_checkpoint(
            checkpoint_validator, cpu["checkpoint"], case)
        cuda_qualification = qualify_checkpoint(
            checkpoint_validator, cuda["checkpoint"], case)
        result["checkpoints"].append({
            "cpu": cpu, "cuda": cuda, "parity": parity,
            "cpu_qualification": cpu_qualification,
            "cuda_qualification": cuda_qualification})
    if "scientific_time" in case:
        terminal_time = float(case["scientific_time"])
        cpu = run_arch_terminal_lane(
            arch, source_root, case, "cpu", terminal_time, output_root)
        cuda = run_arch_terminal_lane(
            arch, source_root, case, "cuda", terminal_time, output_root)
        validate_scientific_steps(cpu, cuda, case)
        parity = compare_hdf5_checkpoints(
            cpu["checkpoint"], cuda["checkpoint"], case["reduction_policy"],
            checkpoint_validator)
        if not parity["passed"]:
            raise RuntimeError(
                f"{case['id']} scientific first mismatch: {parity['first_mismatch']}")
        cpu_qualification = qualify_checkpoint(
            checkpoint_validator, cpu["checkpoint"], case, scientific=True)
        cuda_qualification = qualify_checkpoint(
            checkpoint_validator, cuda["checkpoint"], case, scientific=True)
        for metrics in (cpu_qualification, cuda_qualification):
            if abs(float(metrics["time"]) - terminal_time) > 1.0e-14:
                raise RuntimeError(f"{case['id']} scientific terminal time drifted")
        result["scientific"] = {
            "cpu": cpu, "cuda": cuda, "parity": parity,
            "cpu_qualification": cpu_qualification,
            "cuda_qualification": cuda_qualification,
        }
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--case", action="append", default=[])
    parser.add_argument("--arch", type=Path)
    parser.add_argument("--checkpoint-validator", type=Path)
    parser.add_argument("--source-root", type=Path, default=Path.cwd())
    parser.add_argument("--output-root", type=Path)
    parser.add_argument("--unit-test", action="store_true")
    args = parser.parse_args(argv)
    manifest = load_manifest(args.manifest)
    if args.unit_test:
        print(json.dumps({"schema": manifest["schema"], "cases": len(manifest["cases"])}))
        return 0
    if args.arch is None or args.checkpoint_validator is None or args.output_root is None:
        parser.error(
            "release validation requires --arch, --checkpoint-validator and --output-root")
    selected = set(args.case)
    cases = select_cases(manifest, args.case)
    require_empty_output_root(args.output_root)
    evidence = {
        "schema": 1,
        "manifest_sha256": _sha256(args.manifest),
        "binary_sha256": _sha256(args.arch),
        "cases": [],
    }
    for case in cases:
        evidence["cases"].append(run_case(
            args.arch.resolve(), args.checkpoint_validator.resolve(),
            args.source_root.resolve(), case,
            args.output_root.resolve()))
    evidence["resolution_groups"] = (
        {} if selected else validate_resolution_groups(cases, evidence["cases"]))
    args.output_root.mkdir(parents=True, exist_ok=True)
    evidence_path = args.output_root / "backend-validation-evidence.json"
    evidence_path.write_text(json.dumps(evidence, indent=2, default=str) + "\n", encoding="utf-8")
    print(json.dumps({"status": "pass", "cases": len(cases), "evidence": str(evidence_path)}))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"backend validation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
