"""Failure controls for CI report completeness, not additional solver tests."""

import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from check_ci_results import CPU_COVERAGE_ANCHORS, check_inventory, check_junit, main


class CiResultTests(unittest.TestCase):
    def inventory(self, names=None):
        return {"tests": [{"name": name} for name in sorted(
            CPU_COVERAGE_ANCHORS if names is None else names)]}

    def report(self, names=None):
        names = sorted(CPU_COVERAGE_ANCHORS if names is None else names)
        root = ET.Element("testsuite", tests=str(len(names)), failures="0", skipped="0")
        for name in names:
            ET.SubElement(root, "testcase", name=name, status="run")
        return root

    def test_complete_report_accepts_new_tests_without_a_fixed_total(self):
        names = check_inventory(self.inventory(CPU_COVERAGE_ANCHORS | {"new_regression"}))
        self.assertEqual(check_junit(self.report(names), names), len(names))

    def test_empty_malformed_and_duplicate_inventories_fail(self):
        for data in ({}, {"tests": []}, {"tests": [None]},
                     {"tests": [{"name": ""}]},
                     {"tests": [{"name": "a"}, {"name": "a"}]}):
            with self.subTest(data=data), self.assertRaises(ValueError):
                check_inventory(data)

    def test_disabled_klu_cannot_produce_a_complete_cpu_profile(self):
        with self.assertRaisesRegex(ValueError, "sparse_klu_161_equations"):
            check_inventory(self.inventory(CPU_COVERAGE_ANCHORS - {"sparse_klu_161_equations"}))

    def test_missing_extra_and_duplicate_results_fail(self):
        for names in (set(), CPU_COVERAGE_ANCHORS - {"physical_constants"},
                      CPU_COVERAGE_ANCHORS | {"unexpected"}):
            with self.subTest(names=names), self.assertRaises(ValueError):
                check_junit(self.report(names), CPU_COVERAGE_ANCHORS)
        root = self.report()
        root.append(ET.fromstring(ET.tostring(root[0])))
        with self.assertRaises(ValueError):
            check_junit(root, CPU_COVERAGE_ANCHORS)

    def test_failed_or_skipped_entries_fail_even_with_zero_summary_counters(self):
        for marker in ("failure", "error", "skipped"):
            root = self.report()
            ET.SubElement(root[0], marker)
            with self.subTest(marker=marker), self.assertRaises(ValueError):
                check_junit(root, CPU_COVERAGE_ANCHORS)
        for status in ("notrun", "disabled", "fail"):
            root = self.report()
            root[0].set("status", status)
            with self.subTest(status=status), self.assertRaises(ValueError):
                check_junit(root, CPU_COVERAGE_ANCHORS)

    def test_inconsistent_or_nonzero_summary_counters_fail(self):
        for counter, value in (("tests", "0"), ("failures", "1"), ("errors", "1"),
                               ("skipped", "1"), ("disabled", "1"), ("tests", "invalid")):
            root = self.report()
            root.set(counter, value)
            with self.subTest(counter=counter), self.assertRaises(ValueError):
                check_junit(root, CPU_COVERAGE_ANCHORS)

    def test_invalid_xml_root_fails(self):
        with self.assertRaises(ValueError):
            check_junit(ET.Element("not-a-report"), CPU_COVERAGE_ANCHORS)

    def test_cli_propagates_missing_or_malformed_reports(self):
        with tempfile.TemporaryDirectory() as directory:
            inventory = Path(directory) / "inventory.json"
            report = Path(directory) / "junit.xml"
            inventory.write_text(json.dumps(self.inventory()), encoding="utf-8")
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(main(["--inventory", str(inventory)]), 0)
                ET.ElementTree(self.report()).write(report, encoding="unicode")
                self.assertEqual(main(["--inventory", str(inventory), "--junit", str(report)]), 0)
            with contextlib.redirect_stderr(io.StringIO()):
                report.write_text("<broken", encoding="utf-8")
                self.assertEqual(main(["--inventory", str(inventory), "--junit", str(report)]), 1)
                inventory.write_text("[]", encoding="utf-8")
                self.assertEqual(main(["--inventory", str(inventory)]), 1)
                self.assertEqual(main(["--inventory", str(inventory.with_name("missing"))]), 1)


if __name__ == "__main__":
    unittest.main()
