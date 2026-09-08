#!/usr/bin/env python3
"""Gate AMR or full runtime-parity matrices against final artifacts.

Historical schema-1 reports remain readable, but cannot pass this gate without
the provenance captured by the current runtime validators. This is an evidence
consistency/coverage check, not a substitute for executing the validation suites.
Even the full-runtime profile is not a complete scientific/sanitizer/capacity
release qualification; its output states that boundary explicitly.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import re
import sys
from typing import Any

import validate_backend_results as backend_validation
import validate_cuda_amr_restart as restart_validation
import validation_provenance as provenance


def require_keys(record: dict[str, Any], keys: tuple[str, ...], description: str) -> None:
    if not isinstance(record, dict) or any(key not in record for key in keys):
        raise RuntimeError(f"incomplete {description} evidence")


def check_lane(lane: dict[str, Any], *, backend: str, steps: int,
               initial: bool = False, trace: bool = False,
               regrid: bool = False,
               rkl_policy: dict[str, Any] | None = None,
               plan_policy: dict[str, Any] | None = None) -> None:
    require_keys(lane, ("backend", "steps", "checkpoint", "checkpoint_sha256",
                        "plan", "resolved_plan"), "lane/checkpoint")
    if lane["backend"] != backend or lane["steps"] != steps:
        raise RuntimeError("checkpoint/backend coverage mismatch")
    backend_validation.validate_plan_values(lane["resolved_plan"], backend, plan_policy)
    for prefix in (("checkpoint", "initial_checkpoint") if initial else ("checkpoint",)):
        if not lane.get(prefix) or re.fullmatch(r"[0-9a-f]{64}", lane.get(prefix + "_sha256", "")) is None:
            raise RuntimeError(f"missing/invalid {prefix} identity")
    if not lane["plan"]:
        raise RuntimeError("missing resolved plan path")
    if regrid:
        metrics = lane.get('regrid')
        require_keys(metrics, ('file', 'records', 'summary'), 'whole-regrid measurement')
        require_keys(metrics['file'], ('path', 'sha256'), 'whole-regrid file identity')
        if not metrics['file']['path'] or re.fullmatch(r'[0-9a-f]{64}', metrics['file']['sha256']) is None \
                or backend_validation.summarize_regrids(metrics['records'], backend, steps) != metrics['summary']:
            raise RuntimeError('whole-regrid measurement summary or identity mismatch')
    if trace:
        require_keys(lane, ("trace", "trace_summary"), "CUDA trace")
        if not lane["trace"] or not isinstance(lane["trace_summary"], dict):
            raise RuntimeError("missing CUDA trace evidence")
        require_keys(lane["trace_summary"],
                     ("records", "max_macro_step", "unfinished_transfers", "stale_ghost_publications"),
                     "CUDA trace summary")
        backend_validation.validate_cuda_trace_summary(lane["trace_summary"], steps)
    if rkl_policy:
        require_keys(lane, ("diffusion_schedule", "diffusion_schedule_summary"), "RKL schedule")
        summary = lane["diffusion_schedule_summary"]
        if not lane["diffusion_schedule"]:
            raise RuntimeError("missing RKL schedule path")
        require_keys(summary, ("records", "order", "stages", "valid_timestep_records",
                               "negative_gamma_records", "initial_operator_records",
                               "cache_generations", "macro_steps"), "RKL summary")
        backend_validation.validate_cuda_diffusion_summary(summary, steps, rkl_policy)


def check_comparison(comparison: dict[str, Any], policy: dict[str, Any], *, steps: int,
                     output_offsets: dict[str, int] | None = None,
                     comparison_mode: str = "reproducibility",
                     target_time: float | None = None) -> None:
    required = ("status", "passed", "step", "time", "dimension", "blocks", "min_level",
                "max_level", "topology", "max_abs", "max_rel", "max_field_normalized",
                "max_enuc_normalized", "dt_old", "dt_burn", "candidate_dt_burn",
                "dt_burn_relative", "chk_file_index", "plt_file_index",
                "resume_after_regrid", "tolerance", "output_index_offsets",
                "candidate_chk_file_index", "candidate_plt_file_index")
    require_keys(comparison, required, "checkpoint comparison")
    if comparison["passed"] is not True or comparison["status"] != "pass" \
            or comparison["step"] != steps or comparison["tolerance"] != policy:
        raise RuntimeError("checkpoint comparison failed or declared tolerance/step drifted")
    if comparison.get("comparison_mode", "reproducibility") != comparison_mode:
        raise RuntimeError("checkpoint comparison mode differs from its acceptance contract")
    if comparison_mode in ("physical-time", "step-diagnostic"):
        require_keys(comparison, ("candidate_step", "candidate_time", "candidate_dt_old",
                                  "controller_matches", "time_roundoff_matches",
                                  "output_indices_match"), "temporal comparison")
        if type(comparison["candidate_step"]) is not int or comparison["candidate_step"] < 0 \
                or any(type(comparison[key]) is not bool for key in
                       ("controller_matches", "time_roundoff_matches", "output_indices_match")) \
                or any(not math.isfinite(float(comparison[key])) or comparison[key] < 0
                       for key in ("candidate_time", "candidate_dt_old")):
            raise RuntimeError("invalid temporal comparison diagnostics")
    if comparison_mode == "step-diagnostic" and comparison["candidate_step"] != steps:
        raise RuntimeError("step-diagnostic comparison requires the same accepted step")
    if comparison_mode == "physical-time":
        require_keys(comparison, ("target_time",), "physical-time comparison")
        if target_time is None or not math.isfinite(target_time) or target_time <= 0.0 \
                or comparison["target_time"] != target_time \
                or comparison["time"] != target_time or comparison["candidate_time"] != target_time:
            raise RuntimeError("prescribed physical-time comparison failed")
    expected_offsets = output_offsets or {"checkpoint": 0, "plot": 0}
    if comparison["output_index_offsets"] != expected_offsets:
        raise RuntimeError("checkpoint output history differs from the declared source")
    for key, field in (("checkpoint", "chk_file_index"), ("plot", "plt_file_index")):
        before, after = comparison[field], comparison["candidate_" + field]
        if any(type(value) is not int or value < 0 for value in (before, after)) \
                or (comparison_mode != "physical-time" and after - before != expected_offsets[key]):
            raise RuntimeError("checkpoint output counter continuity failed")
    for key in ("time", "max_abs", "max_rel", "max_field_normalized", "max_enuc_normalized",
                "dt_old", "dt_burn", "candidate_dt_burn", "dt_burn_relative"):
        value = float(comparison[key])
        if not math.isfinite(value) or value < 0.0:
            raise RuntimeError(f"invalid checkpoint comparison metric: {key}")
    topology = comparison["topology"]
    if comparison["dimension"] not in (1, 2, 3) or not isinstance(topology, list) \
            or len(topology) != comparison["blocks"] or not topology \
            or any(not isinstance(item, list) or len(item) != 4
                   or any(type(value) is not int or value < 0 for value in item)
                   for item in topology) \
            or len({tuple(item) for item in topology}) != len(topology) \
            or comparison["min_level"] != min(item[0] for item in topology) \
            or comparison["max_level"] != max(item[0] for item in topology):
        raise RuntimeError("invalid/incomplete checkpoint topology metrics")


def check_qualification(metrics: dict[str, Any], case: dict[str, Any], *, scientific: bool = False) -> None:
    policy = case.get("qualification")
    if not policy:
        if metrics != {"status": "not-requested"}:
            raise RuntimeError("missing not-requested qualification record")
        return
    if not isinstance(metrics, dict) or metrics.get("status") != "pass":
        raise RuntimeError("missing/pending scientific qualification metrics")
    if any(isinstance(value, (int, float)) and not math.isfinite(value) for value in metrics.values()):
        raise RuntimeError("nonfinite scientific qualification metrics")
    backend_validation.validate_qualification_metrics(
        metrics, policy, backend_validation.qualification_mode(policy), scientific=scientific)


def check_matrix(report: dict[str, Any], manifest_path: Path, source_root: Path) -> None:
    manifest = backend_validation.load_manifest(manifest_path)
    if report.get("manifest_sha256") != provenance.sha256(manifest_path):
        raise RuntimeError("matrix manifest SHA-256 differs from the qualification manifest")
    expected = {case["id"]: case for case in manifest["cases"]}
    results = report.get("cases", [])
    if not expected or len(results) != len(expected) \
            or {result.get("id") for result in results} != set(expected):
        raise RuntimeError("matrix is incomplete or contains duplicate/unknown cases")
    expected_inputs = {
        case["input"]: provenance.sha256(source_root / case["input"])
        for case in expected.values()
    }
    if report.get("input_sha256") != expected_inputs:
        raise RuntimeError("matrix canonical input SHA-256 mismatch")
    if report.get("runtime_inputs") != backend_validation.runtime_case_inputs(manifest["cases"], source_root):
        raise RuntimeError("matrix runtime dependency identity mismatch")
    for result in results:
        case = expected[result["id"]]
        checkpoints = result.get("checkpoints", [])
        steps = case["accepted_steps"]
        if len(checkpoints) != len(steps):
            raise RuntimeError(f"{case['id']}: incomplete checkpoint coverage")
        for checkpoint, step in zip(checkpoints, steps):
            check_comparison(checkpoint.get("parity", {}), case["reduction_policy"], steps=step,
                             comparison_mode=backend_validation.checkpoint_comparison_mode(case))
            for backend in ("cpu", "cuda"):
                lane = checkpoint.get(backend, {})
                check_lane(lane, backend=backend, steps=step, initial=True, trace=backend == "cuda", regrid=True,
                           rkl_policy=case.get("rkl_policy") if backend == "cuda" else None,
                           plan_policy=case.get("plan_policy"))
                check_qualification(checkpoint.get(backend + "_qualification"), case)
                conservation = checkpoint.get(backend + "_conservation")
                if case.get("conservation_policy"):
                    require_keys(conservation, ("before", "after", "errors"), "conservation")
                    recomputed = backend_validation.validate_conservation_metrics(
                        conservation["before"], conservation["after"], case["conservation_policy"])
                    if recomputed != conservation:
                        raise RuntimeError("conservation summary differs from recorded metrics")
                    if case["conservation_policy"].get("measure") == "physical_cell_volume":
                        require_keys(lane, ("parameter_file", "parameter_sha256"), "physical-measure lane")
                        if not lane["parameter_file"] or lane["parameter_sha256"] != conservation["before"]["parameter_sha256"]:
                            raise RuntimeError("physical conservation parameter identity differs from actual lane")
                elif conservation != {"status": "not-requested"}:
                    raise RuntimeError("missing not-requested conservation record")
        policy = case.get("topology_policy", {})
        transitions = backend_validation.validate_topology_policy(
            [entry["parity"] for entry in checkpoints], policy, case["id"])
        if transitions is not None and result.get("topology_transitions") != transitions:
            raise RuntimeError("topology transitions differ from recorded checkpoint topology")
        if "scientific_time" in case:
            scientific = result.get("scientific", {})
            require_keys(scientific, ("cpu", "cuda", "parity", "cpu_qualification",
                                      "cuda_qualification"), "scientific run")
            backend_validation.validate_scientific_steps(scientific["cpu"], scientific["cuda"], case)
            check_comparison(scientific["parity"], case["reduction_policy"], steps=scientific["cpu"]["steps"],
                             comparison_mode="physical-time", target_time=case["scientific_time"])
            if scientific["parity"]["candidate_step"] != scientific["cuda"]["steps"]:
                raise RuntimeError("scientific comparison candidate-step identity differs")
            for backend in ("cpu", "cuda"):
                lane = scientific[backend]
                check_lane(lane, backend=backend, steps=lane["steps"], trace=backend == "cuda", regrid=True,
                           plan_policy=case.get("plan_policy"))
                check_qualification(scientific[backend + "_qualification"], case, scientific=True)
                metadata = lane.get("checkpoint_metadata", {})
                require_keys(metadata, ("time", "checkpoint_sha256", "parameter_sha256"), "scientific checkpoint metadata")
                if metadata["checkpoint_sha256"] != lane["checkpoint_sha256"] \
                        or metadata["parameter_sha256"] != lane["parameter_sha256"]:
                    raise RuntimeError("scientific checkpoint metadata identity differs")
                if metadata["time"] != case["scientific_time"]:
                    raise RuntimeError("scientific terminal time drifted")
    if report.get("resolution_groups") != backend_validation.validate_resolution_groups(manifest["cases"], results):
        raise RuntimeError("resolution group qualification is missing or differs")


def check_restart(report: dict[str, Any], *, problem: str, input_path: Path,
                  source_root: Path, checkpoint_validator: Path | None = None) -> None:
    if report.get("problem") != problem \
            or report.get("input_sha256") != provenance.sha256(input_path):
        raise RuntimeError("restart problem/canonical input mismatch")
    expected_inputs = provenance.runtime_inputs(
        parameter_file=input_path, working_directory=source_root,
        parameter_reader=backend_validation.read_parameter_map)
    if report.get("runtime_inputs") != expected_inputs:
        raise RuntimeError("restart runtime dependency identity mismatch")
    routes = {name: (backend, source.removesuffix("_source"))
              for name, backend, source in restart_validation.lane_contract()}
    expected_routes = set(routes)
    comparisons = report.get("comparisons", [])
    if len(comparisons) != len(expected_routes) + 1 \
            or {item.get("route") for item in comparisons} != (
                expected_routes | {"cpu_vs_cuda_continuous"}):
        raise RuntimeError("restart comparison route coverage is incomplete/duplicated")
    def check_metadata(metadata, checkpoint, parameters, steps, phase):
        require_keys(metadata, ("step", "time", "checkpoint_sha256", "parameter_sha256",
                                "resume_after_regrid", "chk_file_index", "plt_file_index"), "restart metadata")
        if type(metadata["step"]) is not int or metadata["step"] != steps \
                or metadata["resume_after_regrid"] is not phase \
                or not isinstance(metadata["time"], (int, float)) \
                or not math.isfinite(metadata["time"]) \
                or any(type(metadata[field]) is not int or metadata[field] < 0
                       for field in ("chk_file_index", "plt_file_index")) \
                or any(re.fullmatch(r"[0-9a-f]{64}", metadata[field]) is None
                       for field in ("checkpoint_sha256", "parameter_sha256")):
            raise RuntimeError("restart checkpoint metadata step/phase/identity mismatch")
        if checkpoint_validator is not None:
            observed = backend_validation.checkpoint_metadata(validator=checkpoint_validator,
                checkpoint=Path(checkpoint), parameters=Path(parameters), expected_steps=steps)
            if observed != metadata:
                raise RuntimeError("restart recorded metadata differs from actual checkpoint")

    for key, steps, run_steps in (("continuous", 4, 4), ("sources", 2, 3)):
        lanes = report.get(key, {})
        if set(lanes) != {"cpu", "cuda"} or any(
            lane.get("backend") != backend or lane.get("steps") != steps
            for backend, lane in lanes.items()
        ):
            raise RuntimeError(f"restart {key} lane coverage is incomplete")
        for backend, lane in lanes.items():
            check_lane(lane, backend=backend, steps=steps)
            if lane.get("run_completed_steps") != run_steps:
                raise RuntimeError("restart process-step coverage mismatch")
            check_metadata(lane.get("checkpoint_metadata"), lane["checkpoint"],
                           lane["parameter"], steps, key == "sources")
            if lane["checkpoint_metadata"]["checkpoint_sha256"] != lane["checkpoint_sha256"]:
                raise RuntimeError("restart metadata checkpoint identity mismatch")
            if key == "sources":
                check_metadata(lane.get("terminal_metadata"), lane.get("terminal_checkpoint"),
                               lane["parameter"], 3, False)
    resumed = report.get("resumed", [])
    if len(resumed) != len(expected_routes) \
            or {lane.get("name") for lane in resumed} != expected_routes \
            or any(lane.get("steps") != 4 or lane.get("backend") !=
                   routes[lane["name"]][0] for lane in resumed):
        raise RuntimeError("restart resumed lane coverage is incomplete")
    for lane in resumed:
        check_lane(lane, backend=lane["backend"], steps=4)
        terminal = lane["name"].endswith("_terminal")
        source = report["sources"][routes[lane["name"]][1]]
        source_checkpoint = source["terminal_checkpoint" if terminal else "checkpoint"]
        source_metadata = source["terminal_metadata" if terminal else "checkpoint_metadata"]
        expected_restore = {"checkpoint": str(Path(source_checkpoint).resolve()),
                            "parameters": source["parameter"], **source_metadata}
        if lane.get("restore_confirmed") is not True or lane.get("restored_from") != expected_restore:
            raise RuntimeError("restart source/restore witness mismatch")
        if lane.get("run_completed_steps") != 4:
            raise RuntimeError("restart resumed process-step coverage mismatch")
        check_metadata(lane.get("checkpoint_metadata"), lane["checkpoint"], lane["parameter"], 4, False)
        if lane["checkpoint_metadata"]["checkpoint_sha256"] != lane["checkpoint_sha256"]:
            raise RuntimeError("resumed metadata checkpoint identity mismatch")
    for comparison in comparisons:
        route = comparison["route"]
        source = report["sources"][routes[route][1]] if route.endswith("_terminal") else None
        check_comparison(comparison, restart_validation.comparison_policy(), steps=4,
                         output_offsets=restart_validation.output_index_offsets(source))
        if comparison["max_level"] <= comparison["min_level"]:
            raise RuntimeError("restart comparison did not retain mixed levels")


def runtime_matrix_contract(profile: str, matrix: Path, manifest: Path,
                            curved: Path | None, uniform: Path | None,
                            generated: Path | None = None):
    if profile == "amr":
        if curved is not None or uniform is not None or generated is not None:
            raise RuntimeError("additional matrices require --profile full-runtime")
        return [(matrix, manifest)]
    if profile != "full-runtime" or curved is None or uniform is None or generated is None:
        raise RuntimeError("full-runtime requires Cartesian, curved, uniform and generated-network matrices")
    if manifest.as_posix() != "validation/amr/gpu_cases.json":
        raise RuntimeError("full-runtime uses the canonical Cartesian AMR manifest")
    if len({path.resolve() for path in (matrix, curved, uniform, generated)}) != 4:
        raise RuntimeError("runtime matrices must have distinct evidence files")
    return [(matrix, manifest), (curved, Path("validation/amr/gpu_curvilinear_cases.json")),
            (uniform, Path("validation/backend/cases.json")),
            (generated, Path("validation/network/runtime_cases.json"))]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", type=Path, required=True)
    parser.add_argument("--checkpoint-validator", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, default=Path.cwd())
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--configuration")
    parser.add_argument("--final-artifacts", type=Path, required=True)
    parser.add_argument("--matrix", type=Path, required=True)
    parser.add_argument("--profile", choices=("amr", "full-runtime"), default="amr")
    parser.add_argument("--curved-matrix", type=Path)
    parser.add_argument("--uniform-matrix", type=Path)
    parser.add_argument("--generated-matrix", type=Path)
    parser.add_argument("--restart-smooth", type=Path, required=True)
    parser.add_argument("--restart-enuc", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, default=Path("validation/amr/gpu_cases.json"))
    args = parser.parse_args(argv)
    source_root = args.source_root.resolve()
    matrix_contract = runtime_matrix_contract(args.profile, args.matrix, args.manifest,
                                               args.curved_matrix, args.uniform_matrix,
                                               args.generated_matrix)
    # Check the legacy top-level binary hash first, so old reports cannot be
    # accidentally qualified merely because all recorded numerical tests pass.
    provenance.require_final_artifact_list(args.final_artifacts, args.arch, args.build_dir)
    expected = provenance.capture(
        arch=args.arch, checkpoint_validator=args.checkpoint_validator,
        source_root=source_root, build_dir=args.build_dir,
        configuration=args.configuration)
    def read_report(path):
        report = json.loads(path.read_text(encoding="utf-8"))
        provenance.require_evidence_identity(report, expected)
        return report
    matrices = []
    for path, manifest in matrix_contract:
        report = read_report(path)
        check_matrix(report, source_root / manifest, source_root)
        matrices.append({"manifest": manifest.as_posix(), "cases": len(report["cases"])})
    check_restart(read_report(args.restart_smooth), problem="SmoothAdvection",
                  input_path=source_root / "validation/amr/inputs/smooth_amr80_l1.par",
                  source_root=source_root, checkpoint_validator=args.checkpoint_validator)
    check_restart(read_report(args.restart_enuc), problem="BurnGradient",
                  input_path=source_root / "validation/amr/inputs/burn_enuc_amr.par",
                  source_root=source_root, checkpoint_validator=args.checkpoint_validator)
    # Rechecking also makes artifact replacement while reading reports fail closed.
    provenance.require_unchanged(expected, provenance.capture(
        arch=args.arch, checkpoint_validator=args.checkpoint_validator,
        source_root=source_root, build_dir=args.build_dir,
        configuration=args.configuration))
    print(json.dumps({"status": "pass", "provenance": expected,
                      "qualification_scope": args.profile,
                      "release_qualified": False,
                      "remaining_release_gates": ["independent-physics", "sanitizer", "capacity"],
                      "matrices": matrices,
                      "matrix_cases": sum(matrix["cases"] for matrix in matrices),
                      "restart_suites": 2}, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"CUDA AMR evidence qualification failed: {error}", file=sys.stderr)
        raise SystemExit(1)
