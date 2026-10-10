"""Failure controls for CI report completeness, not additional solver tests."""

import contextlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))

from check_ci_results import (CPU_COVERAGE_ANCHORS, DRIVER_CUDA_COVERAGE_ANCHORS,
                              check_inventory, check_junit, check_node_tap,
                              check_studio_scope, main, scope_path_requires_studio)


class CiResultTests(unittest.TestCase):
    def test_driver_cuda_requires_real_device_coverage_and_rejects_skips(self):
        # Independent literal prevents a self-consistent group union from
        # silently dropping the actual required-EOS failure CUDA owner.
        eos_owner = "cuda_hydro_eos_failure"
        self.assertIn(eos_owner, DRIVER_CUDA_COVERAGE_ANCHORS)
        names = DRIVER_CUDA_COVERAGE_ANCHORS | {"additional_cuda_contract"}
        self.assertEqual(check_inventory(self.inventory(names), "driver-cuda"), names)
        self.assertEqual(check_junit(self.report(names), names), len(names))
        with self.assertRaisesRegex(ValueError, eos_owner):
            check_inventory(self.inventory(names - {eos_owner}), "driver-cuda")
        with self.assertRaisesRegex(ValueError, "complete CTest inventory"):
            check_junit(self.report(names - {eos_owner}), names)
        # Neither zero summary failure counters nor a present inventory entry
        # can turn a skipped/failed required EOS control into device coverage.
        for marker in ("skipped", "failure", "error"):
            eos_report = self.report(names)
            eos_case = next(case for case in eos_report if case.get("name") == eos_owner)
            ET.SubElement(eos_case, marker)
            with self.subTest(eos_marker=marker), self.assertRaisesRegex(ValueError, eos_owner):
                check_junit(eos_report, names)
        with self.assertRaisesRegex(ValueError, "cuda_compile_probe"):
            check_inventory(self.inventory(CPU_COVERAGE_ANCHORS), "driver-cuda")
        report = self.report(names)
        ET.SubElement(report[0], "skipped")
        with self.assertRaises(ValueError):
            check_junit(report, names)
        with self.assertRaisesRegex(ValueError, "unknown coverage profile"):
            check_inventory(self.inventory(), "typo")

    def node_report(self, tests=2, passed=2, failed=0, cancelled=0, skipped=0, todo=0):
        return (f"TAP version 13\nok 1 - first\nok 2 - second\n1..2\n"
                f"# tests {tests}\n# suites 0\n# pass {passed}\n# fail {failed}\n"
                f"# cancelled {cancelled}\n# skipped {skipped}\n# todo {todo}\n")

    def test_node_report_rejects_empty_skipped_cancelled_todo_and_incomplete(self):
        self.assertEqual(check_node_tap(self.node_report()), 2)
        for report in ("", self.node_report(tests=0, passed=0),
                       self.node_report(passed=1), self.node_report(failed=1),
                       self.node_report(cancelled=1), self.node_report(skipped=1),
                       self.node_report(todo=1), self.node_report().replace("1..2", "1..0"),
                       self.node_report().replace("1..2", "1..1"),
                       self.node_report().replace("ok 1 - first\n", ""),
                       self.node_report().replace("ok 2 - second", "ok 1 - second"),
                       self.node_report() + "# tests 2\n",
                       self.node_report().replace("ok 1 - first", "not ok 1 - first"),
                       self.node_report() + "Bail out! interrupted\n"):
            with self.subTest(report=report), self.assertRaises(ValueError):
                check_node_tap(report)

    def test_node_cli_failure_controls(self):
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory) / "tests.tap"
            report.write_text(self.node_report(), encoding="utf-8")
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(main(["--node-tap", str(report)]), 0)
            report.write_text(self.node_report(skipped=1), encoding="utf-8")
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(main(["--node-tap", str(report)]), 1)
                self.assertEqual(main(["--node-tap", str(report), "--junit", "unused"]), 1)

    def inventory(self, names=None):
        return {"tests": [{"name": name} for name in sorted(
            CPU_COVERAGE_ANCHORS if names is None else names)]}

    def report(self, names=None):
        names = sorted(CPU_COVERAGE_ANCHORS if names is None else names)
        root = ET.Element("testsuite", tests=str(len(names)), failures="0", skipped="0")
        for name in names:
            ET.SubElement(root, "testcase", name=name, status="run")
        return root

    def test_migrated_configuration_preview_and_jens_cannot_disappear(self):
        # Explicit requirement names are independent of the checker group unions.
        required = {
            "config_input_records", "input_resolution", "case_configuration", "configuration_input",
            "configuration_api_contract", "configuration_entry_contract", "configuration_v3_contract",
            "preview_initial_conversion", "preview_api_contract", "preview_full_model_contract",
            "preview_parameter_reads", "preview_parameter_metadata", "preview_sampling_limits",
            "preview_mesh_geometry", "case_inspection_contract", "preview_session_contract",
            "preview_verified_resources", "preview_exact_sample_cache", "preview_cellular_2d",
            "jeans_diagnostics", "refinement_indicator_math",
        }
        self.assertTrue(required <= CPU_COVERAGE_ANCHORS)
        for name in sorted(required):
            with self.subTest(missing=name), self.assertRaisesRegex(ValueError, name):
                check_inventory(self.inventory(CPU_COVERAGE_ANCHORS - {name}))

    def test_low_density_math_cannot_disappear_from_the_cpu_gate(self):
        with self.assertRaisesRegex(ValueError, "low_density_math"):
            check_inventory(self.inventory(CPU_COVERAGE_ANCHORS - {"low_density_math"}))

    def test_release_publication_conservation_and_boundary_contracts_are_required(self):
        required = {
            "plotfile_publication", "checkpoint_conservation_metrics",
            "conservative_acceptance", "rkl_repair_weights",
            "boundary_plan", "amr_flux_surface_plan",
        }
        self.assertTrue(required <= CPU_COVERAGE_ANCHORS)
        for name in sorted(required):
            with self.subTest(missing=name), self.assertRaisesRegex(ValueError, name):
                check_inventory(self.inventory(CPU_COVERAGE_ANCHORS - {name}))

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

    def git_repo(self):
        """Create a throwaway repo so scope tests exercise real git diff output."""
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        root = Path(directory.name)
        env = dict(os.environ, GIT_AUTHOR_NAME="scope", GIT_AUTHOR_EMAIL="scope@example.com",
                   GIT_COMMITTER_NAME="scope", GIT_COMMITTER_EMAIL="scope@example.com")

        def git(*args):
            subprocess.run(["git", *args], cwd=root, check=True, env=env,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        git("init", "-q")
        return root, git

    @staticmethod
    def head_of(root):
        return subprocess.run(["git", "rev-parse", "HEAD"], cwd=root, check=True,
                              stdout=subprocess.PIPE, text=True).stdout.strip()

    def test_studio_scope_only_known_unrelated_paths_may_skip(self):
        # Documentation outside Studio/API text, standalone numerical code,
        # Core-only tests and scientific validation tooling may skip Studio.
        for path in ("README.md", "docs/design.rst", "docs/notes.txt", "notes/guide.md",
                     "README.txt", "LICENSE.txt", "NOTICE.txt",
                     "src/numerics/eos.py", "src/physics/gravity.cpp", "src/cuda/kernel.cu",
                     "tests/host/host_case.cpp", "tests/cuda/device_case.cu", "validation/run.py"):
            with self.subTest(skippable=path):
                self.assertFalse(scope_path_requires_studio(path))
        # Studio UI/API docs, interfaces, core/data/grid/amr/driver/io, include,
        # simulation, cmake/workflow/config/build/tooling and unknown paths stay.
        # Protected build/tool/config/workflow directories win over any doc
        # suffix, and .txt is documentation only under docs/ or with an explicit
        # README/LICENSE/NOTICE basename, so ambiguous notes/todo.txt stays.
        for path in ("studio/ui/main.cpp", "studio/README.md", "src/api/client.cpp",
                     "src/api/README.md", "src/grid/grid.cpp", "src/amr/refine.cpp",
                     "src/driver/step.cpp", "src/io/writer.cpp", "include/arch/header.hpp",
                     "simulation/run.cc", "CMakeLists.txt", "cmake/toolchain.cmake",
                     ".github/workflows/studio.yml", "tools/check_ci_results.py",
                     "tools/requirements.txt", "tools/README.md", "cmake/config.txt",
                     "cmake/notes.txt", "config/settings.txt", "build/notes.txt",
                     ".github/workflows/notes.txt", "requirements.txt", "notes/todo.txt",
                     "src/numerics", "notes/todo.MD", "../escape.md", ""):
            with self.subTest(required=path):
                self.assertTrue(scope_path_requires_studio(path))

    def test_studio_scope_malformed_refs_and_bad_diff_fall_back_to_studio(self):
        good = "a" * 40
        for base in ("HEAD", "abcdef1", "", None, "z" * 40, "a" * 39):
            with self.subTest(base=base):
                result = check_studio_scope(base, "HEAD")
                self.assertTrue(result["required"])
                self.assertEqual(set(result), {"required", "reason", "changed_paths"})
                self.assertIsInstance(result["reason"], str)
                self.assertEqual(result["changed_paths"], [])
        for head in ("main", "abcdef1", "", None, "z" * 40):
            with self.subTest(head=head):
                self.assertTrue(check_studio_scope(good, head)["required"])
        # A valid but non-repository root is a diff error, not a skip decision.
        with tempfile.TemporaryDirectory() as directory:
            self.assertTrue(check_studio_scope(good, "HEAD", repo_root=directory)["required"])

    @unittest.skipUnless(shutil.which("git"), "git is required for real diff semantics")
    def test_studio_scope_real_git_delete_missing_ref_and_empty_fall_back(self):
        root, git = self.git_repo()
        (root / "studio").mkdir()
        (root / "README.md").write_text("docs\n", encoding="utf-8")
        (root / "studio" / "engine.cpp").write_text("int main() {}\n", encoding="utf-8")
        git("add", "-A")
        git("commit", "-q", "-m", "base")
        base = self.head_of(root)

        # Same refs produce an empty change list -> conservative Studio fallback.
        empty = check_studio_scope(base, base, repo_root=root)
        self.assertTrue(empty["required"])
        self.assertEqual(empty["changed_paths"], [])

        # An exact-content rename would hide the old Studio path in normal
        # --name-only output; --no-renames must retain the deletion.
        git("config", "diff.renames", "true")
        (root / "src" / "numerics").mkdir(parents=True)
        git("mv", "studio/engine.cpp", "src/numerics/engine.py")
        git("add", "-A")
        git("commit", "-q", "-m", "move")
        moved = check_studio_scope(base, self.head_of(root), repo_root=root)
        self.assertTrue(moved["required"])
        self.assertIn("studio/engine.cpp", moved["changed_paths"])

        # A well-formed but missing commit ref is a diff failure, not a skip.
        missing = check_studio_scope("0" * 40, self.head_of(root), repo_root=root)
        self.assertTrue(missing["required"])
        self.assertEqual(missing["changed_paths"], [])

    @unittest.skipUnless(shutil.which("git"), "git is required for real diff semantics")
    def test_studio_scope_real_git_docs_only_change_may_skip(self):
        root, git = self.git_repo()
        (root / "README.md").write_text("docs\n", encoding="utf-8")
        (root / "src" / "numerics").mkdir(parents=True)
        (root / "src" / "numerics" / "eos.py").write_text("x = 1\n", encoding="utf-8")
        git("add", "-A")
        git("commit", "-q", "-m", "base")
        base = self.head_of(root)
        (root / "README.md").write_text("more docs\n", encoding="utf-8")
        (root / "src" / "numerics" / "eos.py").write_text("x = 2\n", encoding="utf-8")
        git("add", "-A")
        git("commit", "-q", "-m", "docs and numerics")
        result = check_studio_scope(base, self.head_of(root), repo_root=root)
        self.assertFalse(result["required"])
        self.assertEqual(sorted(result["changed_paths"]),
                         ["README.md", "src/numerics/eos.py"])
        self.assertEqual(set(result), {"required", "reason", "changed_paths"})

    def test_studio_scope_cli_emits_json_and_rejects_mixed_modes(self):
        with contextlib.redirect_stdout(io.StringIO()) as stdout:
            # The capsule is not a Git repository, so this exercises the diff
            # error fallback: exit 0 with a conservative JSON decision, no PASS.
            code = main(["--studio-scope-base", "0" * 40, "--studio-scope-head", "HEAD"])
        self.assertEqual(code, 0)
        payload = json.loads(stdout.getvalue())
        self.assertEqual(set(payload), {"required", "reason", "changed_paths"})
        self.assertIsInstance(payload["required"], bool)
        self.assertIsInstance(payload["reason"], str)
        # Base without head stays conservative: malformed-head rule, exit 0.
        with contextlib.redirect_stdout(io.StringIO()) as stdout:
            self.assertEqual(main(["--studio-scope-base", "0" * 40]), 0)
        self.assertTrue(json.loads(stdout.getvalue())["required"])
        # --studio-scope-head is only valid with --studio-scope-base; alone or
        # paired with --node-tap/--inventory it is rejected, never ignored.
        for argv in (["--studio-scope-head", "HEAD"],
                     ["--node-tap", "unused", "--studio-scope-head", "HEAD"],
                     ["--inventory", "unused", "--studio-scope-head", "HEAD"],
                     ["--node-tap", "unused", "--studio-scope-base", "0" * 40]):
            with self.subTest(argv=argv), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    main(argv)
            self.assertEqual(error.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
