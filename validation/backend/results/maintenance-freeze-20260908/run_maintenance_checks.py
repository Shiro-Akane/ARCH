#!/usr/bin/env python3
"""Record new-build regression and development smoke, not scientific requalification.

Run once after the isolated build completes. Existing result directories are
never reused. Production runners own their checks; this recipe only records
the configured inventory, execution and before/after identities.
"""
from datetime import datetime, timezone
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
BUILD = ROOT / "build/maintenance-freeze-20260908/core"
sys.path.insert(0, str(ROOT / "tools"))
import validation_provenance as provenance
from validate_backend_results import run_arch_with_logs


def now():
    return datetime.now(timezone.utc).isoformat()


def main():
    regression = HERE / "regression"
    smoke = HERE / "smoke"
    smoke_logs = HERE / "smoke-process"
    output = HERE / "runtime-review.json"
    if any(path.exists() for path in (regression, smoke, smoke_logs, output)):
        raise RuntimeError("maintenance runtime output already exists; preserve the previous attempt")
    regression.mkdir()
    arguments = dict(arch=BUILD / "bin/ARCH",
                     checkpoint_validator=BUILD / "arch_cuda_single_level_validation",
                     source_root=ROOT, build_dir=BUILD)
    evidence = {"schema": 1, "scope": "maintenance new-build regression and development smoke",
                "scientific_requalification": False, "status": "running", "started_utc": now(),
                "commands": []}

    def save():
        output.write_text(json.dumps(evidence, indent=2) + "\n")

    def run(command, directory, timeout):
        record = {"command": command, "started_utc": now(), "status": "running"}
        evidence["commands"].append(record)
        save()
        try:
            result = run_arch_with_logs(command, source_root=ROOT, lane_root=directory, timeout=timeout)
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
        recipe = provenance.file_identity(Path(__file__))
        comparison_path = HERE / "relocation-review.json"
        comparison_identity = provenance.file_identity(comparison_path)
        comparison = json.loads(comparison_path.read_text())
        before = provenance.capture(**arguments)
        evidence.update(recipe=recipe, comparison=comparison_identity, identity_before=before)
        if comparison["status"] != "PASS" or before["source"] != comparison["current_source_after"]:
            raise RuntimeError("rebuilt source does not match the reviewed maintenance source")
        ctest_file = provenance.file_identity(BUILD / "CTestTestfile.cmake")
        reviewed = [entry["current"] for entry in comparison["configured_build"]["files"]
                    if entry["relative_path"] == "CTestTestfile.cmake"]
        if reviewed != [ctest_file]:
            raise RuntimeError("CTest commands/properties differ from the reviewed configuration")
        evidence["ctest_definition"] = ctest_file
        inventory_logs = regression / "inventory"
        inventory_logs.mkdir()
        inventory = run(["/usr/bin/ctest", "--test-dir", str(BUILD), "--show-only=json-v1"],
                        inventory_logs, 60)
        (regression / "ctest-inventory.json").write_text(inventory.stdout)
        tests = json.loads(inventory.stdout)["tests"]
        names = [test["name"] for test in tests]
        if len(names) != 98 or len(set(names)) != 98 or names != comparison["configured_build"]["ctest_names"]:
            raise RuntimeError("configured regression inventory differs from the reviewed 98 tests")
        binaries = {test["name"]: Path(test["command"][0]).resolve() for test in tests}
        local = {name: path for name, path in binaries.items() if path.is_relative_to(BUILD)}
        focused = provenance.capture_focused(artifacts=local, source_root=ROOT, build_dir=BUILD)
        external = {name: provenance.file_identity(path) for name, path in binaries.items()
                    if not path.is_relative_to(BUILD)}
        external["ctest"] = provenance.file_identity(Path("/usr/bin/ctest"))
        external["recipe_python"] = provenance.file_identity(Path(sys.executable))
        evidence.update(test_artifacts_before=focused, external_test_executables=external,
                        inventory=provenance.file_identity(regression / "ctest-inventory.json"))
        run(["/usr/bin/ctest", "--test-dir", str(BUILD), "--parallel", "1",
             "--output-on-failure", "--output-junit", str(regression / "ctest.xml")],
            regression, 1200)
        xml = ET.parse(regression / "ctest.xml").getroot()
        cases = list(xml.iter("testcase"))
        if sorted(case.attrib["name"] for case in cases) != sorted(names) or any(
                case.attrib.get("status") != "run" for case in cases) or any(
                case.find(tag) is not None for case in cases for tag in ("failure", "error", "skipped")):
            raise RuntimeError("JUnit does not contain exactly the 98 passing tests without skips")
        evidence["regression"] = {"passed": len(cases), "failed": 0, "skipped": 0,
                                  "junit": provenance.file_identity(regression / "ctest.xml")}
        smoke_logs.mkdir()
        run([sys.executable, "-B", str(ROOT / "tools/smoke_cuda_amr_runtime.py"),
             "--arch", str(arguments["arch"]), "--checkpoint-validator", str(arguments["checkpoint_validator"]),
             "--source-root", str(ROOT), "--output-root", str(smoke), "--threads", "4", "--timeout", "60"],
            smoke_logs, 660)
        report = json.loads((smoke / "smoke-report.json").read_text())
        if report["status"] != "passed" or len(report["lanes"]) != 10 or any(
                lane["status"] != "passed" for lane in report["lanes"]):
            raise RuntimeError("development smoke did not pass all ten source/resume lanes")
        evidence["smoke"] = {"lanes_passed": len(report["lanes"]), "scientific_validation": False,
                             "report": provenance.file_identity(smoke / "smoke-report.json")}
        provenance.require_unchanged(before, provenance.capture(**arguments))
        provenance.require_unchanged(focused, provenance.capture_focused(
            artifacts=local, source_root=ROOT, build_dir=BUILD))
        if external != {name: provenance.file_identity(Path(item["path"])) for name, item in external.items()}:
            raise RuntimeError("external test executable changed during execution")
        if recipe != provenance.file_identity(Path(__file__)):
            raise RuntimeError("maintenance execution recipe changed during execution")
        if ctest_file != provenance.file_identity(BUILD / "CTestTestfile.cmake") or \
                comparison_identity != provenance.file_identity(comparison_path):
            raise RuntimeError("reviewed CTest configuration or source comparison changed during execution")
        evidence.update(status="PASS", identity_verified_after_run=True)
    except Exception as error:
        evidence.update(status="FAIL", error=f"{type(error).__name__}: {error}")
        raise
    finally:
        evidence["completed_utc"] = now()
        save()
    print(json.dumps({"status": evidence["status"], "regression_passed": len(cases),
                      "smoke_lanes_passed": len(report["lanes"]), "report": str(output)}), flush=True)


if __name__ == "__main__":
    main()
