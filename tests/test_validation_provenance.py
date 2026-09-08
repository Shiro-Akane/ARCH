"""Fast provenance/qualification regressions; no CUDA or HDF5 required."""

import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import qualify_cuda_amr_evidence as qualification
import validation_provenance as provenance


class ProvenanceTests(unittest.TestCase):
    def test_focused_identity_tracks_configured_sources_and_artifact_replacement(self):
        def capture():
            return provenance.capture_focused(artifacts={"probe": self.arch},
                source_root=self.root, build_dir=self.build)
        before = capture()
        self.assertEqual(before["build"]["configuration"], "Release")
        self.assertIn("probe", before["artifacts"])
        provenance.require_unchanged(before, capture())
        replacement = self.build / "replacement"
        replacement.write_bytes(self.arch.read_bytes())
        replacement.replace(self.arch)
        with self.assertRaisesRegex(RuntimeError, "artifact_observation"):
            provenance.require_unchanged(before, capture())

    def test_focused_identity_rejects_foreign_empty_or_unstable_artifacts(self):
        arguments = dict(source_root=self.root, build_dir=self.build)
        for artifacts in ({}, {"foreign": self.root / "src/example.h"}):
            with self.assertRaisesRegex(RuntimeError, "build-local"):
                provenance.capture_focused(artifacts=artifacts, **arguments)
        with self.assertRaisesRegex(RuntimeError, "source-root"):
            provenance.capture_focused(artifacts={"probe": self.arch},
                source_root=self.build, build_dir=self.build)
        with mock.patch.object(provenance, "_artifact_observations", side_effect=[{"x": [1]}, {"x": [2]}]), \
             self.assertRaisesRegex(RuntimeError, "changed while recording"):
            provenance.capture_focused(artifacts={"probe": self.arch}, **arguments)

    def test_execution_controls_are_observed_without_dumping_environment(self):
        with mock.patch.dict(os.environ, {"OMP_NUM_THREADS": "2", "OMP_DYNAMIC": "FALSE",
                                         "UNRELATED_PRIVATE_TOKEN": "not-evidence"}, clear=True):
            identity = provenance.execution_environment_identity()
            self.assertEqual(identity["OMP_NUM_THREADS"], "2")
            self.assertIsNone(identity["CUDA_VISIBLE_DEVICES"])
            self.assertNotIn("UNRELATED_PRIVATE_TOKEN", identity)
            before = provenance.capture(arch=self.arch, checkpoint_validator=self.validator,
                source_root=self.root, build_dir=self.build)
            os.environ["OMP_NUM_THREADS"] = "1"
            after = provenance.capture(arch=self.arch, checkpoint_validator=self.validator,
                source_root=self.root, build_dir=self.build)
            self.assertNotEqual(before["execution_environment"], after["execution_environment"])
            with self.assertRaisesRegex(RuntimeError, "execution_environment"):
                provenance.require_unchanged(before, after)

    def test_full_runtime_contract_requires_all_four_canonical_matrices(self):
        canonical = Path("validation/amr/gpu_cases.json")
        matrix, curved, uniform, generated = map(Path,
            ("amr.json", "curved.json", "uniform.json", "generated.json"))
        contract = qualification.runtime_matrix_contract("full-runtime", matrix, canonical, curved, uniform, generated)
        self.assertEqual([manifest.as_posix() for _, manifest in contract], [
            "validation/amr/gpu_cases.json", "validation/amr/gpu_curvilinear_cases.json",
            "validation/backend/cases.json", "validation/network/runtime_cases.json"])
        for args in (("full-runtime", matrix, canonical, None, uniform, generated),
                     ("full-runtime", matrix, canonical, curved, None, generated),
                     ("full-runtime", matrix, canonical, curved, uniform, None),
                     ("full-runtime", matrix, canonical, matrix, uniform, generated),
                     ("full-runtime", matrix, canonical, curved, uniform, matrix),
                     ("full-runtime", matrix, Path("partial.json"), curved, uniform, generated),
                     ("amr", matrix, canonical, curved, uniform, generated)):
            with self.assertRaises(RuntimeError):
                qualification.runtime_matrix_contract(*args)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "src").mkdir()
        (self.root / "src/example.h").write_text("original source\n")
        self.git("init", "-q")
        self.git("-c", "user.email=test@example.invalid", "-c", "user.name=Test",
                 "add", "src/example.h")
        self.git("-c", "user.email=test@example.invalid", "-c", "user.name=Test",
                 "commit", "-qm", "test fixture")
        self.build = self.root / "build"
        self.build.mkdir()
        (self.build / "CMakeCache.txt").write_text(
            "CMAKE_BUILD_TYPE:STRING=Release\nARCH_ENABLE_CUDA:BOOL=ON\n"
            f"ARCH_RUNTIME_OUTPUT_DIRECTORY:PATH={self.build}\n"
            f"CMAKE_HOME_DIRECTORY:INTERNAL={self.root}\n")
        (self.build / "compile_commands.json").write_text("[]\n")
        self.arch = self.build / "ARCH"
        self.validator = self.build / "arch_cuda_single_level_validation"
        self.arch.write_bytes(b"final ARCH")
        self.validator.write_bytes(b"final checkpoint validator")
        self.arguments = dict(arch=self.arch, checkpoint_validator=self.validator,
                              source_root=self.root, build_dir=self.build)

    def git(self, *args):
        return subprocess.run(["git", "-C", str(self.root), *args],
                              check=True, capture_output=True).stdout

    def capture(self):
        return provenance.capture(**self.arguments)

    def test_clean_source_records_full_commit_and_build_configuration(self):
        identity = self.capture()
        self.assertEqual(identity["source"]["commit"],
                         self.git("rev-parse", "HEAD").decode().strip())
        self.assertEqual(len(identity["source"]["commit"]), 40)
        self.assertFalse(identity["source"]["dirty"])
        self.assertEqual(identity["build"]["configuration"], "Release")
        self.assertEqual(identity["build"]["cmake_options"]["ARCH_ENABLE_CUDA"], "ON")

    def test_dirty_and_untracked_compilation_inputs_are_fingerprinted(self):
        original = self.capture()
        (self.root / "src/example.h").write_text("modified source\n")
        (self.root / "simulation/UserCase").mkdir(parents=True)
        (self.root / "simulation/UserCase/UserCase.cpp").write_text("new code\n")
        modified = self.capture()
        self.assertTrue(modified["source"]["dirty"])
        self.assertNotEqual(original["source"]["worktree_sha256"],
                            modified["source"]["worktree_sha256"])
        self.assertEqual(modified["source"]["modified_files"], ["src/example.h"])
        self.assertEqual(modified["source"]["untracked_source_files"],
                         ["simulation/UserCase/UserCase.cpp"])

    def test_output_documentation_and_unrelated_user_files_are_ignored(self):
        original = self.capture()
        (self.root / "docs").mkdir()
        (self.root / "docs/status.md").write_text("report")
        (self.root / "validation/amr/results/run").mkdir(parents=True)
        (self.root / "validation/amr/results/run/evidence.json").write_text("{}")
        (self.root / "personal.json").write_text("{}")
        (self.build / "generated.cpp").write_text("build output")
        self.assertEqual(original, self.capture())

    def test_gitignored_source_is_not_hidden_from_cmake_glob_identity(self):
        original = self.capture()
        (self.root / ".gitignore").write_text("simulation/Ignored/\n")
        (self.root / "simulation/Ignored").mkdir(parents=True)
        (self.root / "simulation/Ignored/problem.cpp").write_text("compiled source")
        after = self.capture()
        self.assertNotEqual(original["source"], after["source"])
        self.assertEqual(after["source"]["untracked_source_files"],
                         ["simulation/Ignored/problem.cpp"])

    def test_timmes_include_fragments_are_in_source_scope(self):
        original = self.capture()
        (self.root / "src/TimmesRhs.inc").write_text("changed nuclear formula")
        self.assertNotEqual(original["source"], self.capture()["source"])
        self.assertIn(".inc", self.capture()["source"]["scope_policy"]["suffixes"])

    def test_deleted_tracked_source_changes_identity(self):
        original = self.capture()
        (self.root / "src/example.h").unlink()
        self.assertNotEqual(original["source"], self.capture()["source"])

    def test_changed_arch_refuses_to_publish_success_report(self):
        original = self.capture()
        self.arch.write_bytes(b"rebuilt while tests were running")
        output = self.build / "evidence.json"
        with self.assertRaisesRegex(RuntimeError, "artifacts changed"):
            provenance.write_evidence(output, {}, original, **self.arguments)
        self.assertFalse(output.exists())

    def test_changed_checkpoint_validator_refuses_success(self):
        original = self.capture()
        self.validator.write_bytes(b"different comparator")
        with self.assertRaisesRegex(RuntimeError, "artifacts changed"):
            provenance.require_unchanged(original, self.capture())

    def test_edit_then_restore_binary_is_detected(self):
        original = self.capture()
        content = self.arch.read_bytes()
        self.arch.write_bytes(b"temporary different executable")
        self.arch.write_bytes(content)
        self.assertEqual(original["artifacts"], self.capture()["artifacts"])
        with self.assertRaisesRegex(RuntimeError, "artifact_observation changed"):
            provenance.require_unchanged(original, self.capture())

    def test_changed_build_configuration_is_detected(self):
        original = self.capture()
        with (self.build / "CMakeCache.txt").open("a") as stream:
            stream.write("CMAKE_CXX_FLAGS:STRING=-ffast-math\n")
        with self.assertRaisesRegex(RuntimeError, "build changed"):
            provenance.require_unchanged(original, self.capture())

    def test_registered_external_math_content_is_in_identity(self):
        network_root = self.root / "external-networks"
        package = network_root / "sample"
        package.mkdir(parents=True)
        (package / "manifest.json").write_text('{"id":"sample"}')
        (package / "Network.h").write_text('#include "generated/actual_rhs.H"\n')
        (package / "generated").mkdir()
        math = package / "generated/actual_rhs.H"
        math.write_text("original math")
        inventory = self.build / "generated"
        inventory.mkdir()
        (inventory / "CustomNetworks.generated.h").write_text(
            '#include "CustomNetworkRegistry.generated.h"\n'
            f'#include "{package / "Network.h"}"\n')
        with (self.build / "CMakeCache.txt").open("a") as stream:
            stream.write(f"ARCH_CUSTOM_NETWORK_ROOT:PATH={network_root}\n")
        original = self.capture()
        math.write_text("changed math without changing path")
        with self.assertRaisesRegex(RuntimeError, "build changed"):
            provenance.require_unchanged(original, self.capture())
        # Unregistered candidates must not become part of a tested math package.
        unchanged = self.capture()
        (network_root / "candidate").mkdir()
        (network_root / "candidate/math.H").write_text("not compiled")
        provenance.require_unchanged(unchanged, self.capture())

    def test_configured_sparse_library_content_is_in_identity(self):
        library = self.root / "libcudss.so"
        library.write_bytes(b"library build one")
        with (self.build / "CMakeCache.txt").open("a") as stream:
            stream.write(f"CuDSS_LIBRARY:FILEPATH={library}\n")
        original = self.capture()
        library.write_bytes(b"library build two")
        with self.assertRaisesRegex(RuntimeError, "build changed"):
            provenance.require_unchanged(original, self.capture())

    def test_final_link_hashes_fetched_klu_dependencies_and_rejects_missing_provider(self):
        names = ['libklu.a', 'libamd.a', 'libcolamd.a', 'libbtf.a', 'libsuitesparseconfig.a', 'libcudss.so.0']
        for name in names:
            (self.build / name).write_bytes(name.encode())
        command = 'c++ -o bin/ARCH ' + ' '.join(names)
        original = provenance.sparse_link_command_identity(self.build, command, {'klu', 'cudss'})
        self.assertEqual(len(original['libraries']), len(names))
        (self.build / 'libamd.a').write_bytes(b'different ordering library')
        self.assertNotEqual(original, provenance.sparse_link_command_identity(self.build, command, {'klu', 'cudss'}))
        with self.assertRaisesRegex(RuntimeError, 'required sparse provider'):
            provenance.sparse_link_command_identity(self.build, command.replace('libcudss.so.0', '-lcudss'), {'klu', 'cudss'})
        with self.assertRaisesRegex(RuntimeError, 'missing runtime validation input'):
            provenance.sparse_link_command_identity(self.build, command + ' /absent/libamd.a', {'klu', 'cudss'})

    def test_wrong_build_source_is_rejected(self):
        (self.build / "CMakeCache.txt").write_text(
            "CMAKE_BUILD_TYPE:STRING=Debug\nCMAKE_HOME_DIRECTORY:INTERNAL=/wrong\n")
        with self.assertRaisesRegex(RuntimeError, "different --source-root"):
            self.capture()

    def test_missing_compile_commands_is_rejected(self):
        (self.build / "compile_commands.json").unlink()
        with self.assertRaisesRegex(RuntimeError, "compile_commands"):
            self.capture()

    def test_multiconfig_build_requires_explicit_configuration(self):
        (self.build / "CMakeCache.txt").write_text(
            "CMAKE_CONFIGURATION_TYPES:STRING=Debug;Release\n"
            f"ARCH_RUNTIME_OUTPUT_DIRECTORY:PATH={self.build}\n"
            f"CMAKE_HOME_DIRECTORY:INTERNAL={self.root}\n")
        with self.assertRaisesRegex(RuntimeError, "configuration"):
            self.capture()
        (self.build / "Debug").mkdir()
        self.arch.rename(self.build / "Debug" / self.arch.name)
        self.validator.rename(self.build / "Debug" / self.validator.name)
        self.arguments.update(arch=self.build / "Debug" / self.arch.name,
                              checkpoint_validator=self.build / "Debug" / self.validator.name)
        self.assertEqual(provenance.capture(**self.arguments, configuration="Debug")
                         ["build"]["configuration"], "Debug")

    def test_unchanged_report_has_reusable_complete_identity(self):
        identity = self.capture()
        output = self.build / "evidence.json"
        provenance.write_evidence(output, {"schema": 1}, identity, **self.arguments)
        evidence = json.loads(output.read_text())
        provenance.require_evidence_identity(evidence, self.capture())
        self.assertTrue(evidence["identity_verified_after_run"])

    def test_legacy_matching_binary_hash_still_does_not_qualify(self):
        identity = self.capture()
        with self.assertRaisesRegex(RuntimeError, "incomplete provenance"):
            provenance.require_evidence_identity(
                {"binary_sha256": identity["artifacts"]["arch_sha256"]}, identity)

    def test_matching_arch_does_not_hide_validator_source_or_build_drift(self):
        identity = self.capture()
        for section in ("artifacts", "source", "build", "execution_environment"):
            report = {"schema": 1,
                      "binary_sha256": identity["artifacts"]["arch_sha256"],
                      "provenance": copy.deepcopy(identity),
                      "identity_verified_after_run": True}
            report["provenance"][section]["changed"] = True
            with self.subTest(section=section):
                with self.assertRaisesRegex(RuntimeError, f"evidence {section} differs"):
                    provenance.require_evidence_identity(report, identity)

    def test_final_artifact_list_rejects_wrong_or_duplicate_arch(self):
        path = self.build / "final-artifacts.sha256"
        value = provenance.sha256(self.arch)
        path.write_text(f"{value}  ARCH\n")
        provenance.require_final_artifact_list(path, self.arch)
        for contents in ("0" * 64 + "  ARCH\n", f"{value}  ARCH\n{value}  ARCH\n"):
            path.write_text(contents)
            with self.assertRaisesRegex(RuntimeError, "exactly one matching"):
                provenance.require_final_artifact_list(path, self.arch)


class QualificationTests(unittest.TestCase):
    def test_physical_time_acceptance_is_not_step_reproducibility(self):
        _, report = self.matrix()
        comparison = copy.deepcopy(report['cases'][0]['checkpoints'][0]['parity'])
        step = comparison['step']
        comparison.update(comparison_mode='physical-time', candidate_step=step + 1,
                          time=0.01, candidate_time=0.01, target_time=0.01,
                          candidate_dt_old=comparison['dt_old'] * 0.9,
                          controller_matches=False, time_roundoff_matches=True,
                          output_indices_match=False)
        comparison['candidate_chk_file_index'] += 1
        def check(record, **kwargs):
            qualification.check_comparison(record, comparison['tolerance'], steps=step,
                comparison_mode='physical-time', target_time=0.01, **kwargs)
        check(comparison)
        for key, value in [('time', 0.010000000000000002), ('candidate_time', 0.009999999999999998),
                           ('target_time', 0.02), ('candidate_step', -1), ('candidate_step', 1.5),
                           ('candidate_dt_old', float('nan')), ('controller_matches', 0),
                           ('comparison_mode', 'step-diagnostic')]:
            changed = dict(comparison, **{key: value})
            with self.subTest(key=key, value=value), self.assertRaises(RuntimeError):
                check(changed)
        with self.assertRaises(RuntimeError):
            qualification.check_comparison(comparison, comparison['tolerance'], steps=step)

    def test_step_diagnostic_retains_step_and_output_identity(self):
        _, report = self.matrix()
        comparison = copy.deepcopy(report['cases'][0]['checkpoints'][0]['parity'])
        step = comparison['step']
        comparison.update(comparison_mode='step-diagnostic', candidate_step=step,
                          candidate_time=comparison['time'] * 1.001,
                          candidate_dt_old=comparison['dt_old'] * 0.9,
                          controller_matches=False, time_roundoff_matches=False,
                          output_indices_match=True)
        def check(record):
            qualification.check_comparison(record, comparison['tolerance'], steps=step,
                                            comparison_mode='step-diagnostic')
        check(comparison)
        for key, value in [('candidate_step', step + 1), ('candidate_time', float('inf')),
                           ('candidate_chk_file_index', comparison['chk_file_index'] + 1)]:
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                check(dict(comparison, **{key: value}))

    @staticmethod
    def output_history_fixture(comparison, extra=0):
        # Unit-only shape adaptation, never persisted as runtime evidence.
        comparison["output_index_offsets"] = {"checkpoint": extra, "plot": extra}
        for field in ("chk_file_index", "plt_file_index"):
            comparison["candidate_" + field] = comparison[field] + extra

    def test_checked_in_historical_reports_reject_final_artifact_mismatch(self):
        directory = ROOT / "validation/amr/results/h100-sm90-20260903"
        final = next(line.split()[0] for line in
                     (directory / "final-artifacts.sha256").read_text().splitlines()
                     if line.split()[1] == "ARCH")
        for name in ("backend-validation-evidence.json", "restart-smooth-evidence.json",
                     "restart-enuc-evidence.json"):
            report = json.loads((directory / name).read_text())
            with self.subTest(name=name):
                with self.assertRaisesRegex(RuntimeError, "differs from the final artifact"):
                    provenance.require_evidence_identity(
                        report, {"artifacts": {"arch_sha256": final}})

    def matrix(self):
        manifest_path = ROOT / "validation/amr/gpu_cases.json"
        manifest = json.loads(manifest_path.read_text())
        # Test-only in-memory fixture with the new self-contained summary
        # fields. It deliberately has no provenance and cannot qualify real
        # artifacts; the checked-in historical JSON is never rewritten.
        result = json.loads((ROOT / "validation/amr/results/h100-sm90-20260903"
                            / "backend-validation-evidence.json").read_text())
        result["manifest_sha256"] = provenance.sha256(manifest_path)
        result["input_sha256"] = {
            case["input"]: provenance.sha256(ROOT / case["input"])
            for case in manifest["cases"]}
        result["runtime_inputs"] = qualification.backend_validation.runtime_case_inputs(manifest["cases"], ROOT)
        for case, record in zip(manifest["cases"], result["cases"]):
            for checkpoint in record["checkpoints"]:
                self.output_history_fixture(checkpoint["parity"])
                checkpoint["parity"]["tolerance"] = case["reduction_policy"]
                for backend in ("cpu", "cuda"):
                    lane = checkpoint[backend]
                    lane["initial_checkpoint_sha256"] = "0" * 64
                    lane["resolved_plan"] = {"requested": backend, "resolved": backend, "fallback_reason": ""}
                    rows = [dict(macro_step=0, physical_time=0.0, backend=backend,
                                 old_blocks=1, new_blocks=1, topology_changed=0, wall_seconds=0.01,
                                 bytes_h2d=0, bytes_d2h=0, kernel_count=0, stream_sync_count=0)]
                    lane['regrid'] = {'file': {'path': 'unit-only-regrid.tsv', 'sha256': 'a' * 64},
                        'records': rows,
                        'summary': qualification.backend_validation.summarize_regrids(rows, backend, lane['steps'])}
                    if backend == "cuda":
                        lane["trace_summary"].update({"max_macro_step": lane["steps"],
                                                      "unfinished_transfers": 0,
                                                      "stale_ghost_publications": 0})
                    schedule = lane.get("diffusion_schedule_summary")
                    if schedule:
                        schedule["valid_timestep_records"] = schedule["records"]
                        schedule["initial_operator_records"] = schedule["records"] if schedule["order"] == 2 else 0
        return manifest_path, result

    def test_complete_matrix_is_accepted_and_partial_matrix_is_rejected(self):
        path, report = self.matrix()
        qualification.check_matrix(report, path, ROOT)
        report["cases"].pop()
        with self.assertRaisesRegex(RuntimeError, "matrix is incomplete"):
            qualification.check_matrix(report, path, ROOT)

    def test_physical_matrix_requires_declared_measure_and_actual_parameter_identity(self):
        original_path, report = self.matrix()
        manifest = json.loads(original_path.read_text())
        manifest["cases"][0]["conservation_policy"]["measure"] = "physical_cell_volume"
        for checkpoint in report["cases"][0]["checkpoints"]:
            for backend in ("cpu", "cuda"):
                checkpoint[backend].update({"parameter_file": "actual-run.par", "parameter_sha256": "a" * 64})
                conservation = checkpoint[backend + "_conservation"]
                for name in ("before", "after"):
                    conservation[name].update({"measure": "physical_cell_volume", "geometry": "cartesian",
                                               "parameter_sha256": "a" * 64})
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "physical-manifest.json"
            path.write_text(json.dumps(manifest))
            report["manifest_sha256"] = provenance.sha256(path)
            qualification.check_matrix(report, path, ROOT)
            for defect in ("measure", "parameter_sha256", "lane_hash"):
                changed = copy.deepcopy(report)
                checkpoint = changed["cases"][0]["checkpoints"][0]
                if defect == "lane_hash":
                    checkpoint["cpu"]["parameter_sha256"] = "b" * 64
                else:
                    checkpoint["cpu_conservation"]["before"].pop(defect)
                with self.subTest(defect=defect), self.assertRaises(RuntimeError):
                    qualification.check_matrix(changed, path, ROOT)

    def test_missing_checkpoint_failed_parity_and_input_drift_are_rejected(self):
        path, original = self.matrix()
        for mutation in ("checkpoint", "parity", "input", "topology"):
            report = copy.deepcopy(original)
            if mutation == "checkpoint":
                report["cases"][0]["checkpoints"].pop()
            elif mutation == "parity":
                report["cases"][0]["checkpoints"][0]["parity"]["passed"] = False
            elif mutation == "topology":
                for case in report["cases"]:
                    case["topology_transitions"] = {}
            else:
                report["input_sha256"] = {}
            with self.subTest(mutation=mutation):
                with self.assertRaises(RuntimeError):
                    qualification.check_matrix(report, path, ROOT)

    def test_matrix_rejects_truncated_requested_gate_records(self):
        path, original = self.matrix()
        for record, key in (("cpu", "checkpoint_sha256"), ("cpu", "initial_checkpoint_sha256"),
                            ("cuda", "resolved_plan"), ("cuda", "trace_summary"),
                            ("parity", "topology"), ("parity", "max_abs"),
                            ("parity", "tolerance"), ("cpu_conservation", "before"),
                            ("cpu_conservation", "errors")):
            report = copy.deepcopy(original)
            report["cases"][0]["checkpoints"][0][record].pop(key)
            with self.subTest(record=record, key=key):
                with self.assertRaises(RuntimeError):
                    qualification.check_matrix(report, path, ROOT)
        for key in ("cpu_qualification", "cuda_qualification", "cpu_conservation", "cuda_conservation"):
            report = copy.deepcopy(original)
            report["cases"][0]["checkpoints"][0].pop(key)
            with self.subTest(key=key):
                with self.assertRaises(RuntimeError):
                    qualification.check_matrix(report, path, ROOT)

    def test_matrix_rejects_invalid_trace_plan_conservation_and_tolerances(self):
        path, original = self.matrix()
        for defect in ("trace", "plan", "conservation", "tolerance", "nonfinite"):
            report = copy.deepcopy(original)
            checkpoint = report["cases"][0]["checkpoints"][0]
            if defect == "trace":
                checkpoint["cuda"]["trace_summary"]["unfinished_transfers"] = 1
            elif defect == "plan":
                checkpoint["cuda"]["resolved_plan"]["resolved"] = "cpu"
            elif defect == "conservation":
                checkpoint["cpu_conservation"]["after"]["mass"] *= 2
            elif defect == "tolerance":
                checkpoint["parity"]["tolerance"]["rtol"] = 1.0
            else:
                checkpoint["parity"]["max_abs"] = float("nan")
            with self.subTest(defect=defect):
                with self.assertRaises(RuntimeError):
                    qualification.check_matrix(report, path, ROOT)

    def test_matrix_rejects_missing_or_invalid_rkl_summary(self):
        path, original = self.matrix()
        for key in ("cache_generations", "macro_steps", "negative_gamma_records",
                    "valid_timestep_records", "initial_operator_records"):
            report = copy.deepcopy(original)
            checkpoint = next(case for case in report["cases"] if "rkl2" in case["id"])["checkpoints"][0]
            checkpoint["cuda"]["diffusion_schedule_summary"].pop(key)
            with self.subTest(key=key):
                with self.assertRaises(RuntimeError):
                    qualification.check_matrix(report, path, ROOT)
        report = copy.deepcopy(original)
        checkpoint = next(case for case in report["cases"] if "rkl2" in case["id"])["checkpoints"][0]
        checkpoint["cuda"]["diffusion_schedule_summary"]["negative_gamma_records"] = 0
        with self.assertRaisesRegex(RuntimeError, "diffusion summary"):
            qualification.check_matrix(report, path, ROOT)

    def restart(self):
        path = ROOT / "validation/amr/results/h100-sm90-20260903/restart-smooth-evidence.json"
        report = json.loads(path.read_text())
        report["runtime_inputs"] = provenance.runtime_inputs(
            parameter_file=ROOT / "validation/amr/inputs/smooth_amr80_l1.par",
            working_directory=ROOT, parameter_reader=qualification.backend_validation.read_parameter_map)
        for lane in [*report["continuous"].values(), *report["sources"].values(), *report["resumed"]]:
            backend = lane["backend"]
            lane["resolved_plan"] = {"requested": backend, "resolved": backend, "fallback_reason": ""}
        # Synthetic unit-only metadata for the expanded shape. Never write these
        # additions into historical evidence or use them to qualify real files.
        def metadata(lane, step, phase):
            return {"step": step, "time": step * .1, "resume_after_regrid": phase,
                    "chk_file_index": step, "plt_file_index": step,
                    "checkpoint_sha256": lane["checkpoint_sha256"], "parameter_sha256": "a" * 64}
        for lane in report["continuous"].values():
            lane["run_completed_steps"] = 4
            lane["checkpoint_metadata"] = metadata(lane, 4, False)
        for lane in report["sources"].values():
            lane["steps"] = 2; lane["run_completed_steps"] = 3
            lane["checkpoint_metadata"] = metadata(lane, 2, True)
            lane["terminal_checkpoint"] = lane["checkpoint"] + ".unit-terminal"
            lane["terminal_metadata"] = metadata(lane, 3, False)
        for lane in report["resumed"]:
            source = report["sources"][lane["name"].split("_to_")[0]]
            lane["run_completed_steps"] = 4
            lane["checkpoint_metadata"] = metadata(lane, 4, False)
            lane["restore_confirmed"] = True
            lane["restored_from"] = {"checkpoint": str(Path(source["checkpoint"]).resolve()),
                "parameters": source["parameter"], **source["checkpoint_metadata"]}
        for lane in list(report["resumed"]):
            terminal = copy.deepcopy(lane)
            terminal["name"] += "_terminal"
            source = report["sources"][lane["name"].split("_to_")[0]]
            terminal["restored_from"] = {"checkpoint": str(Path(source["terminal_checkpoint"]).resolve()),
                "parameters": source["parameter"], **source["terminal_metadata"]}
            report["resumed"].append(terminal)
            comparison = copy.deepcopy(next(item for item in report["comparisons"]
                                            if item["route"] == lane["name"]))
            comparison["route"] = terminal["name"]
            report["comparisons"].append(comparison)
        for comparison in report["comparisons"]:
            # Unit-only adaptation to the current native-composition field
            # inventory; never overwrite the retained historical evidence.
            comparison["tolerance"] = qualification.restart_validation.comparison_policy()
            self.output_history_fixture(comparison, int(comparison["route"].endswith("_terminal")))
        return report

    def test_restart_rejects_process_checkpoint_confusion_and_wrong_restore(self):
        for defect in ("step", "phase", "process", "restore"):
            report = self.restart()
            if defect == "step": report["sources"]["cpu"]["steps"] = 3
            elif defect == "phase": report["sources"]["cpu"]["checkpoint_metadata"]["resume_after_regrid"] = False
            elif defect == "process": report["sources"]["cpu"]["run_completed_steps"] = 2
            else: report["resumed"][0]["restore_confirmed"] = False
            with self.subTest(defect=defect), self.assertRaises(RuntimeError):
                qualification.check_restart(report, problem="SmoothAdvection",
                    input_path=ROOT / "validation/amr/inputs/smooth_amr80_l1.par", source_root=ROOT)

    def test_restart_does_not_hide_output_counter_drift(self):
        for defect in ("candidate_counter", "claimed_offset", "source_history"):
            report = self.restart()
            if defect == "candidate_counter":
                report["comparisons"][0]["candidate_chk_file_index"] += 1
            elif defect == "claimed_offset":
                report["comparisons"][-1]["output_index_offsets"]["checkpoint"] = 2
            else:
                report["sources"]["cpu"]["terminal_metadata"]["chk_file_index"] += 1
            with self.subTest(defect=defect), self.assertRaises(RuntimeError):
                qualification.check_restart(report, problem="SmoothAdvection",
                    input_path=ROOT / "validation/amr/inputs/smooth_amr80_l1.par", source_root=ROOT)

    def test_restart_requires_all_routes_and_both_continuous_lanes(self):
        original = self.restart()
        input_path = ROOT / "validation/amr/inputs/smooth_amr80_l1.par"
        # This tests coverage only: identity qualification intentionally rejects
        # this historical report in the separate regression above.
        qualification.check_restart(original, problem="SmoothAdvection", input_path=input_path, source_root=ROOT)
        for key in ("comparisons", "resumed", "continuous", "sources"):
            report = copy.deepcopy(original)
            if isinstance(report[key], list):
                report[key].pop()
            else:
                report[key].pop("cuda")
            with self.subTest(key=key):
                with self.assertRaisesRegex(RuntimeError, "coverage"):
                    qualification.check_restart(report, problem="SmoothAdvection", input_path=input_path, source_root=ROOT)

    def test_restart_requires_mixed_topology_tolerance_and_checkpoint_identity(self):
        original = self.restart()
        input_path = ROOT / "validation/amr/inputs/smooth_amr80_l1.par"
        for defect in ("topology", "tolerance", "checkpoint", "mixed"):
            report = copy.deepcopy(original)
            if defect == "topology":
                report["comparisons"][0].pop("topology")
            elif defect == "tolerance":
                report["comparisons"][0]["tolerance"]["enuc_scale_rtol"] = 1.0
            elif defect == "mixed":
                item = report["comparisons"][0]
                item["topology"] = [[0, index, 0, 0] for index in range(item["blocks"])]
                item["min_level"] = item["max_level"] = 0
            else:
                report["resumed"][0].pop("checkpoint_sha256")
            with self.subTest(defect=defect):
                with self.assertRaises(RuntimeError):
                    qualification.check_restart(report, problem="SmoothAdvection", input_path=input_path, source_root=ROOT)

    def test_mutated_external_eos_table_invalidates_restart_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            table = source / "external-table.dat"
            parameter = source / "restart.par"
            table.write_text("original table")
            parameter.write_text(f"eos_type = helmholtz\neos_table_path = {table}\n")
            report = self.restart()
            report["input_sha256"] = provenance.sha256(parameter)
            report["runtime_inputs"] = provenance.runtime_inputs(
                parameter_file=parameter, working_directory=source,
                parameter_reader=qualification.backend_validation.read_parameter_map)
            qualification.check_restart(report, problem="SmoothAdvection", input_path=parameter, source_root=source)
            table.write_text("changed table without any change to the .par")
            with self.assertRaisesRegex(RuntimeError, "runtime dependency identity"):
                qualification.check_restart(report, problem="SmoothAdvection", input_path=parameter, source_root=source)

    def test_requested_scientific_qualification_cannot_be_omitted_or_relaxed(self):
        case = {"qualification": {"reference": "cpu_and_network_conservation",
                                  "species_sum_atol": 1e-12}}
        metrics = {"status": "pass", "min_rho": 1.0, "min_eng": 1.0,
                   "species_sum_error": 0.0}
        qualification.check_qualification(metrics, case)
        for changed in (None, {"status": "not-requested"},
                        {**metrics, "species_sum_error": 1e-3}):
            with self.assertRaises(RuntimeError):
                qualification.check_qualification(changed, case)


if __name__ == "__main__":
    unittest.main()
