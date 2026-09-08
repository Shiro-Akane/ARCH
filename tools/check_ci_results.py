#!/usr/bin/env python3
"""Check CPU CI coverage and CTest completion, without redefining test physics.

CTest owns test discovery, execution and numerical pass/fail decisions. This
reader rejects incomplete or skipped runs that would otherwise look successful.
It does not create or replace scientific Validation evidence.
"""

import argparse
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET


# Coverage anchors for the CPU+KLU profile, not a frozen total test count.
# All other tests discovered by CTest must also appear in the completed report.
CPU_COVERAGE_ANCHORS = frozenset({
    "physical_constants", "curvilinear_metrics", "amr_operation_plans",
    "topology_transaction", "burn_mainline_reference",
    "checkpoint_compatibility", "tabular_eos_ideal_gas",
    "sparse_klu_161_equations",
})


def check_inventory(inventory):
    """Return unique configured test names and require the CPU coverage anchors."""
    tests = inventory.get("tests")
    if not isinstance(tests, list) or not tests:
        raise ValueError("CTest inventory is empty or malformed")
    names = [test.get("name") for test in tests if isinstance(test, dict)]
    if len(names) != len(tests) or any(
            not isinstance(name, str) or not name for name in names):
        raise ValueError("CTest inventory contains an invalid test name")
    if len(names) != len(set(names)):
        raise ValueError("CTest inventory contains duplicate test names")
    missing = CPU_COVERAGE_ANCHORS - set(names)
    if missing:
        raise ValueError("CPU coverage is missing: " + ", ".join(sorted(missing)))
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


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", type=Path, required=True,
                        help="ctest --show-only=json-v1 output from the CI build")
    parser.add_argument("--junit", type=Path,
                        help="ctest --output-junit report; omit to check discovery only")
    args = parser.parse_args(argv)
    try:
        inventory = json.loads(args.inventory.read_text(encoding="utf-8"))
        if not isinstance(inventory, dict):
            raise ValueError("CTest inventory must be a JSON object")
        expected = check_inventory(inventory)
        if args.junit is not None:
            count = check_junit(ET.parse(args.junit).getroot(), expected)
            print(f"CPU CI: all {count} configured tests passed without skips")
        else:
            print(f"CPU CI: {len(expected)} configured tests; coverage anchors present")
    except (OSError, ValueError, ET.ParseError) as error:
        print(f"CPU CI check failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
