"""Runtime-table and selected-build identity; no CUDA, EOS library or large tables."""

from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import validation_provenance as provenance
from validate_backend_results import read_parameter_map
import validate_cuda_amr_restart as restart_validation


class RestartTerminalTimeTests(unittest.TestCase):
    def test_invalid_time_or_unbounded_steps_are_rejected_before_execution(self):
        for steps, target in ((-1, None), (0, .2), (4, 0.), (4, -1.),
                              (4, float('nan')), (4, float('inf'))):
            with self.subTest(steps=steps, target=target), self.assertRaises(ValueError):
                restart_validation.run_lane(arch=Path('unused'), source_root=ROOT,
                    canonical_input=Path('unused'), output_root=Path('unused'),
                    name='unused', problem='BurnGradient', backend='cpu',
                    max_steps=steps, checkpoint_validator=Path('unused'), terminal_time=target)

    def test_prescribed_time_uses_the_same_restart_parameter_renderer(self):
        class RenderObserved(Exception):
            pass
        for steps, target, expected in ((4, None, '1e99'), (-1, .2, '0.2')):
            with self.subTest(steps=steps, target=target), tempfile.TemporaryDirectory() as directory:
                with mock.patch.object(restart_validation.backend_validation,
                        '_render_parameter_overrides', side_effect=RenderObserved) as render:
                    with self.assertRaises(RenderObserved):
                        restart_validation.run_lane(arch=Path('unused'), source_root=ROOT,
                            canonical_input=Path('unused'), output_root=Path(directory),
                            name='lane', problem='BurnGradient', backend='cpu',
                            max_steps=steps, checkpoint_validator=Path('unused'), terminal_time=target)
                values = render.call_args.args[2]
                self.assertEqual(values['tmax'], expected)
                self.assertEqual(values['max_steps'], str(steps))
                self.assertEqual(values['chk_dstep'], '2')

    def test_terminal_time_cannot_precede_the_restored_state(self):
        for target in (.1, .2):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as directory:
                with mock.patch.object(restart_validation.backend_validation,
                        'checkpoint_metadata', return_value={'resume_after_regrid': True, 'time': .2}):
                    with self.assertRaisesRegex(ValueError, 'follow its source'):
                        restart_validation.run_lane(arch=Path('unused'), source_root=ROOT,
                            canonical_input=Path('unused'), output_root=Path(directory),
                            name='lane', problem='BurnGradient', backend='cpu',
                            max_steps=-1, checkpoint_validator=Path('unused'), terminal_time=target,
                            restart_file=Path('checkpoint'), restart_parameters=Path('parameters'),
                            restart_step=2, restart_phase=True)


class RuntimeInputTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.cwd = self.root / "source"
        self.cwd.mkdir()
        (self.cwd / "cases").mkdir()
        self.parameter = self.cwd / "cases/canonical.par"
        self.parameter.write_text("eos_type = ideal\n")

    def capture(self, **kwargs):
        return provenance.runtime_inputs(
            parameter_file=self.parameter, working_directory=self.cwd,
            parameter_reader=read_parameter_map, **kwargs)

    def test_default_and_explicit_ideal_do_not_consume_unused_tables(self):
        for text in ("", "eos_type = IDEAL\neos_table_path = absent.tbl\n"):
            self.parameter.write_text(text)
            self.assertEqual(self.capture()["dependencies"], [])

    def test_relative_path_is_resolved_from_arch_cwd_not_parameter_directory(self):
        self.parameter.write_text("eos_type = tabular\neos_table_path = table.h5\n")
        (self.cwd / "table.h5").write_bytes(b"actually loaded table")
        (self.parameter.parent / "table.h5").write_bytes(b"wrong .par-relative table")
        result = self.capture()
        self.assertEqual(result["dependencies"], [{
            "parameter": "eos_table_path", "path": str(self.cwd / "table.h5"),
            "sha256": provenance.sha256(self.cwd / "table.h5"),
        }])

    def test_quoted_external_path_preserves_spaces_and_case(self):
        table = self.root / "External Table.DAT"
        table.write_bytes(b"external Helmholtz content")
        self.parameter.write_text(
            f"eos_type = HeLmHoLtZ\neos_table_path = '{table}' # normal comment\n")
        result = self.capture()
        self.assertEqual(result["eos_type"], "helmholtz")
        self.assertEqual(result["dependencies"][0]["path"], str(table))

    def test_effective_scientific_override_selects_only_the_loaded_table(self):
        self.parameter.write_text("eos_type = ideal\neos_table_path = unused-missing.tbl\n")
        table = self.root / "override.tbl"
        table.write_bytes(b"selected external table")
        overrides = {"eos_type": "helmholtz", "eos_table_path": f'"{table}"', "gamma": 1.4}
        original = self.capture(scientific_overrides=overrides)
        self.assertEqual(original["dependencies"][0]["path"], str(table))
        self.assertEqual(original["scientific_overrides"]["gamma"], "1.4")
        table.write_bytes(b"modified outside the source tree")
        after = self.capture(scientific_overrides=overrides)
        self.assertEqual(original["parameter_sha256"], after["parameter_sha256"])
        self.assertNotEqual(original["dependencies"], after["dependencies"])

    def test_ideal_override_does_not_hash_an_inactive_missing_table(self):
        self.parameter.write_text("eos_type = helmholtz\neos_table_path = missing.tbl\n")
        self.assertEqual(self.capture(scientific_overrides={"eos_type": "ideal"})
                         ["dependencies"], [])

    def test_missing_active_path_or_table_fails_closed(self):
        for text in ("eos_type = helmholtz\n",
                     "eos_type = tabular\neos_table_path = missing.h5\n",
                     "eos_type = helmholtz\neos_table_path = cases\n"):
            self.parameter.write_text(text)
            with self.subTest(text=text), self.assertRaises(RuntimeError):
                self.capture()

    def test_unused_eos_directory_contents_do_not_change_identity(self):
        self.parameter.write_text("eos_type = helmholtz\neos_table_path = selected.tbl\n")
        (self.cwd / "selected.tbl").write_bytes(b"selected")
        before = self.capture()
        (self.cwd / "unused-big-table.tbl").write_bytes(b"not loaded")
        self.assertEqual(before, self.capture())

    def test_shared_parameter_parser_is_injected_not_reimplemented(self):
        reader = mock.Mock(wraps=read_parameter_map)
        provenance.runtime_inputs(parameter_file=self.parameter, working_directory=self.cwd,
                                  parameter_reader=reader)
        reader.assert_called_once_with(self.parameter.resolve())

    def test_parameter_change_during_dependency_capture_is_rejected(self):
        def changing_reader(path):
            result = read_parameter_map(path)
            path.write_text("eos_type = ideal\ngamma = 1.6\n")
            return result
        with self.assertRaisesRegex(RuntimeError, "canonical parameter input changed"):
            provenance.runtime_inputs(parameter_file=self.parameter, working_directory=self.cwd,
                                      parameter_reader=changing_reader)

    def test_missing_canonical_parameter_fails(self):
        self.parameter.unlink()
        with self.assertRaisesRegex(RuntimeError, "missing runtime validation input"):
            self.capture()

    def test_hidden_override_assignments_or_comments_are_rejected(self):
        for value in ("table.tbl\neos_type=ideal", "table.tbl # commented", "a\rb"):
            with self.subTest(value=value), self.assertRaisesRegex(RuntimeError, "single parameter"):
                self.capture(scientific_overrides={"eos_table_path": value})


class ArtifactBindingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / "build"
        self.build.mkdir()
        self.output = self.build / "bin"
        self.output.mkdir()
        self.identity = {
            "source_directory": str(self.root), "configuration": "Release",
            "multi_config": False,
            "cmake_options": {"ARCH_RUNTIME_OUTPUT_DIRECTORY": str(self.output)},
        }

    def paths(self):
        return provenance._configured_artifact_paths(self.build, self.identity)

    def test_single_config_build_output_and_comparator_paths(self):
        self.assertEqual(self.paths(), {
            "arch": self.output / "ARCH",
            "checkpoint_validator": self.build / "arch_cuda_single_level_validation",
        })

    def test_multiconfig_artifacts_include_selected_configuration(self):
        self.identity["multi_config"] = True
        self.assertEqual(self.paths(), {
            "arch": self.output / "Release/ARCH",
            "checkpoint_validator": self.build / "Release/arch_cuda_single_level_validation",
        })

    def test_shared_source_bin_is_not_a_qualified_build_local_artifact(self):
        self.identity["cmake_options"]["ARCH_RUNTIME_OUTPUT_DIRECTORY"] = str(self.root / "bin")
        with self.assertRaisesRegex(RuntimeError, "beneath --build-dir"):
            self.paths()

    def test_missing_or_generator_expression_output_is_not_guessed(self):
        for value in ("", "$<CONFIG>/bin"):
            self.identity["cmake_options"]["ARCH_RUNTIME_OUTPUT_DIRECTORY"] = value
            with self.subTest(value=value), self.assertRaisesRegex(RuntimeError, "explicit build-local"):
                self.paths()

    def test_symlinked_executable_cannot_escape_build_tree(self):
        outside = self.root / "other-ARCH"
        outside.write_bytes(b"not this build")
        (self.output / "ARCH").symlink_to(outside)
        with self.assertRaisesRegex(RuntimeError, "inside --build-dir"):
            self.paths()

    def test_capture_rejects_copied_arch_or_comparator_before_hashing(self):
        good = self.paths()
        for role in ("arch", "checkpoint_validator"):
            paths = dict(good)
            paths[role] = self.root / ("copied-" + role)
            with self.subTest(role=role), \
                    mock.patch.object(provenance, "build_identity", return_value=self.identity), \
                    self.assertRaisesRegex(RuntimeError, f"{role} must be the configured artifact"):
                provenance.capture(**paths, source_root=self.root, build_dir=self.build)


if __name__ == "__main__":
    unittest.main()
