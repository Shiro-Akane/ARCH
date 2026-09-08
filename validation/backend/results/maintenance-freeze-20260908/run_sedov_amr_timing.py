"""Serial, matched CPU/CUDA Sedov AMR timing using the existing ARCH validators.

Only ARCH process creation-to-exit is timed. Initialization, computation, AMR,
forced endpoint output and shutdown are included; parameter generation and all
validation are excluded. This is not a kernel-only timer or a speed threshold.
"""
import argparse
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys
import threading
import time

ROOT = next(parent for parent in Path(__file__).resolve().parents
            if (parent / "CMakeLists.txt").is_file())
sys.path.insert(0, str(ROOT / "tools"))
import validate_backend_results as validation
import validation_provenance as provenance
import smoke_cuda_amr_runtime as smoke

CANONICAL = ROOT / "simulation/Sedov/Sedov.par"
POLICY_MANIFEST = ROOT / "validation/amr/gpu_cases.json"
RECIPE_DEPENDENCIES = {
    "recipe": Path(__file__).resolve(),
    "backend_validation": Path(validation.__file__).resolve(),
    "provenance": Path(provenance.__file__).resolve(),
    "smoke": Path(smoke.__file__).resolve(),
    "sanitizer": Path(validation.validation_sanitizer.__file__).resolve(),
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def capacity(blocks, levels):
    full_tree = blocks ** 2 * sum(4 ** level for level in range(levels + 1))
    with_margin = (5 * full_tree + 3) // 4
    return 1 << (with_margin - 1).bit_length()


def regrid_groups(observation):
    groups = {}
    for label, rows in (("initial_step_zero", [r for r in observation["records"] if r["macro_step"] == 0]),
                        ("runtime", [r for r in observation["records"] if r["macro_step"] > 0])):
        groups[label] = {"events": len(rows),
                         "topology_changes": sum(r["topology_changed"] for r in rows),
                         "wall_seconds": math.fsum(r["wall_seconds"] for r in rows),
                         "block_sequence": [{key: row[key] for key in
                                             ("macro_step", "physical_time", "old_blocks", "new_blocks", "topology_changed")}
                                            for row in rows]}
    return groups


def run_lane(args, blocks, backend, phase, repeat, lane, save):
    directory = args.output_root / f"blocks-{blocks}" / f"{phase}-{repeat:02d}-{backend}"
    directory.mkdir(parents=True)
    parameter = directory / "run.par"
    overrides = {"nblockx1": blocks, "nblockx2": blocks, "nblockx3": 0,
                 "max_blocks": capacity(blocks, args.levels), "lrefinemin": 0,
                 "lrefinemax": args.levels, "regrid_interval": 2,
                 "solver": "HLLC", "reconstruct": "ppm", "time_integrator": "RK3",
                 "cfl": 0.4, "refine_threshold": 0.4, "derefine_threshold": 0.1,
                 "max_steps": -1, "chk_dt": -1, "chk_dstep": -1,
                 "plt_dt": -1, "plt_dstep": -1}
    validation.render_terminal_parameter_file(
        CANONICAL, parameter, backend=backend, output_dir=directory,
        base_name="SedovTiming", terminal_time=args.physical_time,
        scientific_overrides=overrides)
    lane.update(backend=backend, blocks_per_axis=blocks, phase=phase, repeat=repeat,
                directory=str(directory), parameter=provenance.file_identity(parameter),
                effective_parameters=validation.read_parameter_map(parameter),
                command=[str(args.arch), "Sedov", str(parameter)],
                measurement_status="running", numerical_status="pending", returncode=None)
    save()
    environment = dict(os.environ, OMP_NUM_THREADS=str(args.threads), OMP_DYNAMIC="FALSE",
                       OMP_PLACES="cores", OMP_PROC_BIND="close")
    timed_out = False
    with (directory / "arch.stdout").open("w") as stdout, (directory / "arch.stderr").open("w") as stderr:
        started = time.perf_counter()
        process = subprocess.Popen(lane["command"], cwd=ROOT, env=environment,
                                   stdout=stdout, stderr=stderr)

        def timeout_process():
            nonlocal timed_out
            if process.poll() is None:
                timed_out = True
                process.kill()

        # wait(timeout=...) polls on POSIX and can round a short run upwards by
        # tens of milliseconds. A deadline callback leaves waitpid blocking.
        deadline = threading.Timer(args.timeout, timeout_process)
        deadline.daemon = True
        deadline.start()
        try:
            returncode = process.wait()
        except BaseException:
            process.kill()
            process.wait()
            raise
        finally:
            lane["arch_wall_seconds"] = time.perf_counter() - started
            deadline.cancel()
    lane.update(returncode=returncode, timed_out=timed_out,
                measurement_status="passed" if returncode == 0 and not timed_out else "failed",
                stdout=provenance.file_identity(directory / "arch.stdout"),
                stderr=provenance.file_identity(directory / "arch.stderr"))
    save()
    require(returncode == 0 and not timed_out, f"ARCH failed/timed out: {directory}")
    require(provenance.file_identity(parameter) == lane["parameter"], "run parameter changed during execution")
    initial = directory / "SedovTiming_chk_0000.h5"
    final = directory / "SedovTiming_chk_0001.h5"
    require(sorted(p.name for p in directory.glob("*_chk_*.h5")) == [initial.name, final.name],
            "expected only the forced initial and terminal checkpoints")
    lane["initial_metadata"] = validation.checkpoint_metadata(
        validator=args.checkpoint_validator, checkpoint=initial, parameters=parameter, expected_steps=0)
    lane["final_metadata"] = validation.checkpoint_metadata(
        validator=args.checkpoint_validator, checkpoint=final, parameters=parameter, expected_steps=None)
    steps = lane["final_metadata"]["step"]
    match = validation.STEP_RE.search((directory / "arch.stdout").read_text())
    require(steps > 0 and match is not None and int(match.group(1)) == steps,
            "stdout and terminal checkpoint accepted-step counts disagree")
    lane["checkpoint"] = str(final)
    lane["resolved_plan"] = validation.validate_resolved_plan(
        directory / "SedovTiming_backend_plan.txt", backend)
    lane["terminal_time_check"] = validation.compare_hdf5_checkpoints(
        final, final, args.field_policy, args.checkpoint_validator,
        comparison_mode="physical-time", target_time=args.physical_time)
    # The inherited Sedov conservation budget uses level-normalized Cartesian
    # totals. Compare the actual initial checkpoint, not a theoretical E=1.
    before = validation.read_conservation_metrics(args.checkpoint_validator, initial)
    after = validation.read_conservation_metrics(args.checkpoint_validator, final)
    lane["initial_leaf_count"] = before.get("blocks")
    lane["final_leaf_count"] = after.get("blocks")
    try:
        lane["conservation"] = {"passed": True, **validation.validate_conservation_metrics(
            before, after, args.conservation_policy)}
    except RuntimeError as error:
        lane["conservation"] = {"passed": False, "error": str(error), "before": before, "after": after}
    lane["regrid"] = validation.read_regrid_metrics(
        directory / "SedovTiming_regrid.tsv", backend, steps)
    lane["regrid_groups"] = regrid_groups(lane["regrid"])
    lane["runtime_dynamic_amr"] = lane["regrid_groups"]["runtime"]["topology_changes"] > 0
    require(all(row["physical_time"] <= args.physical_time
                or math.isclose(row["physical_time"], args.physical_time, rel_tol=1e-12)
                for row in lane["regrid"]["records"]),
            "regrid observation exceeds the prescribed terminal time")
    if backend == "cuda":
        lane["cuda_trace"] = smoke.trace_summary(directory / "SedovTiming_backend_trace.tsv", steps, False)
    lane["numerical_status"] = ("passed" if lane["terminal_time_check"]["passed"]
                                and lane["conservation"]["passed"] else "failed")
    save()


def compare_lanes(args, reference, candidate, kind):
    result = validation.compare_hdf5_checkpoints(
        Path(reference["checkpoint"]), Path(candidate["checkpoint"]), args.field_policy,
        args.checkpoint_validator, comparison_mode="physical-time", target_time=args.physical_time)
    keys = ("macro_step", "old_blocks", "new_blocks", "topology_changed")
    left = [tuple(row[key] for key in keys) for row in reference["regrid"]["records"] if row["macro_step"] > 0]
    right = [tuple(row[key] for key in keys) for row in candidate["regrid"]["records"] if row["macro_step"] > 0]
    left_times = [row["physical_time"] for row in reference["regrid"]["records"] if row["macro_step"] > 0]
    right_times = [row["physical_time"] for row in candidate["regrid"]["records"] if row["macro_step"] > 0]
    # Match the controller's relative terminal-time roundoff allowance.
    times_match = len(left_times) == len(right_times) and all(
        math.isclose(a, b, rel_tol=1e-12) for a, b in zip(left_times, right_times))
    def scientific_parameters(lane):
        return {key: value for key, value in lane["effective_parameters"].items()
                if key not in {"compute_backend", "out_dir"}}
    parameters_match = scientific_parameters(reference) == scientific_parameters(candidate)
    aligned = (reference["final_metadata"]["step"] == candidate["final_metadata"]["step"]
               and left == right and times_match and parameters_match)
    return {"kind": kind, "reference": reference["directory"], "candidate": candidate["directory"],
            "field_comparison": result, "workload_aligned": aligned,
            "parameters_match": parameters_match, "regrid_physical_times_match": times_match,
            "regrid_time_rtol": 1e-12,
            "alignment_scope": "same scientific parameters, accepted steps and runtime regrid step/time/old/new/changed sequence; initialization and wall times are reported separately"}


def qualified_measurements(lanes, comparisons):
    """A speed observation requires matching work and actual runtime AMR."""
    return bool(lanes) and all(
        lane["numerical_status"] == "passed" and lane["runtime_dynamic_amr"] for lane in lanes
    ) and all(item["field_comparison"]["passed"] and item["workload_aligned"] for item in comparisons)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", type=Path, required=True)
    parser.add_argument("--checkpoint-validator", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True, help="absent/empty directory under this checkout's ignored build/")
    parser.add_argument("--blocks", type=int, nargs="+", choices=(4, 8, 16), default=[4, 16])
    parser.add_argument("--levels", type=int, choices=(2,), default=2)
    parser.add_argument("--time", dest="physical_time", type=float, default=0.005)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--backend", nargs="+", choices=("cpu", "cuda"), default=["cpu", "cuda"])
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=1200, help="seconds per ARCH process")
    parser.add_argument("--pilot", action="store_true", help="one measured round and no warmups, through the same execution/validation path")
    args = parser.parse_args()
    require(args.threads > 0 and args.repeats > 0 and args.warmups >= 0
            and math.isfinite(args.physical_time) and args.physical_time > 0
            and math.isfinite(args.timeout) and args.timeout > 0, "invalid timing/count argument")
    require(len(set(args.backend)) == len(args.backend) and len(set(args.blocks)) == len(args.blocks),
            "duplicate backend or scale selection")
    for name in ("arch", "checkpoint_validator", "output_root"):
        setattr(args, name, getattr(args, name).resolve())
    require(args.output_root.is_relative_to(ROOT / "build") and args.output_root != ROOT / "build",
            "benchmark outputs must remain under the ignored build directory")
    require(args.arch.is_file() and args.checkpoint_validator.is_file(), "build ARCH and its same-build comparator first")
    validation.require_empty_output_root(args.output_root)
    args.output_root.mkdir(parents=True, exist_ok=True)
    if args.pilot:
        args.repeats, args.warmups = 1, 0
    policy_case = next(case for case in json.loads(POLICY_MANIFEST.read_text())["cases"]
                       if case["id"] == "hydro_amr_rk3_2d" and case["problem"] == "Sedov")
    args.field_policy = policy_case["reduction_policy"]
    args.conservation_policy = policy_case["conservation_policy"]
    capture_args = dict(arch=args.arch, checkpoint_validator=args.checkpoint_validator,
                        source_root=ROOT, build_dir=args.checkpoint_validator.parent)
    report = {"schema": 1, "status": "running", "measurement_status": "pending",
              "numerical_status": "pending", "started_utc": datetime.now(timezone.utc).isoformat(),
              "scope": __doc__, "canonical_input": provenance.file_identity(CANONICAL),
              "policy_source": provenance.file_identity(POLICY_MANIFEST),
              "recipe_and_helpers_before": {name: provenance.file_identity(path)
                                            for name, path in RECIPE_DEPENDENCIES.items()},
              "field_policy": args.field_policy, "conservation_policy": args.conservation_policy,
              "settings": {key: value for key, value in vars(args).items() if key not in ("field_policy", "conservation_policy")},
              "environment": {"OMP_NUM_THREADS": str(args.threads), "OMP_DYNAMIC": "FALSE",
                              "OMP_PLACES": "cores", "OMP_PROC_BIND": "close"},
              "capacity_policy": "ceil_pow2(1.25 * blocks_per_axis^2 * sum(4^level, level=0..levels)); identical on both backends",
              "timing_policy": "Sequential alternating backend order; ARCH end-to-end only; warmups excluded from statistics; no speed threshold",
              "lanes": [], "comparisons": [], "statistics": {}}
    output = args.output_root / "timing-report.json"

    def save():
        output.write_text(json.dumps(report, indent=2, default=str, allow_nan=False) + "\n")

    save()
    try:
        report["identity_before"] = provenance.capture(**capture_args)
        for blocks in args.blocks:
            references = {}
            for index in range(args.warmups + args.repeats):
                phase = "warmup" if index < args.warmups else "measured"
                repeat = index if phase == "warmup" else index - args.warmups
                order = args.backend if index % 2 == 0 else list(reversed(args.backend))
                round_lanes = {}
                for backend in order:
                    lane = {}
                    report["lanes"].append(lane)
                    run_lane(args, blocks, backend, phase, repeat, lane, save)
                    round_lanes[backend] = lane
                    if phase == "measured":
                        if backend in references:
                            report["comparisons"].append(compare_lanes(args, references[backend], lane, "same-backend-repeat"))
                        else:
                            references[backend] = lane
                if set(round_lanes) == {"cpu", "cuda"}:
                    report["comparisons"].append(compare_lanes(args, round_lanes["cpu"], round_lanes["cuda"], "cpu-vs-cuda"))
                save()
            stats = {}
            for backend in args.backend:
                durations = [lane["arch_wall_seconds"] for lane in report["lanes"]
                             if lane["blocks_per_axis"] == blocks and lane["backend"] == backend
                             and lane["phase"] == "measured"]
                stats[backend] = {"seconds": durations, "median": statistics.median(durations),
                                  "minimum": min(durations), "maximum": max(durations)}
            if set(stats) == {"cpu", "cuda"}:
                stats["cpu_over_cuda_median_ratio"] = stats["cpu"]["median"] / stats["cuda"]["median"]
                scale_lanes = [lane for lane in report["lanes"] if lane["blocks_per_axis"] == blocks]
                scale_paths = {lane["directory"] for lane in scale_lanes}
                stats["speedup_qualified"] = qualified_measurements(
                    scale_lanes, [item for item in report["comparisons"] if item["reference"] in scale_paths])
            report["statistics"][str(blocks)] = stats
        report["measurement_status"] = "passed"
        report["numerical_status"] = ("passed" if all(lane["numerical_status"] == "passed" for lane in report["lanes"])
                                      and all(item["field_comparison"]["passed"] for item in report["comparisons"]) else "failed")
        report["workload_alignment_status"] = ("not-compared" if not report["comparisons"] else
                                                "passed" if all(item["workload_aligned"] for item in report["comparisons"])
                                                else "different-observed-work")
        report["runtime_dynamic_amr_status"] = (
            "passed" if all(lane["runtime_dynamic_amr"] for lane in report["lanes"])
            else "no-runtime-topology-change")
        report["identity_after"] = provenance.capture(**capture_args)
        provenance.require_unchanged(report["identity_before"], report["identity_after"])
        report["identity_verified_after_run"] = True
        report["recipe_and_helpers_after"] = {name: provenance.file_identity(path)
                                             for name, path in RECIPE_DEPENDENCIES.items()}
        require(report["recipe_and_helpers_before"] == report["recipe_and_helpers_after"],
                "measurement recipe or shared helper changed during execution")
        report["recipe_and_helpers_verified_after_run"] = True
        report["status"] = ("passed" if qualified_measurements(report["lanes"], report["comparisons"])
                            else "comparison-qualification-failed")
    except Exception as error:
        report["status"] = "failed"
        report["error"] = f"{type(error).__name__}: {error}"
        completed = sum(lane.get("measurement_status") == "passed" for lane in report["lanes"])
        expected = len(args.blocks) * len(args.backend) * (args.warmups + args.repeats)
        report["measurement_status"] = "passed" if completed == expected else "partial" if completed else "failed"
        report["numerical_status"] = "incomplete"
    finally:
        report["completed_utc"] = datetime.now(timezone.utc).isoformat()
        save()
    print(json.dumps({"status": report["status"], "report": str(output), "statistics": report["statistics"]}, indent=2))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
