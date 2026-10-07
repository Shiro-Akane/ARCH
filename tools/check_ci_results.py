#!/usr/bin/env python3
"""Check CTest coverage and Node completion, without redefining test physics.

CTest owns test discovery, execution and numerical pass/fail decisions. This
reader rejects incomplete or skipped runs that would otherwise look successful.
Node reports use a separate completion mode; no CTest scientific coverage is implied.
It does not create or replace scientific Validation evidence.
"""

import argparse
import json
import re
from pathlib import Path
import sys
import xml.etree.ElementTree as ET


# Existing parser / entry / API tests; removing their CMake registration must
# not silently shrink a "complete" CPU report after the v3 contract migration.
CONFIGURATION_COVERAGE_ANCHORS = frozenset({
    "config_input_records", "input_resolution", "case_configuration", "configuration_input",
    "configuration_api_contract", "configuration_entry_contract", "configuration_v3_contract",
})
PREVIEW_COVERAGE_ANCHORS = frozenset({
    "preview_initial_conversion", "preview_api_contract", "preview_full_model_contract",
    "preview_parameter_reads", "preview_parameter_metadata", "preview_sampling_limits",
    "preview_mesh_geometry", "case_inspection_contract", "preview_session_contract",
    "preview_verified_resources", "preview_exact_sample_cache", "preview_cellular_2d",
    "ui_expansion_contract",
})
JENS_COVERAGE_ANCHORS = frozenset({"jeans_diagnostics", "refinement_indicator_math"})
RELEASE_CONTRACT_COVERAGE_ANCHORS = frozenset({
    "plotfile_publication", "checkpoint_conservation_metrics",
    "conservative_acceptance", "rkl_repair_weights",
    "boundary_plan", "amr_flux_surface_plan",
})

# Coverage anchors for the CPU+KLU profile, not a frozen total test count.
# All other tests discovered by CTest must also appear in the completed report.
CPU_COVERAGE_ANCHORS = frozenset({
    "physical_constants", "curvilinear_metrics", "amr_operation_plans", "low_density_math",
    "topology_transaction", "burn_mainline_reference",
    "ppm_limiter_math", "conservative_flux_limiter",
    "reduction_contract", "same_level_exchange_plan",
    "checkpoint_compatibility", "tabular_eos_ideal_gas",
    "sparse_klu_161_equations",
    "shared_stage_scheduler", "gravity_stage_contract", "checkpoint_temporal_comparison",
    "poisson_multigrid_contract", "poisson_multigrid_analytic",
    "composite_poisson_contract", "composite_poisson_analytic", "self_gravity_lifecycle", "self_gravity_physics",
}) | CONFIGURATION_COVERAGE_ANCHORS | PREVIEW_COVERAGE_ANCHORS | JENS_COVERAGE_ANCHORS | RELEASE_CONTRACT_COVERAGE_ANCHORS

DRIVER_CUDA_COVERAGE_ANCHORS = frozenset({
    "cuda_compile_probe", "shared_stage_scheduler", "gravity_stage_contract",
    "cuda_regrid_transaction", "cuda_regrid_migration", "cuda_amr_composition",
    "cuda_store_lifecycle", "cuda_amr_exchange", "cuda_hydro_dispatch",
    "diffusion_rkl_parity", "cuda_multiblock_hydro", "cuda_multiblock_diffusion",
    "cuda_multiblock_burn", "cuda_reduction_contract", "checkpoint_temporal_comparison",
    "hydro_leaf_parity", "cuda_curvilinear_geometry_smoke", "cuda_grid_metrics_cache",
    "cuda_refinement_indicators", "cuda_ppm_limiter_math", "cuda_flux_limiter",
    "cuda_conservative_acceptance", "eos_host_device_parity",
})
PROFILES = {"cpu": CPU_COVERAGE_ANCHORS, "driver-cuda": DRIVER_CUDA_COVERAGE_ANCHORS}


def check_inventory(inventory, profile="cpu"):
    """Require profile anchors and later account for every supplied inventory entry.

    CPU CI supplies its complete numerical/API inventory; the separate Tooling
    job owns tests labeled tooling. Local unfiltered inventories are accepted.
    Driver-CUDA covers selected orchestration and shared numerical-leaf contracts,
    not the complete GPU scientific campaign.
    Neither profile permits missing, skipped or unexpected result entries.
    """
    if profile not in PROFILES:
        raise ValueError("unknown coverage profile: " + profile)
    tests = inventory.get("tests")
    if not isinstance(tests, list) or not tests:
        raise ValueError("CTest inventory is empty or malformed")
    names = [test.get("name") for test in tests if isinstance(test, dict)]
    if len(names) != len(tests) or any(
            not isinstance(name, str) or not name for name in names):
        raise ValueError("CTest inventory contains an invalid test name")
    if len(names) != len(set(names)):
        raise ValueError("CTest inventory contains duplicate test names")
    missing = PROFILES[profile] - set(names)
    if missing:
        raise ValueError(profile + " coverage is missing: " + ", ".join(sorted(missing)))
    return set(names)


def check_junit(root, expected):
    """Require a passing CTest JUnit entry for every configured test, with no skips."""
    if root.tag != "testsuite":
        raise ValueError("expected a CTest JUnit testsuite")
    cases = root.findall("testcase")
    names = [case.get("name") for case in cases]
    if not cases or len(names) != len(set(names)) or set(names) != expected:
        raise ValueError("JUnit test names do not match the complete CTest inventory")
    if int(root.get("tests", "-1")) != len(cases):
        raise ValueError("JUnit test count does not match its entries")
    for counter in ("failures", "errors", "skipped", "disabled"):
        if int(root.get(counter, "0")) != 0:
            raise ValueError("JUnit reports nonzero " + counter)
    for case in cases:
        if case.get("status", "run") != "run" or any(
                case.find(marker) is not None for marker in ("failure", "error", "skipped")):
            raise ValueError("CTest did not pass: " + case.get("name", "<unnamed>"))
    return len(cases)


def check_node_tap(text):
    """Require Node's complete TAP summary, without rerunning its Host subset."""
    if not text.startswith("TAP version 13\n"):
        raise ValueError("expected a Node TAP report")
    counters = {}
    for key in ("tests", "suites", "pass", "fail", "cancelled", "skipped", "todo"):
        matches = re.findall(r"^# " + key + r" ([0-9]+)$", text, re.MULTILINE)
        if len(matches) != 1:
            raise ValueError("missing or repeated Node TAP counter: " + key)
        counters[key] = int(matches[0])
    if counters["tests"] == 0 or counters["pass"] != counters["tests"]:
        raise ValueError("Node suite is empty or incomplete")
    if any(counters[key] for key in ("fail", "cancelled", "skipped", "todo")):
        raise ValueError("Node suite contains failed, cancelled, skipped or TODO tests")
    if re.search(r"^\s*(?:not ok\b|Bail out!)", text, re.MULTILINE):
        raise ValueError("Node TAP contains an unsuccessful test or bailout")
    plans = re.findall(r"^1\.\.([0-9]+)$", text, re.MULTILINE)
    if len(plans) != 1 or int(plans[0]) == 0:
        raise ValueError("Node TAP has no completed nonempty plan")
    records = re.findall(r"^\s*ok [0-9]+ - ", text, re.MULTILINE)
    top_records = re.findall(r"^ok ([0-9]+) - ", text, re.MULTILINE)
    if len(records) != counters["tests"] + counters["suites"] or len(top_records) != int(plans[0]):
        raise ValueError("Node TAP summary or plan does not match completed entries")
    if [int(i) for i in top_records] != list(range(1, int(plans[0]) + 1)):
        raise ValueError("Node TAP top-level entries are missing or repeated")
    return counters["tests"]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=PROFILES, default="cpu",
                        help="cpu uses the complete inventory; driver-cuda is a scoped GPU gate")
    reports = parser.add_mutually_exclusive_group(required=True)
    reports.add_argument("--inventory", type=Path,
                        help="ctest --show-only=json-v1 output from the CI build")
    reports.add_argument("--node-tap", type=Path,
                         help="complete Node --test --test-reporter=tap output")
    parser.add_argument("--junit", type=Path,
                        help="ctest --output-junit report; omit to check discovery only")
    args = parser.parse_args(argv)
    try:
        if args.node_tap is not None:
            if args.junit is not None:
                raise ValueError("--junit requires --inventory")
            count = check_node_tap(args.node_tap.read_text(encoding="utf-8"))
            print(f"studio: all {count} Node tests passed without skips or TODO")
            return 0
        inventory = json.loads(args.inventory.read_text(encoding="utf-8"))
        if not isinstance(inventory, dict):
            raise ValueError("CTest inventory must be a JSON object")
        expected = check_inventory(inventory, args.profile)
        if args.junit is not None:
            count = check_junit(ET.parse(args.junit).getroot(), expected)
            print(f"{args.profile}: all {count} supplied inventory tests passed without skips")
        else:
            print(f"{args.profile}: {len(expected)} inventory tests; coverage anchors present")
    except (OSError, ValueError, ET.ParseError) as error:
        profile = "studio" if args.node_tap is not None else args.profile
        print(f"{profile} result check failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
