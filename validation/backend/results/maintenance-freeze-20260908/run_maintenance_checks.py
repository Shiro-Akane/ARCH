#!/usr/bin/env python3
"""Record current-build regression, smoke and strict CPU/CUDA restart checks.

Write to a new or empty staging directory. Review the resulting measurements
before replacing the maintained record; this recipe never overwrites an
existing result set or substitutes a smoke run for a scientific campaign.
"""
from datetime import datetime, timezone
import argparse
import json
import math
import os
from pathlib import Path
import shutil
import sys
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT / "tools"))
import smoke_cuda_amr_runtime as smoke_runner
import validate_cuda_amr_restart as restart_runner
import validation_provenance as provenance
import validation_sanitizer
from validate_backend_results import require_empty_output_root, run_arch_with_logs


def now():
    return datetime.now(timezone.utc).isoformat()


def test_names(inventory):
    tests = inventory.get("tests", [])
    names = [test.get("name") for test in tests]
    if not names or any(not isinstance(name, str) or not name for name in names) \
            or len(names) != len(set(names)):
        raise RuntimeError("CTest inventory must contain unique, nonempty test names")
    return names


def require_local_network_profile(build):
    """Keep owner-deferred networks out of this local execution recipe."""
    selected_names = list(filter(None, build["cmake_options"].get("ARCH_CUSTOM_NETWORKS", "").split(";")))
    registered_names = [item["package"] for item in build["registered_networks"]]
    providers = build["sparse_link"]["required_providers"]
    if any(len(items) != len(set(items)) for items in (selected_names, registered_names, providers)):
        raise RuntimeError("local network/provider inventories must not contain duplicate entries")
    selected, registered = set(selected_names), set(registered_names)
    if {"audit150", "audit200"} & (selected | registered):
        raise RuntimeError("audit150/audit200 are owner-deferred; this recipe cannot run them")
    if selected != {"audit31", "weak_urca"} or registered != selected:
        raise RuntimeError("the local reference profile requires only audit31 and weak_urca")
    if set(providers) != {"klu", "cudss"}:
        raise RuntimeError("the local reference profile requires actual CPU KLU and CUDA cuDSS links")


def require_restart_report(path, identity, problem, canonical, sanitizer):
    """Check the existing restart runner's coverage, identity and tool reports."""
    report = json.loads(path.read_text())
    provenance.require_evidence_identity(report, identity)
    if report.get("problem") != problem or Path(report.get("input", "")).resolve() != canonical \
            or report.get("input_sha256") != provenance.sha256(canonical):
        raise RuntimeError("restart evidence does not identify the requested problem/input")
    contract = restart_runner.lane_contract()
    expected = ["cpu_vs_cuda_continuous"] + [name for name, _, _ in contract]
    if len(expected) != 9:
        raise RuntimeError("the maintained restart profile must contain nine comparison routes")
    comparisons = report.get("comparisons", [])
    names = [item.get("route") for item in comparisons]
    if sorted(names) != sorted(expected) or any(item.get("passed") is not True for item in comparisons):
        raise RuntimeError("restart evidence lacks all nine passing comparisons")
    continuous, sources = report.get("continuous", {}), report.get("sources", {})
    if set(continuous) != {"cpu", "cuda"} or set(sources) != {"cpu", "cuda"}:
        raise RuntimeError("restart evidence lacks both continuous and source backends")
    resumed = report.get("resumed", [])
    if sorted(item.get("name", "") for item in resumed) != sorted(name for name, _, _ in contract):
        raise RuntimeError("restart evidence lacks an intermediate or terminal restart direction")
    expected_backend = {name: backend for name, backend, _ in contract}
    lanes = list(continuous.values()) + list(sources.values()) + resumed
    for family, records in (("continuous", continuous), ("source", sources)):
        if any(lane.get("backend") != backend or lane.get("name") != f"{backend}_{family}"
               for backend, lane in records.items()):
            raise RuntimeError("restart source or continuous lane has the wrong backend identity")
    if any(lane.get("backend") != expected_backend[lane["name"]]
           or lane.get("restore_confirmed") is not True for lane in resumed):
        raise RuntimeError("restart direction or confirmed restoration is missing")
    instrumented = 0
    for lane in lanes:
        parameter = Path(lane.get("parameter", "")).resolve()
        checkpoint = Path(lane.get("checkpoint", "")).resolve()
        if not parameter.is_relative_to(path.parent) or not checkpoint.is_relative_to(path.parent) \
                or not parameter.is_file() or not checkpoint.is_file() \
                or lane.get("checkpoint_sha256") != provenance.sha256(checkpoint):
            raise RuntimeError("restart lane does not own its newly measured input/checkpoint")
        observed = lane.get("sanitizer")
        if sanitizer is not None and lane["backend"] == "cuda":
            if observed != sanitizer.evidence(parameter.parent):
                raise RuntimeError("restart sanitizer evidence differs from its actual clean tool report")
            instrumented += 1
        elif observed is not None:
            raise RuntimeError("an ordinary reference lane unexpectedly claims instrumentation")
    if sanitizer is not None and instrumented != 6:
        raise RuntimeError("restart instrumentation must cover all six CUDA application processes")
    return {"comparisons_passed": len(comparisons), "application_lanes": len(lanes),
            "instrumented_cuda_lanes": instrumented,
            "sanitizer_tool": sanitizer.tool if sanitizer else None,
            "report": provenance.file_identity(path)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True,
                        help="completed CMake build with the local CUDA/KLU/cuDSS test profile")
    parser.add_argument("--output-root", type=Path, required=True,
                        help="new or empty staging directory; existing results are never overwritten")
    parser.add_argument("--reference-inventory", type=Path,
                        default=HERE / "regression/ctest-inventory.json",
                        help="maintained complete CTest inventory whose test names must remain present")
    parser.add_argument("--configuration", help="required for a multi-configuration CMake build")
    parser.add_argument("--threads", type=int, default=1,
                        help="OpenMP threads per process; CTest runs one test at a time")
    parser.add_argument("--timeout", type=float, default=3600.0,
                        help="maximum seconds per runner command and per smoke application")
    parser.add_argument("--cuda-sanitizer", type=Path,
                        help="also check BurnGradient restart with both memcheck and racecheck")
    args = parser.parse_args(argv)
    if args.threads < 1 or not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("threads and timeout must be positive")
    build_dir = args.build_dir.resolve()
    output_root = args.output_root.resolve()
    reference_path = args.reference_inventory.resolve()
    require_empty_output_root(output_root)
    if output_root in (ROOT, build_dir, HERE) or output_root in ROOT.parents:
        parser.error("output root must be a separate staging directory")
    output_root.mkdir(parents=True, exist_ok=True)
    os.environ["OMP_NUM_THREADS"] = str(args.threads)
    os.environ["OMP_DYNAMIC"] = "FALSE"
    output = output_root / "runtime-review.json"
    evidence = {
        "schema": 1, "scope": "current-build full regression, development smoke and strict restart",
        "scientific_requalification": False, "status": "running", "started_utc": now(),
        "commands": [], "restart": {}, "staging_root": str(output_root),
        "large_networks": {"status": "owner-deferred", "packages": ["audit150", "audit200"]},
        "sanitizer_requested": args.cuda_sanitizer is not None,
    }

    def save():
        output.write_text(json.dumps(evidence, indent=2) + "\n")

    def run(command, directory):
        directory.mkdir(parents=True, exist_ok=True)
        record = {"command": command, "started_utc": now(), "status": "running"}
        evidence["commands"].append(record)
        save()
        try:
            result = run_arch_with_logs(command, source_root=ROOT, lane_root=directory,
                                        timeout=args.timeout)
            record.update(returncode=result.returncode,
                          status="passed" if result.returncode == 0 else "failed")
        except Exception as error:
            record.update(status="failed", error=f"{type(error).__name__}: {error}")
            raise
        finally:
            record["completed_utc"] = now()
            for stream in ("stdout", "stderr"):
                path = directory / f"arch.{stream}"
                if path.is_file():
                    record[stream] = provenance.file_identity(path)
            save()
        if result.returncode:
            raise RuntimeError(f"command failed with exit {result.returncode}: {command[0]}")
        return result

    save()
    try:
        build = provenance.build_identity(build_dir, args.configuration)
        require_local_network_profile(build)
        artifacts = provenance._configured_artifact_paths(build_dir, build)
        arguments = dict(**artifacts, source_root=ROOT, build_dir=build_dir,
                         configuration=args.configuration)
        before = provenance.capture(**arguments)
        ctest = shutil.which("ctest")
        if ctest is None:
            raise RuntimeError("ctest executable was not found")
        reference = json.loads(reference_path.read_text())
        reference_names = test_names(reference)
        if len(reference_names) != 98:
            raise RuntimeError("the maintained local reference inventory must contain 98 tests")
        metadata_paths = {
            "recipe": Path(__file__).resolve(), "reference_inventory": reference_path,
            "ctest_definition": build_dir / "CTestTestfile.cmake",
            "smoke_runner": ROOT / "tools/smoke_cuda_amr_runtime.py",
            "restart_runner": ROOT / "tools/validate_cuda_amr_restart.py",
            "smoke_manifest": ROOT / "tests/smoke/cuda_amr_cases.json",
        }
        metadata_paths.update({
            "ctest:" + path.relative_to(build_dir).as_posix(): path
            for path in build_dir.rglob("CTestTestfile.cmake")
        })
        metadata_before = {name: provenance.file_identity(path) for name, path in metadata_paths.items()}
        metadata_observation = provenance._artifact_observations(metadata_paths)
        # Retain command paths so a changed executable symlink is observed too.
        external_paths = {"ctest": Path(ctest), "recipe_python": Path(sys.executable)}
        if args.cuda_sanitizer is not None:
            external_paths["cuda_sanitizer"] = args.cuda_sanitizer
        evidence.update(identity_before=before, metadata_before=metadata_before,
                        metadata_observation_before=metadata_observation)
        regression = output_root / "regression"
        inventory = run([ctest, "--test-dir", str(build_dir), "-C", build["configuration"],
                         "--show-only=json-v1"], regression / "inventory")
        inventory_path = regression / "ctest-inventory.json"
        inventory_path.write_text(inventory.stdout)
        configured = json.loads(inventory.stdout)
        names = test_names(configured)
        if not set(reference_names).issubset(names):
            raise RuntimeError("configured CTest inventory omits maintained reference tests")
        if any(package in name.lower() for package in ("audit150", "audit200") for name in names):
            raise RuntimeError("configured tests include owner-deferred large-network work")
        local = {}
        for test in configured["tests"]:
            command = test.get("command", [])
            if not command:
                raise RuntimeError(f"CTest command is missing: {test['name']}")
            binary = Path(command[0]).resolve()
            if not binary.is_file():
                raise RuntimeError(f"CTest executable is missing: {binary}")
            if binary.is_relative_to(build_dir):
                local[test["name"]] = binary
            else:
                external_paths[test["name"]] = Path(command[0])
            for index, item in enumerate(command[1:], 1):
                path = Path(item)
                if path.is_absolute() and path.is_file() and path.resolve().is_relative_to(build_dir):
                    local[f"{test['name']}:argument-{index}"] = path.resolve()
        focused = provenance.capture_focused(artifacts=local, source_root=ROOT, build_dir=build_dir,
                                             configuration=args.configuration)
        provenance.require_unchanged(before, provenance.capture(**arguments))
        external_before = {name: provenance.file_identity(path) for name, path in external_paths.items()}
        external_observation = provenance._artifact_observations(external_paths)
        evidence.update(test_artifacts_before=focused, external_test_executables=external_before,
                        external_observation_before=external_observation,
                        inventory=provenance.file_identity(inventory_path),
                        reference_tests_required=len(reference_names), configured_tests=len(names))
        run([ctest, "--test-dir", str(build_dir), "-C", build["configuration"], "--parallel", "1",
             "--output-on-failure", "--timeout", str(args.timeout),
             "--output-junit", str(regression / "ctest.xml")], regression)
        cases = list(ET.parse(regression / "ctest.xml").getroot().iter("testcase"))
        if sorted(case.attrib.get("name", "") for case in cases) != sorted(names) or any(
                case.attrib.get("status") != "run" for case in cases) or any(
                case.find(tag) is not None for case in cases for tag in ("failure", "error", "skipped")):
            raise RuntimeError("JUnit does not contain every configured test passing without skips")
        evidence["regression"] = {"passed": len(cases), "failed": 0, "skipped": 0,
                                  "junit": provenance.file_identity(regression / "ctest.xml")}
        smoke = output_root / "smoke"
        manifest = metadata_paths["smoke_manifest"]
        expected_smoke = {
            f"{case['id']}_{backend}_{phase}"
            for case in smoke_runner.cases_from(manifest) for backend in ("cpu", "cuda")
            for phase in (("source", "resume") if case.get("restart_steps", 0) else ("source",))
        }
        if len(expected_smoke) != 10:
            raise RuntimeError("the maintained smoke profile must contain ten application lanes")
        run([sys.executable, "-B", str(metadata_paths["smoke_runner"]),
             "--arch", str(artifacts["arch"]), "--checkpoint-validator", str(artifacts["checkpoint_validator"]),
             "--source-root", str(ROOT), "--manifest", str(manifest), "--output-root", str(smoke),
             "--threads", str(args.threads), "--timeout", str(args.timeout)], output_root / "smoke-process")
        report = json.loads((smoke / "smoke-report.json").read_text())
        lanes = report.get("lanes", [])
        if report.get("status") != "passed" or report.get("scientific_validation") is not False \
                or sorted(lane.get("name", "") for lane in lanes) != sorted(expected_smoke) \
                or any(lane.get("status") != "passed" or lane.get("returncode") != 0 for lane in lanes) \
                or report.get("binary_unchanged") is not True \
                or report.get("checkpoint_validator_unchanged") is not True \
                or report.get("arch_sha256") != before["artifacts"]["arch_sha256"] \
                or report.get("checkpoint_validator_sha256") != before["artifacts"]["checkpoint_validator_sha256"] \
                or report.get("manifest_sha256") != provenance.sha256(manifest):
            raise RuntimeError("development smoke did not verify all ten current-artifact lanes")
        evidence["smoke"] = {"lanes_passed": len(lanes), "scientific_validation": False,
                             "report": provenance.file_identity(smoke / "smoke-report.json")}

        restart_profiles = [
            ("smooth", "SmoothAdvection", ROOT / "validation/amr/inputs/smooth_amr80_l1.par", None),
            ("burn", "BurnGradient", ROOT / "validation/amr/inputs/burn_enuc_amr.par", None),
        ]
        if args.cuda_sanitizer is not None:
            restart_profiles += [
                (f"burn-{tool}", "BurnGradient", ROOT / "validation/amr/inputs/burn_enuc_amr.par", tool)
                for tool in ("memcheck", "racecheck")
            ]
        for name, problem, canonical, tool in restart_profiles:
            destination = output_root / "restart" / name
            command = [sys.executable, "-B", str(metadata_paths["restart_runner"]),
                       "--arch", str(artifacts["arch"]), "--checkpoint-validator", str(artifacts["checkpoint_validator"]),
                       "--source-root", str(ROOT), "--build-dir", str(build_dir),
                       "--configuration", build["configuration"], "--input", str(canonical),
                       "--problem", problem, "--output-root", str(destination)]
            sanitizer = None
            if tool:
                sanitizer = validation_sanitizer.CudaSanitizer(args.cuda_sanitizer, tool)
                command += ["--cuda-sanitizer", str(sanitizer.executable), "--sanitizer-tool", tool]
            run(command, output_root / "restart-process" / name)
            evidence["restart"][name] = require_restart_report(
                destination / "restart-validation-evidence.json", before, problem, canonical, sanitizer)
            save()

        inventory_after = run(
            [ctest, "--test-dir", str(build_dir), "-C", build["configuration"], "--show-only=json-v1"],
            regression / "inventory-after")
        if json.loads(inventory_after.stdout) != configured:
            raise RuntimeError("configured CTest inventory changed during execution")
        after = provenance.capture(**arguments)
        provenance.require_unchanged(before, after)
        focused_after = provenance.capture_focused(
            artifacts=local, source_root=ROOT, build_dir=build_dir, configuration=args.configuration)
        provenance.require_unchanged(focused, focused_after)
        if external_before != {name: provenance.file_identity(path) for name, path in external_paths.items()} \
                or external_observation != provenance._artifact_observations(external_paths):
            raise RuntimeError("external test executable or sanitizer changed during execution")
        if metadata_before != {name: provenance.file_identity(path) for name, path in metadata_paths.items()} \
                or metadata_observation != provenance._artifact_observations(metadata_paths):
            raise RuntimeError("execution recipe, reference inventory or configured inputs changed during execution")
        evidence.update(status="PASS", identity_after=after, identity_verified_after_run=True,
                        test_artifacts_identity_verified_after_run=True)
    except Exception as error:
        evidence.update(status="FAIL", error=f"{type(error).__name__}: {error}")
        raise
    finally:
        evidence["completed_utc"] = now()
        save()
    print(json.dumps({"status": evidence["status"],
                      "regression_passed": evidence["regression"]["passed"],
                      "smoke_lanes_passed": evidence["smoke"]["lanes_passed"],
                      "restart_profiles": list(evidence["restart"]), "report": str(output)}), flush=True)


if __name__ == "__main__":
    main()
