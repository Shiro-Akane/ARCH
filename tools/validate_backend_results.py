#!/usr/bin/env python3
"""Run and compare canonical CPU/CUDA backend validation cases.

Synthetic JSON is deliberately confined to ``--unit-test``.  Release evidence
always comes from ARCH plan/trace sidecars and project HDF5 checkpoints.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import re
import subprocess
import sys
from typing import Any

import validation_provenance as provenance
import validation_sanitizer


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
        checkpoint_comparison_mode(case)
    return manifest


def checkpoint_comparison_mode(case: dict[str, Any]) -> str:
    mode = case.get("checkpoint_comparison", "reproducibility")
    if mode not in {"reproducibility", "step-diagnostic"}:
        raise RuntimeError("invalid checkpoint comparison mode")
    if mode == "step-diagnostic":
        target = float(case.get("scientific_time", 0.0))
        if not math.isfinite(target) or target <= 0.0:
            raise RuntimeError("step diagnostics require prescribed physical-time acceptance")
    return mode


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


def runtime_case_inputs(cases: list[dict[str, Any]], source_root: Path) -> dict[str, Any]:
    return {
        case["id"]: provenance.runtime_inputs(
            parameter_file=source_root / case["input"], working_directory=source_root,
            parameter_reader=read_parameter_map, scientific_overrides=case.get("overrides"))
        for case in cases
    }


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


def validate_resolved_plan(
    path: Path, expected_backend: str,
    expected_policies: dict[str, Any] | None = None,
) -> dict[str, str]:
    plan = read_plan(path)
    validate_plan_values(plan, expected_backend, expected_policies)
    return plan


def validate_plan_values(
    plan: dict[str, str], expected_backend: str,
    expected_policies: dict[str, Any] | None = None,
) -> None:
    requested = plan.get("requested")
    resolved = plan.get("resolved")
    fallback = plan.get("fallback_reason", "")
    if requested != expected_backend or resolved != expected_backend:
        raise RuntimeError(
            f"resolved CUDA/backend mismatch: requested={requested}, resolved={resolved}")
    if expected_backend == "cuda" and fallback:
        raise RuntimeError(f"CUDA lane used fallback: {fallback}")
    if expected_policies is not None and not isinstance(expected_policies, dict):
        raise RuntimeError("expected plan policies must be a mapping")
    for field, expected in (expected_policies or {}).items():
        if isinstance(expected, dict):
            if set(expected) != {"cpu", "cuda"}:
                raise RuntimeError("backend-specific plan policy must cover CPU and CUDA")
            expected = expected[expected_backend]
        if not isinstance(expected, str) or not expected:
            raise RuntimeError("expected plan policy must name a resolved registration")
        if plan.get(field) != expected:
            raise RuntimeError(
                f"resolved policy mismatch: {field} expected={expected}, actual={plan.get(field)}")


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
    checkpoint_validator: Path, *, terminal_source_pair: tuple[Path, Path] | None = None,
    comparison_mode: str = "reproducibility", target_time: float | None = None,
) -> dict[str, Any]:
    operations = {"reproducibility": "--compare", "step-diagnostic": "--compare-step-diagnostic",
                  "physical-time": "--compare-physical-time"}
    if comparison_mode not in operations or (terminal_source_pair and comparison_mode != "reproducibility"):
        raise RuntimeError("invalid checkpoint comparison operation")
    if comparison_mode == "physical-time":
        if target_time is None or not math.isfinite(target_time) or target_time <= 0.0:
            raise RuntimeError("physical comparison requires a prescribed time")
    elif target_time is not None:
        raise RuntimeError("target time is only valid for a physical comparison")
    command = [str(checkpoint_validator),
               "--compare-terminal-restart" if terminal_source_pair else operations[comparison_mode],
               str(reference), str(candidate), str(policy["rtol"]), str(policy["atol"]),
               str(policy.get("enuc_scale_rtol", policy["rtol"])),
               str(policy.get("dt_burn_rtol", policy["rtol"]))]
    if terminal_source_pair:
        command.extend(str(path) for path in terminal_source_pair)
    if target_time is not None:
        command.append(str(target_time))
    completed = subprocess.run(
        command,
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
    result["tolerance"] = dict(policy)
    return result


def read_conservation_metrics(
    checkpoint_validator: Path, checkpoint: Path, parameter_file: Path | None = None
) -> dict[str, Any]:
    command = [str(checkpoint_validator), "--metrics", str(checkpoint)]
    parameter_sha256 = None
    if parameter_file is not None:
        parameter_sha256 = _sha256(parameter_file)
        command.extend(["--parameters", str(parameter_file)])
    completed = subprocess.run(
        command,
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=120, check=False)
    if completed.returncode != 0:
        raise RuntimeError(
            f"checkpoint metrics failed: {completed.stderr.strip()}")
    lines = [line for line in completed.stdout.splitlines() if line.startswith("{")]
    if not lines:
        raise RuntimeError("checkpoint metrics produced no JSON summary")
    result = json.loads(lines[-1])
    if parameter_file is not None:
        if _sha256(parameter_file) != parameter_sha256:
            raise RuntimeError("conservation parameter file changed while computing metrics")
        if result.get("measure") != "physical_cell_volume":
            raise RuntimeError("checkpoint validator did not provide physical cell-volume metrics")
        result["parameter_sha256"] = parameter_sha256
    else:
        if result.get("measure", "legacy_level_normalized") != "legacy_level_normalized":
            raise RuntimeError("level-normalized conservation metric measure changed")
        result["measure"] = "legacy_level_normalized"
    return result


def checkpoint_metadata(*, validator: Path, checkpoint: Path,
                        parameters: Path, expected_steps: int | None) -> dict[str, Any]:
    """Read actual HDF5 metadata with the project's existing comparator.

    File indices and process stdout cannot establish the checkpoint's step.
    Parameter-bound metrics also work for the annular checkpoint geometry.
    This checks continuation identity, not conservation/scientific accuracy.
    """
    identity = provenance.sha256(checkpoint)
    metrics = read_conservation_metrics(validator, checkpoint, parameters)
    step = metrics.get("step")
    if type(step) is not int or step < 0 or (expected_steps is not None and step != expected_steps):
        raise RuntimeError(
            f"checkpoint metadata step mismatch: expected {expected_steps}, got {step!r}")
    if provenance.sha256(checkpoint) != identity:
        raise RuntimeError("checkpoint changed while inspecting metadata")
    return {"step": step, "time": metrics.get("time"),
            "resume_after_regrid": metrics.get("resume_after_regrid"),
            "chk_file_index": metrics.get("chk_file_index"),
            "plt_file_index": metrics.get("plt_file_index"),
            "measure": metrics["measure"], "geometry": metrics.get("geometry"),
            "parameter_sha256": metrics["parameter_sha256"], "checkpoint_sha256": identity,
            "command": [str(validator), "--metrics", str(checkpoint),
                        "--parameters", str(parameters)]}


def validate_conservation(
    checkpoint_validator: Path, initial: Path, final: Path,
    policy: dict[str, Any], *, parameter_file: Path | None = None,
    parameter_sha256: str | None = None,
) -> dict[str, Any]:
    measure = policy.get("measure", "legacy_level_normalized")
    if measure not in ("legacy_level_normalized", "physical_cell_volume"):
        raise RuntimeError(f"unsupported conservation measure: {measure}")
    if measure == "physical_cell_volume":
        if parameter_file is None or parameter_sha256 is None:
            raise RuntimeError("physical conservation metrics require actual run parameter identity")
        if _sha256(parameter_file) != parameter_sha256:
            raise RuntimeError("conservation parameters differ from actual run")
    else:
        # Level-normalized Cartesian totals and their absolute budgets use
        # the same measure; physical-volume parameters do not apply here.
        parameter_file = None
    before = read_conservation_metrics(checkpoint_validator, initial, parameter_file)
    after = read_conservation_metrics(checkpoint_validator, final, parameter_file)
    if parameter_file is not None and (
            before["parameter_sha256"] != parameter_sha256
            or after["parameter_sha256"] != parameter_sha256):
        raise RuntimeError("conservation parameters differ from actual run")
    return validate_conservation_metrics(before, after, policy)


def validate_conservation_metrics(
    before: dict[str, Any], after: dict[str, Any], policy: dict[str, Any]
) -> dict[str, Any]:
    measure = policy.get("measure", "legacy_level_normalized")
    if measure not in ("legacy_level_normalized", "physical_cell_volume"):
        raise RuntimeError(f"unsupported conservation measure: {measure}")
    if any(metrics.get("measure", "legacy_level_normalized") != measure for metrics in (before, after)):
        raise RuntimeError("conservation metric measure differs from declared policy")
    if measure == "physical_cell_volume":
        if before.get("geometry") not in ("cartesian", "cylindrical", "spherical") \
                or before.get("geometry") != after.get("geometry") \
                or re.fullmatch(r"[0-9a-f]{64}", before.get("parameter_sha256", "")) is None \
                or before.get("parameter_sha256") != after.get("parameter_sha256"):
            raise RuntimeError("physical conservation metrics lack matching geometry/parameter identity")
    rtol = float(policy.get("rtol", 0.0))
    atol = float(policy.get("atol", 0.0))
    requested = policy.get(
        "fields", ["mass", "mom_u", "mom_v", "mom_w", "energy", "rhoX"])
    errors: dict[str, Any] = {}
    for field in requested:
        left_values = before[field] if field == "rhoX" else [before[field]]
        right_values = after[field] if field == "rhoX" else [after[field]]
        if len(left_values) != len(right_values):
            raise RuntimeError(f"conservation field shape drifted: {field}")
        field_errors = []
        for left, right in zip(left_values, right_values):
            if not math.isfinite(float(left)) or not math.isfinite(float(right)):
                raise RuntimeError(f"conservation field is nonfinite: {field}")
            absolute = abs(float(right) - float(left))
            relative = absolute / max(
                abs(float(left)), float.fromhex("0x1p-1022"))
            if absolute > atol + rtol * abs(float(left)):
                raise RuntimeError(
                    f"conservation drift field={field} before={left} "
                    f"after={right} abs={absolute} rel={relative}")
            field_errors.append({"absolute": absolute, "relative": relative})
        errors[field] = field_errors if field == "rhoX" else field_errors[0]
    return {"before": before, "after": after, "errors": errors}


def validate_topology_policy(
    observed: list[dict[str, Any]], policy: dict[str, Any], identifier: str
) -> dict[str, bool] | None:
    if policy.get("require_refined") and not any(int(item["max_level"]) > 0 for item in observed):
        raise RuntimeError(f"{identifier} never produced a refined leaf")
    if policy.get("require_mixed") and not any(
        int(item["min_level"]) < int(item["max_level"]) for item in observed
    ):
        raise RuntimeError(f"{identifier} never produced a mixed-level hierarchy")
    if policy.get("require_count_change") and len({int(item["blocks"]) for item in observed}) < 2:
        raise RuntimeError(f"{identifier} leaf count never changed")
    if policy.get("require_refine_transition") or policy.get("require_derefine_transition"):
        return validate_topology_transitions(
            observed, require_refine=bool(policy.get("require_refine_transition")),
            require_derefine=bool(policy.get("require_derefine_transition")))
    return None


def validate_topology_transitions(
    snapshots: list[dict[str, Any]], *, require_refine: bool,
    require_derefine: bool
) -> dict[str, bool]:
    """Require explicit parent/children leaf transitions between snapshots."""
    refined = False
    derefined = False
    for left, right in zip(snapshots, snapshots[1:]):
        dimension = int(left.get("dimension", 0))
        if dimension != int(right.get("dimension", 0)) or dimension not in (1, 2, 3):
            raise RuntimeError("topology transition dimension is invalid")
        left_keys = {tuple(map(int, item)) for item in left.get("topology", [])}
        right_keys = {tuple(map(int, item)) for item in right.get("topology", [])}
        if not left_keys or not right_keys or any(len(item) != 4 for item in left_keys | right_keys):
            raise RuntimeError("topology transition evidence is incomplete")

        def children(parent: tuple[int, ...]) -> set[tuple[int, ...]]:
            level, x1, x2, x3 = parent
            result: set[tuple[int, ...]] = set()
            for child in range(1 << dimension):
                coordinates = [x1, x2, x3]
                for axis in range(dimension):
                    coordinates[axis] = 2 * coordinates[axis] + ((child >> axis) & 1)
                result.add((level + 1, *coordinates))
            return result

        refined = refined or any(
            parent not in right_keys and children(parent).issubset(right_keys)
            for parent in left_keys)
        derefined = derefined or any(
            parent not in left_keys and children(parent).issubset(left_keys)
            for parent in right_keys)
    if require_refine and not refined:
        raise RuntimeError("no explicit refine transition was observed")
    if require_derefine and not derefined:
        raise RuntimeError("no explicit derefine transition was observed")
    return {"refined": refined, "derefined": derefined}


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
        if "linf_max" in qualification and metrics["linf"] > qualification["linf_max"]:
            raise RuntimeError("diffusion Linf gate failed")
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
    elif mode == "gravity":
        for field in ('rho', 'velocity', 'pressure', 'energy'):
            error = metrics.get(field + '_linf')
            if not isinstance(error, (int, float)) or not math.isfinite(error) \
                    or error < 0 or error > qualification['linf_max']:
                raise RuntimeError(f'constant-acceleration analytic gate failed for {field}')
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
        minimum_orders = {field: float(qualification["minimum_" + field + "_order"])
                          for field in ("l1", "l2") if "minimum_" + field + "_order" in qualification}
        pairs = qualification.get("convergence_pairs", "all")
        target_time = members[0].get("scientific_time")
        if not minimum_orders or any(not math.isfinite(value) or value <= 0 for value in minimum_orders.values()) \
                or pairs not in ("all", "final") or not isinstance(target_time, (int, float)) \
                or not math.isfinite(target_time) or target_time <= 0 \
                or any(item.get("scientific_time") != target_time for item in members):
            raise RuntimeError(f"{group} convergence time/order contract is invalid")
        for item in members:
            policy = item["qualification"]
            if policy.get("convergence_pairs", "all") != pairs or any(
                    policy.get("minimum_" + field + "_order") != qualification.get("minimum_" + field + "_order")
                    for field in ("l1", "l2")):
                raise RuntimeError(f"{group} convergence budget differs between resolutions")
        orders: dict[str, dict[str, list[float]]] = {}
        for backend in ("cpu", "cuda"):
            metrics = [
                by_id[item["id"]]["scientific"][f"{backend}_qualification"]
                for item in members
            ]
            backend_orders = {"l1": [], "l2": []}
            for field in ("l1", "l2"):
                minimum = minimum_orders.get(field)
                errors = [float(metric[field]) for metric in metrics]
                if not all(math.isfinite(error) and error > 0.0 for error in errors):
                    raise RuntimeError(f"{group} {backend} {field.upper()} is invalid")
                for index in range(len(errors) - 1):
                    order = math.log(errors[index] / errors[index + 1]) / math.log(
                        resolutions[index + 1] / resolutions[index])
                    backend_orders[field].append(order)
                    if minimum is not None and (pairs == "all" or index == len(errors) - 2) and order < minimum:
                        raise RuntimeError(
                            f"{group} {backend} {field.upper()} convergence gate failed: "
                            f"{order} < {minimum}")
            orders[backend] = backend_orders
        summaries[group] = {"resolutions": resolutions, "orders": orders}
    return summaries


def qualify_checkpoint(
    checkpoint_validator: Path, checkpoint: Path, case: dict[str, Any],
    *, scientific: bool = False, parameter_file: Path | None = None
) -> dict[str, Any]:
    qualification = case.get("qualification")
    if not qualification:
        return {"status": "not-requested"}
    mode = qualification_mode(qualification)
    command = [str(checkpoint_validator), "--qualify", str(checkpoint), mode]
    if mode == 'gravity':
        if parameter_file is None:
            raise RuntimeError('gravity reference requires actual run parameters')
        command += ['--parameters', str(parameter_file)]
    completed = subprocess.run(
        command,
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


def qualification_mode(qualification: dict[str, Any]) -> str:
    reference = qualification["reference"]
    if reference == "periodic_entropy_wave":
        mode = "smooth"
    elif reference == "periodic_diffusion_mode":
        mode = "diffusion"
    elif reference == "sod_exact_riemann":
        mode = "sod"
    elif reference == "cpu_and_network_conservation":
        mode = "burn"
    elif reference == "constant_external_acceleration":
        mode = "gravity"
    else:
        raise RuntimeError(f"unknown independent reference: {reference}")
    return mode


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
    summary = {"records": len(rows),
               "max_macro_step": max(int(row["macro_step"]) for row in rows),
               "unfinished_transfers": 0, "stale_ghost_publications": 0}
    validate_cuda_trace_summary(summary, expected_steps)
    return summary


def validate_cuda_trace_summary(summary: dict[str, Any], expected_steps: int) -> None:
    if int(summary["records"]) <= 0 or int(summary["max_macro_step"]) < expected_steps \
            or summary["unfinished_transfers"] != 0 or summary["stale_ghost_publications"] != 0:
        raise RuntimeError("CUDA trace summary failed completion/ghost validity gates")


def summarize_regrids(records: list[dict[str, Any]], backend: str, expected_steps: int) -> dict[str, Any]:
    """Whole transaction costs, not an addition to overlapping backend traces."""
    if backend not in ('cpu', 'cuda') or not records:
        raise RuntimeError('missing regrid measurement coverage')
    counters = ('bytes_h2d', 'bytes_d2h', 'kernel_count', 'stream_sync_count')
    integer_fields = ('macro_step', 'old_blocks', 'new_blocks', 'topology_changed', *counters)
    previous = None
    for record in records:
        if set(record) != {'backend', 'physical_time', 'wall_seconds', *integer_fields} \
                or record['backend'] != backend \
                or any(type(record[key]) is not int or record[key] < 0 for key in integer_fields) \
                or any(type(record[key]) not in (int, float) or not math.isfinite(record[key])
                       or record[key] < 0 for key in ('physical_time', 'wall_seconds')) \
                or record['macro_step'] >= expected_steps \
                or min(record['old_blocks'], record['new_blocks']) < 1 \
                or record['topology_changed'] not in (0, 1) \
                or (not record['topology_changed'] and record['old_blocks'] != record['new_blocks']) \
                or (backend == 'cpu' and any(record[key] for key in counters)):
            raise RuntimeError('invalid regrid measurement')
        if previous and (record['macro_step'] < previous['macro_step']
                         or record['physical_time'] < previous['physical_time']
                         or record['old_blocks'] != previous['new_blocks']):
            raise RuntimeError('regrid measurement sequence is discontinuous')
        previous = record
    return {'records': len(records),
            'topology_changes': sum(row['topology_changed'] for row in records),
            'wall_seconds': math.fsum(row['wall_seconds'] for row in records),
            **{key: sum(row[key] for row in records) for key in counters},
            'overlaps_backend_trace': True}


def read_regrid_metrics(path: Path, backend: str, expected_steps: int) -> dict[str, Any]:
    if not path.is_file():
        raise RuntimeError('ARCH lane did not produce whole-regrid measurements')
    lines = path.read_text().splitlines()
    if len(lines) < 2:
        raise RuntimeError('regrid measurement file is empty')
    header = lines[0].split('\t')
    if len(set(header)) != len(header):
        raise RuntimeError('duplicate regrid measurement fields')
    records = []
    for line in lines[1:]:
        values = line.split('\t')
        if len(values) != len(header):
            raise RuntimeError('incomplete regrid measurement row')
        try:
            records.append({key: (value if key == 'backend' else
                float(value) if key in ('physical_time', 'wall_seconds') else int(value))
                for key, value in zip(header, values)})
        except ValueError as error:
            raise RuntimeError('malformed regrid measurement value') from error
    return {'file': provenance.file_identity(path), 'records': records,
            'summary': summarize_regrids(records, backend, expected_steps)}


def validate_cuda_diffusion_schedule(
    path: Path, expected_steps: int, policy: dict[str, Any]
) -> dict[str, Any]:
    if not path.is_file():
        raise RuntimeError(f"missing CUDA diffusion schedule: {path}")
    lines = [line for line in path.read_text(encoding="utf-8").splitlines()
             if line.strip()]
    if len(lines) < 2:
        raise RuntimeError("CUDA diffusion schedule is empty")
    header = lines[0].split("\t")
    required = {
        "macro_step", "cache_generation", "order", "stages",
        "negative_gamma_stages", "captures_initial_operator",
        "diffusion_dt", "dt_forward_euler",
    }
    if not required.issubset(header):
        raise RuntimeError("CUDA diffusion schedule schema drifted")
    rows = [dict(zip(header, line.split("\t"))) for line in lines[1:]]
    lanes_per_step = int(policy.get("lanes_per_step", 2))
    if len(rows) != expected_steps * lanes_per_step:
        raise RuntimeError("CUDA diffusion schedule lane count drifted")
    expected_order = int(policy["order"])
    expected_stages = int(policy["stages"])
    generations: list[int] = []
    expected_macro_steps = [
        step for step in range(expected_steps)
        for _ in range(lanes_per_step)
    ]
    observed_macro_steps: list[int] = []
    for row in rows:
        observed_macro_steps.append(int(row["macro_step"]))
        order = int(row["order"])
        stages = int(row["stages"])
        negative = int(row["negative_gamma_stages"])
        captures = int(row["captures_initial_operator"])
        generation = int(row["cache_generation"])
        diffusion_dt = float(row["diffusion_dt"])
        dt_forward_euler = float(row["dt_forward_euler"])
        if order != expected_order or stages != expected_stages:
            raise RuntimeError(
                "CUDA diffusion schedule order/stage count drifted")
        if not math.isfinite(diffusion_dt) or diffusion_dt <= 0.0 \
                or not math.isfinite(dt_forward_euler) \
                or dt_forward_euler <= 0.0:
            raise RuntimeError("CUDA diffusion schedule timestep is invalid")
        if expected_order == 2:
            if negative <= 0 or captures != 1:
                raise RuntimeError(
                    "RKL2 did not exercise negative gamma and F(Y0) capture")
        elif negative != 0 or captures != 0:
            raise RuntimeError("RKL1 unexpectedly used the RKL2 F(Y0) cache")
        generations.append(generation)
    if generations != list(range(1, len(rows) + 1)):
        raise RuntimeError(
            "CUDA diffusion F(Y0) cache generation crossed a lane boundary")
    if observed_macro_steps != expected_macro_steps:
        raise RuntimeError(
            "CUDA diffusion schedule macro-step/lane ordering drifted")
    summary = {
        "records": len(rows),
        "order": expected_order,
        "stages": expected_stages,
        "negative_gamma_records": sum(
            int(row["negative_gamma_stages"]) > 0 for row in rows),
        "cache_generations": generations,
        "macro_steps": observed_macro_steps,
        "valid_timestep_records": len(rows),
        "initial_operator_records": sum(int(row["captures_initial_operator"]) for row in rows),
    }
    validate_cuda_diffusion_summary(summary, expected_steps, policy)
    return summary


def validate_cuda_diffusion_summary(
    summary: dict[str, Any], expected_steps: int, policy: dict[str, Any]
) -> None:
    lanes = int(policy.get("lanes_per_step", 2))
    records = expected_steps * lanes
    order = int(policy["order"])
    if summary["records"] != records or summary["order"] != order \
            or summary["stages"] != int(policy["stages"]) \
            or summary["valid_timestep_records"] != records \
            or summary["negative_gamma_records"] != (records if order == 2 else 0) \
            or summary["initial_operator_records"] != (records if order == 2 else 0) \
            or summary["cache_generations"] != list(range(1, records + 1)) \
            or summary["macro_steps"] != [step for step in range(expected_steps) for _ in range(lanes)]:
        raise RuntimeError("CUDA diffusion summary failed schedule/cache/timestep gates")


def _sha256(path: Path) -> str:
    return provenance.sha256(path)


def require_empty_output_root(path: Path) -> None:
    if path.exists() and (not path.is_dir() or any(path.iterdir())):
        raise RuntimeError(f"release evidence root must be empty: {path}")


def run_arch_with_logs(command: list[str], *, source_root: Path,
                       lane_root: Path, timeout: float,
                       sanitizer: validation_sanitizer.CudaSanitizer | None = None) -> subprocess.CompletedProcess:
    """Retain diagnostic streams on success, failure and process timeout.

    TimeoutExpired may expose bytes even with text=True. Persist them before
    re-raising the original timeout; no incomplete run can become evidence.
    Both formal runtime validators use this one process/logging boundary.
    """
    def save(stdout, stderr) -> None:
        for name, value in (("arch.stdout", stdout), ("arch.stderr", stderr)):
            if isinstance(value, bytes):
                value = value.decode("utf-8", errors="replace")
            (lane_root / name).write_text(value or "", encoding="utf-8")

    if sanitizer is not None:
        command = sanitizer.command(command, lane_root)
    try:
        completed = subprocess.run(
            command, cwd=source_root, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=timeout, check=False)
    except subprocess.TimeoutExpired as error:
        save(error.stdout, error.stderr)
        raise
    save(completed.stdout, completed.stderr)
    if completed.returncode == 0 and sanitizer is not None:
        sanitizer.evidence(lane_root)  # Exit zero alone does not prove instrumentation ran.
    return completed


def run_arch_lane(
    arch: Path, source_root: Path, case: dict[str, Any], backend: str,
    steps: int, output_root: Path,
    sanitizer: validation_sanitizer.CudaSanitizer | None = None
) -> dict[str, Any]:
    lane_root = output_root / case["id"] / f"step-{steps}" / backend
    lane_root.mkdir(parents=True, exist_ok=True)
    base_name = f"{case['id']}_{backend}_s{steps}"
    parameter = lane_root / f"{base_name}.par"
    render_parameter_file(
        source_root / case["input"], parameter, backend=backend,
        output_dir=lane_root, base_name=base_name, accepted_steps=steps,
        scientific_overrides=case.get("overrides", {}))
    parameter_sha256 = _sha256(parameter)
    completed = run_arch_with_logs(
        [str(arch), case["problem"], str(parameter)], source_root=source_root,
        lane_root=lane_root, timeout=int(case.get("timeout_seconds", 600)),
        sanitizer=sanitizer if backend == "cuda" else None)
    if _sha256(parameter) != parameter_sha256:
        raise RuntimeError("actual run parameter file changed during ARCH execution")
    if completed.returncode != 0:
        raise RuntimeError(
            f"{case['id']} {backend} step {steps} failed: {completed.returncode}")
    match = STEP_RE.search(completed.stdout)
    if match is None or int(match.group(1)) != steps:
        raise RuntimeError(f"{case['id']} {backend} accepted-step count drifted")
    prefix = lane_root / base_name
    plan = prefix.with_name(prefix.name + "_backend_plan.txt")
    initial_checkpoint = prefix.with_name(prefix.name + "_chk_0000.h5")
    checkpoint = prefix.with_name(prefix.name + "_chk_0001.h5")
    resolved_plan = validate_resolved_plan(plan, backend, case.get("plan_policy"))
    trace = prefix.with_name(prefix.name + "_backend_trace.tsv")
    trace_summary = validate_cuda_trace(trace, steps) if backend == "cuda" else None
    schedule = prefix.with_name(prefix.name + "_diffusion_schedule.tsv")
    schedule_summary = None
    if backend == "cuda" and case.get("rkl_policy"):
        schedule_summary = validate_cuda_diffusion_schedule(
            schedule, steps, case["rkl_policy"])
    if not initial_checkpoint.is_file() or not checkpoint.is_file():
        raise RuntimeError(f"missing HDF5 checkpoint: {checkpoint}")
    return {
        "backend": backend,
        "steps": steps,
        "parameter_file": parameter,
        "parameter_sha256": parameter_sha256,
        "initial_checkpoint": initial_checkpoint,
        "initial_checkpoint_sha256": _sha256(initial_checkpoint),
        "checkpoint": checkpoint,
        "checkpoint_sha256": _sha256(checkpoint),
        "plan": plan,
        "resolved_plan": resolved_plan,
        "trace": trace if backend == "cuda" else None,
        "trace_summary": trace_summary,
        "regrid": read_regrid_metrics(prefix.with_name(prefix.name + "_regrid.tsv"), backend, steps),
        "diffusion_schedule": schedule if schedule_summary else None,
        "diffusion_schedule_summary": schedule_summary,
        "sanitizer": sanitizer.evidence(lane_root) if sanitizer and backend == "cuda" else None,
    }


def run_arch_terminal_lane(
    arch: Path, source_root: Path, case: dict[str, Any], backend: str,
    terminal_time: float, output_root: Path,
    sanitizer: validation_sanitizer.CudaSanitizer | None = None
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
    parameter_sha256 = _sha256(parameter)
    completed = run_arch_with_logs(
        [str(arch), case["problem"], str(parameter)], source_root=source_root,
        lane_root=lane_root, timeout=int(case.get("timeout_seconds", 600)),
        sanitizer=sanitizer if backend == "cuda" else None)
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
    resolved_plan = validate_resolved_plan(plan, backend, case.get("plan_policy"))
    trace = prefix.with_name(prefix.name + "_backend_trace.tsv")
    trace_summary = (
        validate_cuda_trace(trace, accepted_steps) if backend == "cuda" else None)
    if not checkpoint.is_file():
        raise RuntimeError(f"missing scientific HDF5 checkpoint: {checkpoint}")
    if _sha256(parameter) != parameter_sha256:
        raise RuntimeError("scientific parameter file changed during execution")
    return {
        "backend": backend,
        "steps": accepted_steps,
        "terminal_time": terminal_time,
        "parameter_file": parameter,
        "parameter_sha256": parameter_sha256,
        "checkpoint": checkpoint,
        "checkpoint_sha256": _sha256(checkpoint),
        "plan": plan,
        "resolved_plan": resolved_plan,
        "trace": trace if backend == "cuda" else None,
        "trace_summary": trace_summary,
        "regrid": read_regrid_metrics(prefix.with_name(prefix.name + "_regrid.tsv"), backend, accepted_steps),
        "sanitizer": sanitizer.evidence(lane_root) if sanitizer and backend == "cuda" else None,
    }


def validate_scientific_steps(
    cpu: dict[str, Any], cuda: dict[str, Any], case: dict[str, Any]
) -> None:
    # Internal adaptive step counts are diagnostics; each executor must still
    # do the declared minimum work and reach the separately checked target time.
    minimum = max(1, int(case.get("minimum_scientific_steps", 0)))
    for lane in (cpu, cuda):
        if type(lane.get("steps")) is not int or lane["steps"] < minimum:
            raise RuntimeError(f"{case['id']} scientific minimum accepted steps not reached")


def run_case(
    arch: Path, checkpoint_validator: Path, source_root: Path,
    case: dict[str, Any], output_root: Path,
    sanitizer: validation_sanitizer.CudaSanitizer | None = None
) -> dict[str, Any]:
    result = {"id": case["id"], "checkpoints": []}
    for steps in case["accepted_steps"]:
        cpu = run_arch_lane(arch, source_root, case, "cpu", int(steps), output_root)
        cuda = run_arch_lane(arch, source_root, case, "cuda", int(steps), output_root, sanitizer)
        parity = compare_hdf5_checkpoints(
            cpu["checkpoint"], cuda["checkpoint"], case["reduction_policy"],
            checkpoint_validator, comparison_mode=checkpoint_comparison_mode(case))
        if not parity["passed"]:
            raise RuntimeError(
                f"{case['id']} step {steps} first mismatch: {parity['first_mismatch']}")
        cpu_qualification = qualify_checkpoint(
            checkpoint_validator, cpu["checkpoint"], case, parameter_file=cpu.get('parameter_file'))
        cuda_qualification = qualify_checkpoint(
            checkpoint_validator, cuda["checkpoint"], case, parameter_file=cuda.get('parameter_file'))
        conservation_policy = case.get("conservation_policy")
        cpu_conservation = cuda_conservation = {"status": "not-requested"}
        if conservation_policy:
            cpu_conservation = validate_conservation(
                checkpoint_validator, cpu["initial_checkpoint"],
                cpu["checkpoint"], conservation_policy,
                parameter_file=cpu.get("parameter_file"),
                parameter_sha256=cpu.get("parameter_sha256"))
            cuda_conservation = validate_conservation(
                checkpoint_validator, cuda["initial_checkpoint"],
                cuda["checkpoint"], conservation_policy,
                parameter_file=cuda.get("parameter_file"),
                parameter_sha256=cuda.get("parameter_sha256"))
        result["checkpoints"].append({
            "cpu": cpu, "cuda": cuda, "parity": parity,
            "cpu_qualification": cpu_qualification,
            "cuda_qualification": cuda_qualification,
            "cpu_conservation": cpu_conservation,
            "cuda_conservation": cuda_conservation})
    if "scientific_time" in case:
        terminal_time = float(case["scientific_time"])
        cpu = run_arch_terminal_lane(
            arch, source_root, case, "cpu", terminal_time, output_root)
        cuda = run_arch_terminal_lane(
            arch, source_root, case, "cuda", terminal_time, output_root, sanitizer)
        validate_scientific_steps(cpu, cuda, case)
        parity = compare_hdf5_checkpoints(
            cpu["checkpoint"], cuda["checkpoint"], case["reduction_policy"],
            checkpoint_validator, comparison_mode="physical-time", target_time=terminal_time)
        if not parity["passed"]:
            raise RuntimeError(
                f"{case['id']} scientific first mismatch: {parity['first_mismatch']}")
        cpu_qualification = qualify_checkpoint(
            checkpoint_validator, cpu["checkpoint"], case, scientific=True, parameter_file=cpu.get('parameter_file'))
        cuda_qualification = qualify_checkpoint(
            checkpoint_validator, cuda["checkpoint"], case, scientific=True, parameter_file=cuda.get('parameter_file'))
        # Reaching the requested time is a checkpoint fact, independent of
        # whether this case has a scientific oracle. A parity-only case must
        # still prove its terminal state, not crash on "not-requested" or skip
        # the check. Reuse the shared, parameter-bound metadata reader.
        for lane in (cpu, cuda):
            metrics = checkpoint_metadata(
                validator=checkpoint_validator, checkpoint=lane["checkpoint"],
                parameters=lane["parameter_file"], expected_steps=int(lane["steps"]))
            if metrics["parameter_sha256"] != lane["parameter_sha256"]:
                raise RuntimeError("scientific parameter identity drifted")
            actual_time = float(metrics["time"])
            if not math.isfinite(actual_time) or actual_time != terminal_time:
                raise RuntimeError(f"{case['id']} scientific terminal time drifted")
            lane["checkpoint_metadata"] = metrics
        result["scientific"] = {
            "cpu": cpu, "cuda": cuda, "parity": parity,
            "cpu_qualification": cpu_qualification,
            "cuda_qualification": cuda_qualification,
        }
    topology_policy = case.get("topology_policy", {})
    if topology_policy:
        observed = [entry["parity"] for entry in result["checkpoints"]]
        transitions = validate_topology_policy(observed, topology_policy, case["id"])
        if transitions is not None:
            result["topology_transitions"] = transitions
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True,
                        help="canonical case manifest with inputs and acceptance budgets")
    parser.add_argument("--case", action="append", default=[],
                        help="run only this case ID; repeat to select several cases")
    parser.add_argument("--arch", type=Path, help="ARCH executable from the selected build")
    parser.add_argument("--checkpoint-validator", type=Path,
                        help="arch_cuda_single_level_validation from the same build")
    parser.add_argument("--source-root", type=Path, default=Path.cwd(),
                        help="repository root used to resolve inputs and capture source identity")
    parser.add_argument("--build-dir", type=Path,
                        help="CMake build directory that produced both executables")
    parser.add_argument("--configuration", help="required for multi-config builds")
    parser.add_argument("--output-root", type=Path,
                        help="new or empty directory for logs, checkpoints and the result report")
    parser.add_argument("--unit-test", action="store_true",
                        help="check manifest structure only; do not execute ARCH")
    validation_sanitizer.add_arguments(parser)
    args = parser.parse_args(argv)
    manifest = load_manifest(args.manifest)
    if args.unit_test:
        print(json.dumps({"schema": manifest["schema"], "cases": len(manifest["cases"])}))
        return 0
    if args.arch is None or args.checkpoint_validator is None \
            or args.output_root is None or args.build_dir is None:
        parser.error(
            "release validation requires --arch, --checkpoint-validator, --build-dir and --output-root")
    selected = set(args.case)
    cases = select_cases(manifest, args.case)
    require_empty_output_root(args.output_root)
    identity_arguments = {
        "arch": args.arch.resolve(),
        "checkpoint_validator": args.checkpoint_validator.resolve(),
        "source_root": args.source_root.resolve(),
        "build_dir": args.build_dir.resolve(),
        "configuration": args.configuration,
    }
    identity = provenance.capture(**identity_arguments)
    sanitizer = validation_sanitizer.from_arguments(args)
    evidence = {
        "schema": 1,
        "manifest_sha256": _sha256(args.manifest),
        "input_sha256": {
            case["input"]: _sha256(args.source_root / case["input"])
            for case in cases
        },
        "runtime_inputs": runtime_case_inputs(cases, args.source_root.resolve()),
        "cases": [],
    }
    for case in cases:
        evidence["cases"].append(run_case(
            args.arch.resolve(), args.checkpoint_validator.resolve(),
            args.source_root.resolve(), case,
            args.output_root.resolve(), sanitizer))
    evidence["resolution_groups"] = (
        {} if selected else validate_resolution_groups(cases, evidence["cases"]))
    args.output_root.mkdir(parents=True, exist_ok=True)
    evidence_path = args.output_root / "backend-validation-evidence.json"
    if evidence["manifest_sha256"] != _sha256(args.manifest) or any(
        value != _sha256(args.source_root / name)
        for name, value in evidence["input_sha256"].items()
    ):
        raise RuntimeError("validation manifest or input changed during execution")
    if evidence["runtime_inputs"] != runtime_case_inputs(cases, args.source_root.resolve()):
        raise RuntimeError("runtime validation dependencies changed during execution")
    provenance.write_evidence(evidence_path, evidence, identity, **identity_arguments)
    print(json.dumps({"status": "pass", "cases": len(cases), "evidence": str(evidence_path)}))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"backend validation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
