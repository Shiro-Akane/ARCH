"""Runtime-table and selected-build identity; no CUDA, EOS library or large tables."""

from pathlib import Path
import ast
import os
import shlex
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
import validation_provenance as provenance
import validation_fixture_build as fixture_build
from validate_backend_results import read_parameter_map
import validate_cuda_amr_restart as restart_validation


class RestartTerminalTimeTests(unittest.TestCase):
    def test_adaptive_cross_backend_comparison_uses_existing_physical_time_mode(self):
        reference={'checkpoint':'reference.h5'}
        candidate={'checkpoint':'candidate.h5','name':'cuda_continuous'}
        result=dict(passed=True,min_level=0,max_level=1,
                    output_index_offsets={'checkpoint':0,'plot':0})
        for target in (None,0.2):
            with self.subTest(target=target), mock.patch.object(
                    restart_validation.backend_validation,'compare_hdf5_checkpoints',
                    return_value=dict(result)) as compare:
                restart_validation.compare(Path('validator'),reference,candidate,
                                           target_time=target)
                self.assertEqual(compare.call_args.kwargs['comparison_mode'],
                                 'reproducibility' if target is None else 'physical-time')
                self.assertEqual(compare.call_args.kwargs['target_time'],target)
                self.assertEqual(compare.call_args.args[2],restart_validation.comparison_policy())

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

    def test_explicit_ideal_does_not_consume_unused_tables(self):
        for text in ("eos_type=ideal\n", "eos_type = IDEAL\neos_table_path = absent.tbl\n"):
            self.parameter.write_text(text)
            self.assertEqual(self.capture()["dependencies"], [])

    def test_missing_empty_or_unknown_eos_cannot_create_validation_identity(self):
        for text in ("", "eos_type=\n", "eos_type=typo\n"):
            self.parameter.write_text(text)
            with self.subTest(text=text), self.assertRaisesRegex(
                    RuntimeError, "explicit valid eos_type"):
                self.capture()

    def test_explicit_override_can_supply_required_eos(self):
        self.parameter.write_text("")
        identity = self.capture(scientific_overrides={"eos_type": "ideal"})
        self.assertEqual(identity["eos_type"], "ideal")
        self.assertEqual(identity["scientific_overrides"], {"eos_type": "ideal"})
        self.assertEqual(identity["dependencies"], [])

    def test_ambiguous_or_malformed_input_cannot_create_identity(self):
        for text, code in (
                ("eos_type=ideal\neos_type=ideal\n", "DUPLICATE_PARAMETER"),
                ("eos_type=ideal\ngamma=1.4\ngamma=1.6\n", "DUPLICATE_PARAMETER"),
                ("eos_type=ideal\nbroken line\n", "MALFORMED_LINE"),
                ("eos_type=ideal\n=unused\n", "EMPTY_KEY")):
            self.parameter.write_text(text)
            with self.subTest(text=text), self.assertRaisesRegex(RuntimeError, code):
                self.capture(scientific_overrides={"eos_type": "ideal"})

    def test_raw_parser_retains_empty_and_expression_values(self):
        self.parameter.write_text(
            "# comment\neos_type = ideal # chosen\noptional =\n"
            "x1_max=2*pi\npath=file=name\n")
        self.assertEqual(read_parameter_map(self.parameter), {
            "eos_type": "ideal", "optional": "", "x1_max": "2*pi", "path": "file=name"})

    def test_renderer_rejects_duplicate_source_before_writing(self):
        self.parameter.write_text("eos_type=ideal\ncompute_backend=cpu\ncompute_backend=cuda\n")
        output = self.root / "not-created" / "rendered.par"
        with self.assertRaisesRegex(RuntimeError, "DUPLICATE_PARAMETER"):
            restart_validation.backend_validation._render_parameter_overrides(
                self.parameter, output, {"compute_backend": "cpu"})
        self.assertFalse(output.parent.exists())

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


class PrivateFixtureRecipeTests(unittest.TestCase):
    """Thin-link ownership/identity contracts; never run a compiler or model."""
    originals = [
        "CMakeFiles/arch_gravity_stage_contract.dir/tests/host/gravity/test_gravity_stage_contract.cpp.o",
        "CMakeFiles/arch_gravity_stage_contract.dir/tests/host/driver/test_host_hydro_transaction.cpp.o",
        "CMakeFiles/arch_gravity_stage_contract.dir/tests/host/driver/test_rz_runtime_boundary.cpp.o",
        "CMakeFiles/arch_gravity_stage_contract.dir/tests/host/driver/test_rz_runtime_external.cpp.o",
    ]
    production_objects = [
        "CMakeFiles/arch_gravity_stage_contract.dir/src/amr/elliptic/EllipticMeshAdapter.cpp.o",
        "CMakeFiles/arch_gravity_stage_contract.dir/src/driver/stages/GravityStage.cpp.o",
    ]

    def link_line(self, objects=None):
        objects = self.originals if objects is None else objects
        return ": && /usr/bin/c++ -O3 -flto=auto -fno-fast-math " + " ".join(objects + self.production_objects) + \
            " -o arch_gravity_stage_contract libarch_driver_runtime.a libarch_diffusion_math.a " \
            "libarch_gravity_cpu.a -lm -fopenmp && :"

    def test_all_original_test_objects_become_one_and_production_flags_libraries_keep_order(self):
        tokens = fixture_build.fixture_link_recipe(self.link_line(), self.originals,
            Path("/private/0.o"), Path("/private/rz-runtime-boundary"))
        expected = fixture_build.command_tokens(self.link_line())
        expected[expected.index(self.originals[0])] = "/private/0.o"
        for name in self.originals[1:]:
            expected.remove(name)
        expected[expected.index("-o") + 1] = "/private/rz-runtime-boundary"
        self.assertEqual(tokens, expected)
        self.assertEqual([t for t in tokens if t.endswith(".o")],
                         ["/private/0.o", *self.production_objects])
        self.assertIn("-flto=auto", tokens)

    def test_upstream_archive_shell_scaffold_is_not_executed_or_selected(self):
        commands = "/usr/bin/cmake -E rm -f library.a && /usr/bin/gcc-ar qc library.a x.o\n" \
            + self.link_line()
        result = fixture_build.fixture_link_recipe(commands, self.originals,
            Path("/private/0.o"), Path("/private/exe"))
        self.assertNotIn("gcc-ar", " ".join(result))

    def test_extra_application_object_cannot_be_forced_into_fixture_link(self):
        with self.assertRaisesRegex(RuntimeError, "exactly its declared"):
            fixture_build.fixture_link_recipe(
                self.link_line(self.originals + ["CMakeFiles/ARCH.dir/src/api/preview/Preview.cpp.o"]),
                self.originals, Path("0.o"), Path("exe"))

    def test_missing_duplicate_and_ambiguous_link_owner_fail(self):
        for commands in ("", self.link_line() + "\n" + self.link_line(),
                         self.link_line(self.originals[:1]),
                         self.link_line(self.originals + self.originals[:1])):
            with self.subTest(commands=commands), self.assertRaises(RuntimeError):
                fixture_build.fixture_link_recipe(commands, self.originals, Path("0.o"), Path("exe"))

    def test_stray_test_unknown_production_or_duplicate_production_objects_stay_rejected(self):
        for extra in ("CMakeFiles/arch_gravity_stage_contract.dir/tests/host/driver/test_stray.cpp.o",
                      "CMakeFiles/arch_gravity_stage_contract.dir/src/driver/stages/UnknownStage.cpp.o",
                      self.production_objects[0]):
            with self.subTest(extra=extra), self.assertRaisesRegex(RuntimeError, "exactly its declared"):
                fixture_build.fixture_link_recipe(self.link_line(self.originals + [extra]),
                    self.originals, Path("0.o"), Path("exe"))
        for missing in self.production_objects:
            line = self.link_line().replace(" " + missing, "")
            with self.subTest(missing=missing), self.assertRaisesRegex(RuntimeError, "exactly its declared"):
                fixture_build.fixture_link_recipe(line, self.originals, Path("0.o"), Path("exe"))

    def test_replacement_caller_cannot_claim_a_different_test_owner_list(self):
        forged = self.originals[:-1] + ["CMakeFiles/arch_gravity_stage_contract.dir/tests/host/driver/test_forged.cpp.o"]
        with self.assertRaisesRegex(RuntimeError, "exactly its declared"):
            fixture_build.fixture_link_recipe(self.link_line(forged), forged, Path("0.o"), Path("exe"))

    def test_selected_link_refuses_shell_tokens(self):
        for suffix in (" && execute-other", " ; execute-other", " | execute-other", " > output"):
            with self.subTest(suffix=suffix), self.assertRaisesRegex(RuntimeError, "scaffolding"):
                fixture_build.fixture_link_recipe(self.link_line() + suffix,
                    self.originals, Path("0.o"), Path("exe"))

    def test_target_compile_recipe_retains_lto_and_declared_production_openmp_only(self):
        entry = {"command": "c++ -DARCH_CUDA_BUILD_ENABLED=0 -Itests -O3 -flto=auto "
                            "-fno-fast-math -ffp-contract=off -o original.o -c original.cpp"}
        production = {"command": "c++ -DARCH_CUDA_BUILD_ENABLED=0 -DARCH_OPENMP_ENABLED=1 "
                                 "-DAPP_ONLY=1 -fopenmp -o main.o -c main.cpp"}
        result, additions = fixture_build.fixture_compile_recipe(entry, production,
            Path("/source/fixture.cpp"), Path("/private/0.o"))
        self.assertEqual(additions, ["-fopenmp", "-DARCH_OPENMP_ENABLED=1"])
        self.assertNotIn("-DAPP_ONLY=1", result)
        self.assertIn("-flto=auto", result)
        self.assertIn("-ffp-contract=off", result)
        self.assertEqual(result[result.index("-c") + 1], "/source/fixture.cpp")

    def test_private_standalone_removes_only_exact_embedded_switch(self):
        entry = {"command": "c++ -DARCH_CUDA_BUILD_ENABLED=0 -DARCH_RZ_RUNTIME_CONTRACT_EMBEDDED=1 "
                 "-DARCH_HAS_KLU=1 -O3 -flto=auto -fno-fast-math -ffp-contract=off -o x.o -c x.cpp"}
        production = {"command": "c++ -DARCH_CUDA_BUILD_ENABLED=0 -fopenmp -o main.o -c main.cpp"}
        tokens, added = fixture_build.fixture_compile_recipe(entry, production, Path("fixture.cpp"), Path("0.o"))
        self.assertNotIn("-DARCH_RZ_RUNTIME_CONTRACT_EMBEDDED=1", tokens)
        for original in ("-DARCH_HAS_KLU=1", "-O3", "-flto=auto", "-fno-fast-math", "-ffp-contract=off"):
            self.assertIn(original, tokens)
        self.assertEqual(added, ["-fopenmp"])
        for bad in ("-DARCH_RZ_RUNTIME_CONTRACT_EMBEDDED=0", "-DARCH_RZ_RUNTIME_CONTRACT_EMBEDDED",
                    "-DARCH_RZ_RUNTIME_CONTRACT_EMBEDDED=1 -DARCH_RZ_RUNTIME_CONTRACT_EMBEDDED=1"):
            with self.subTest(bad=bad), self.assertRaisesRegex(RuntimeError, "unexpected embedded"):
                fixture_build.fixture_compile_recipe(
                    {"command": "c++ -DARCH_CUDA_BUILD_ENABLED=0 " + bad + " -o x.o -c x.cpp"},
                    production, Path("fixture.cpp"), Path("0.o"))

    def test_openmp_is_neither_invented_nor_duplicated(self):
        base = "c++ -DARCH_CUDA_BUILD_ENABLED=0 -o original.o -c original.cpp"
        for suffix, expected in (("", []), (" -fopenmp -DARCH_OPENMP_ENABLED=1", [])):
            entry = {"command": base + suffix}
            result, additions = fixture_build.fixture_compile_recipe(entry, entry,
                Path("fixture.cpp"), Path("0.o"))
            self.assertEqual(additions, expected)
            self.assertLessEqual(result.count("-fopenmp"), 1)

    def test_device_compile_and_duplicate_output_option_fail_closed(self):
        for command in ("c++ -DARCH_CUDA_BUILD_ENABLED=1 -o x.o -c x.cpp",
                        "c++ -DARCH_CUDA_BUILD_ENABLED=0 -o x.o -o y.o -c x.cpp"):
            entry = {"command": command}
            with self.subTest(command=command), self.assertRaises(RuntimeError):
                fixture_build.fixture_compile_recipe(entry, entry, Path("fixture.cpp"), Path("0.o"))

    def test_compile_entry_ambiguity_cannot_select_another_target(self):
        source = Path("/project/fixture.cpp")
        with self.assertRaisesRegex(RuntimeError, "ambiguous"):
            fixture_build.unique_entry([{"file": str(source)}, {"file": str(source)}], source)

    def test_dry_run_rejects_configure_compile_and_link_without_real_build(self):
        for work in ("Re-running CMake...", "Building CXX object owner.o", "Linking CXX static library owner.a"):
            output = "[1/2] " + work + "\n"
            with self.subTest(work=work), \
                    self.assertRaisesRegex(RuntimeError, "not frozen/fresh"):
                fixture_build._require_no_target_work(output)

    def test_fresh_exact_manifest_dry_run_requires_actual_no_work(self):
        fixture_build._require_no_target_work("ninja: no work to do.\n")
        for output in ("", "[0/1] Re-checking globbed directories...\n", "unexpected output\n"):
            with self.subTest(output=output), self.assertRaises(RuntimeError):
                fixture_build._require_no_target_work(output)

    def test_changed_source_or_consumed_archive_identity_rejects(self):
        before = {"source": {"worktree_sha256": "a"}, "files": {"owner.a": {"sha256": "b"}}}
        fixture_build.require_unchanged(before, dict(before))
        for changed in ({**before, "source": {"worktree_sha256": "changed"}},
                        {**before, "files": {"owner.a": {"sha256": "changed"}}}):
            with self.subTest(changed=changed), self.assertRaisesRegex(RuntimeError, "baseline changed"):
                fixture_build.require_unchanged(before, changed)

    def test_actual_consumed_archive_byte_change_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / "owner.a"
            archive.write_bytes(b"frozen compiled owner")
            with mock.patch.object(fixture_build.provenance, "source_identity", return_value={"sha256": "fixed"}):
                before = fixture_build.capture_inputs(root, [archive])
                archive.write_bytes(b"different compiled owner")
                after = fixture_build.capture_inputs(root, [archive])
            self.assertNotEqual(before["files"][str(archive)]["sha256"], after["files"][str(archive)]["sha256"])
            with self.assertRaisesRegex(RuntimeError, "baseline changed"):
                fixture_build.require_unchanged(before, after)

    def test_identical_elf_bytes_with_changed_file_identity_are_not_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture"
            path.write_bytes(b"\x7fELFconstant payload")
            before = fixture_build._stable_file_identity(path)
            info = path.stat()
            os.utime(path, ns=(info.st_atime_ns, info.st_mtime_ns + 1))
            frozen = {"root": Path(directory), "paths": [], "before": {}}
            with mock.patch.object(fixture_build, "capture_inputs", return_value={}), \
                    self.assertRaisesRegex(RuntimeError, "executable changed"):
                fixture_build.verify_fixture_inputs(frozen, path, before)

    def test_reused_owner_is_not_reported_as_recompiled_production(self):
        runner = (ROOT / "validation/amr/run_rz_runtime_boundary.py").read_text()
        self.assertIn('"recompiledSources":{source:source_sha(source)}', runner)
        self.assertIn('"reusedCompiledOwner":build_record["reusedCompiledOwner"]', runner)
        self.assertNotIn('"ninja","-t","commands","ARCH"', runner)

    def test_provider_objects_follow_fixture_and_leave_libraries_in_original_order(self):
        providers = ["/build/CMakeFiles/ARCH.dir/src/io/plot/PlotIO.cpp.o",
                     "/build/CMakeFiles/arch_solver_dispatch.dir/src/driver/io/DriverIO.cpp.o"]
        original = fixture_build.fixture_link_recipe(self.link_line(), self.originals,
            Path("/private/0.o"), Path("/private/exe"))
        result = fixture_build.fixture_link_recipe(self.link_line(), self.originals,
            Path("/private/0.o"), Path("/private/exe"), providers)
        index = result.index("/private/0.o")
        self.assertEqual(result[index + 1:index + 3], providers)
        self.assertEqual(result[:index + 1] + result[index + 3:], original)

    def test_provider_objects_cannot_duplicate_fixture_or_import_a_full_archive(self):
        for providers in (["a.o", "a.o"], [self.originals[0]], [self.production_objects[0]], ["/private/0.o"],
                          ["libarch_solver_dispatch.a"]):
            with self.subTest(providers=providers), self.assertRaisesRegex(RuntimeError, "provider object"):
                fixture_build.fixture_link_recipe(self.link_line(), self.originals,
                    Path("/private/0.o"), Path("/private/exe"), providers)
        with self.assertRaisesRegex(RuntimeError, "full Dispatch archive"):
            fixture_build.fixture_link_recipe(self.link_line().replace("libarch_driver_runtime.a",
                "libarch_solver_dispatch.a"), self.originals, Path("0.o"), Path("exe"))

    def test_production_recipe_retains_main_flags_and_borrows_only_tests_include(self):
        original = {"command": "/usr/bin/c++ -DARCH_CUDA_BUILD_ENABLED=0 -I/project/tests "
            "-DTEST_ONLY=1 -o old.o -c old.cpp"}
        production = {"command": "/usr/bin/c++ -DARCH_CUDA_BUILD_ENABLED=0 -DARCH_OPENMP_ENABLED=1 "
            "-I/project/src -O3 -flto=auto -fno-fast-math -fno-math-errno -ffp-contract=off "
            "-fopenmp -o main.o -c main.cpp"}
        result, additions = fixture_build.fixture_compile_recipe(original, production,
            Path("/project/fixture.cpp"), Path("/private/0.o"), recipe="production")
        expected = fixture_build.replace_option(fixture_build.command_tokens(production["command"]),
            "-c", "/project/fixture.cpp")
        expected = fixture_build.replace_option(expected, "-o", "/private/0.o")
        self.assertEqual(result, expected + ["-I/project/tests"])
        self.assertEqual(additions, [])
        self.assertNotIn("-DTEST_ONLY=1", result)
        with self.assertRaisesRegex(RuntimeError, "unknown fixture compile recipe"):
            fixture_build.fixture_compile_recipe(original, production, Path("f.cpp"),
                Path("0.o"), recipe="unchecked-physics")


class PrivateRetainedOwnerTests(unittest.TestCase):
    """Actual-object provenance checks for the two configured source owners only."""
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name) / "source"
        self.build = Path(self.directory.name) / "build"
        self.entries = []
        controls = "-DARCH_CUDA_BUILD_ENABLED=0 -DARCH_RZ_RUNTIME_CONTRACT_EMBEDDED=1 " \
            "-O3 -flto=auto -fno-fast-math -fno-math-errno -ffp-contract=off"
        self.template = {"command": "/usr/bin/c++ " + controls + " -o template.o -c template.cpp"}
        for name, target in zip(fixture_build.LINK_PRODUCTION_SOURCES, fixture_build.LINK_PRODUCTION_OBJECTS):
            source = self.root / name; source.parent.mkdir(parents=True, exist_ok=True)
            source.write_text("retained actual source bytes")
            obj = self.build / target; obj.parent.mkdir(parents=True, exist_ok=True)
            obj.write_bytes(b"retained actual object bytes")
            self.entries.append({"file": str(source), "directory": str(self.build), "output": target,
                "command": "/usr/bin/c++ " + controls + " -o " + target + " -c " + str(source)})

    def selected(self):
        return fixture_build.selected_link_owners(self.entries, self.root, self.build, self.template)

    def test_exact_sources_and_objects_are_authenticated_without_execution(self):
        retained = self.selected()
        self.assertEqual([x["source"] for x in retained], list(fixture_build.LINK_PRODUCTION_SOURCES))
        self.assertEqual([x["ninjaTarget"] for x in retained], list(fixture_build.LINK_PRODUCTION_OBJECTS))
        self.assertTrue(all(x["owner"] == fixture_build.TARGET for x in retained))
        for record in retained:
            self.assertEqual(Path(record["object"]), (self.build / record["ninjaTarget"]).resolve())

    def test_foreign_build_missing_object_operand_drift_or_fp_drift_is_rejected(self):
        original = dict(self.entries[0])
        changes = (
            {"directory": str(self.root)},
            {"command": original["command"].replace("-c " + original["file"], "-c /foreign.cpp")},
            {"command": original["command"].replace("-fno-fast-math", "-ffast-math")},
        )
        for changed in changes:
            with self.subTest(changed=changed), self.assertRaises(RuntimeError):
                self.entries[0] = {**original, **changed}; self.selected()
        self.entries[0] = original
        Path(self.build / original["output"]).unlink()
        with self.assertRaisesRegex(RuntimeError, "missing"):
            self.selected()

    def test_retained_object_bytes_and_identity_enter_the_same_frozen_input_guard(self):
        retained = self.selected(); paths = [Path(record["object"]) for record in retained]
        with mock.patch.object(fixture_build.provenance, "source_identity", return_value={"sha256": "frozen"}):
            before = fixture_build.capture_inputs(self.root, paths)
            paths[0].write_bytes(b"changed actual object bytes")
            after = fixture_build.capture_inputs(self.root, paths)
        with self.assertRaisesRegex(RuntimeError, "baseline changed"):
            fixture_build.require_unchanged(before, after)


class PrivateProviderReuseTests(unittest.TestCase):
    """Selected real-owner shape and identity negatives; no compiler or Ninja runs."""
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / "build"
        self.build.mkdir()
        self.name = "src/core/files/FileFingerprint.cpp"
        self.owner = "ARCH"
        self.source = self.root / self.name
        self.source.parent.mkdir(parents=True)
        self.source.write_text("synthetic compile metadata input; never compiled\n")
        self.object = self.build / "CMakeFiles/ARCH.dir" / (self.name + ".o")
        self.object.parent.mkdir(parents=True)
        self.object.write_bytes(b"synthetic object identity; never linked")
        self.controls = ["/usr/bin/c++", "-DARCH_CUDA_BUILD_ENABLED=0", "-DARCH_HAS_KLU=1",
            "-DARCH_OPENMP_ENABLED=1", "-O3", "-march=native", "-std=gnu++20", "-flto=auto",
            "-fno-fat-lto-objects", "-fno-fast-math", "-fno-math-errno", "-ffp-contract=off",
            "-fopenmp", "-DNDEBUG"]
        self.entry = {"directory": str(self.build), "file": str(self.source),
            "output": str(self.object.relative_to(self.build)),
            "command": shlex.join(self.controls + ["-o", str(self.object), "-c", str(self.source)])}
        self.production = {"command": shlex.join(self.controls + ["-o", "main.o", "-c", "main.cpp"])}

    def select(self, entries=None, mapping=None):
        return fixture_build.selected_providers([self.entry] if entries is None else entries,
            self.root, self.build, {self.name: self.owner} if mapping is None else mapping, self.production)

    def test_source_target_pair_selects_correct_owner_and_keeps_recipe_provenance(self):
        other = {**self.entry, "output": "CMakeFiles/arch_preview_cellular_reference.dir/" + self.name + ".o"}
        result = self.select([other, self.entry])
        self.assertEqual(result, [{"source": self.name, "owner": self.owner,
            "object": str(self.object), "ninjaTarget": self.entry["output"], "compileEntry": self.entry}])

    def test_duplicate_or_missing_exact_target_cannot_be_guessed(self):
        for entries in ([], [self.entry, dict(self.entry)],
                        [{**self.entry, "output": "CMakeFiles/other.dir/" + self.name + ".o"}]):
            with self.subTest(entries=entries), self.assertRaisesRegex(RuntimeError, "ambiguous"):
                self.select(entries)

    def test_main_runtime_and_arbitrary_source_or_wrong_target_are_not_reusable(self):
        for mapping in ({"src/main.cpp": "ARCH"}, {"src/driver/runtime/DriverRuntime.cpp": "ARCH"},
                        {"src/driver/runtime/DriverBoundaryDiagnostics.cpp": "arch_driver_runtime"},
                        {"src/physics/boundary/PhysicalBoundaryHandler.cpp": "ARCH"},
                        {self.name: "arch_preview_cellular_reference"},
                        {"src/driver/dispatch/bindings/Dispatch_RK3.cpp": "arch_solver_dispatch"}):
            with self.subTest(mapping=mapping), self.assertRaisesRegex(RuntimeError, "unapproved"):
                self.select(mapping=mapping)

    def test_wrong_directory_missing_source_or_object_escape_fail_before_use(self):
        for entry in ({**self.entry, "directory": str(self.root)},
                      {**self.entry, "output": "CMakeFiles/ARCH.dir/../../../escape.o"}):
            with self.subTest(entry=entry), self.assertRaisesRegex(RuntimeError, "another build"):
                self.select([entry])
        self.object.unlink()
        with self.assertRaisesRegex(RuntimeError, "missing"):
            self.select()
        self.object.write_bytes(b"synthetic object")
        self.source.unlink()
        with self.assertRaisesRegex(RuntimeError, "missing"):
            self.select()

    def test_object_symlink_may_not_escape_its_compiled_target(self):
        outside = self.build / "outside.o"
        outside.write_bytes(b"wrong target allocation")
        self.object.unlink()
        self.object.symlink_to(outside)
        with self.assertRaisesRegex(RuntimeError, "another build"):
            self.select()

    def test_source_and_output_argv_must_match_actual_database_owner(self):
        for command in (self.entry["command"].replace(str(self.source), str(self.root / "wrong.cpp")),
                        self.entry["command"].replace(str(self.object), str(self.build / "wrong.o")),
                        self.entry["command"] + " -o duplicate.o"):
            with self.subTest(command=command), self.assertRaisesRegex(RuntimeError, "operands disagree"):
                self.select([{**self.entry, "command": command}])

    def test_strict_fp_lto_openmp_feature_and_compiler_controls_cannot_change(self):
        for old, new in (("-flto=auto", ""), ("-fno-fast-math", "-ffast-math"),
                         ("-ffp-contract=off", "-ffp-contract=fast"), ("-O3", "-O0"),
                         ("-DARCH_HAS_KLU=1", "-DARCH_HAS_KLU=0"),
                         ("-DARCH_CUDA_BUILD_ENABLED=0", "-DARCH_CUDA_BUILD_ENABLED=1"),
                         ("-fopenmp", ""), ("/usr/bin/c++", "/different/compiler")):
            with self.subTest(old=old), self.assertRaisesRegex(RuntimeError, "compile contract"):
                self.select([{**self.entry, "command": self.entry["command"].replace(old, new)}])

    def test_build_identity_source_macro_is_the_only_nonphysics_control_exemption(self):
        entry = {**self.entry, "command": self.entry["command"] + ' -DARCH_IDENTITY_BUILD_CONFIG="Release"'}
        self.assertEqual(len(self.select([entry])), 1)
        with self.assertRaisesRegex(RuntimeError, "compile contract"):
            self.select([{**self.entry, "command": self.entry["command"] + " -DARCH_ARBITRARY_PHYSICS=1"}])

    def test_actual_provider_content_or_metadata_change_rejects_frozen_identity(self):
        with mock.patch.object(fixture_build.provenance, "source_identity", return_value={"sha256": "fixed"}):
            before = fixture_build.capture_inputs(self.root, [self.source, self.object])
            self.object.write_bytes(b"changed selected provider")
            with self.assertRaisesRegex(RuntimeError, "baseline changed"):
                fixture_build.require_unchanged(before,
                    fixture_build.capture_inputs(self.root, [self.source, self.object]))

    def test_io_identity_exact_source_digest_and_only_two_names_are_generated(self):
        for name in fixture_build.IO_FIXTURES:
            source = self.root / name
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_text("synthetic IO source identity\n")
            result = fixture_build.fixture_identity_defines(self.root, source, True)
            self.assertEqual(result, [f'-DARCH_IO_FIXTURE_SOURCE_FILE="{source}"',
                f'-DARCH_IO_FIXTURE_SOURCE_SHA256="{provenance.sha256(source)}"'])
        self.assertEqual(fixture_build.fixture_identity_defines(self.root, self.source, False), [])
        for source in (self.source, self.root.parent / "test_driver_checkpoint_geometry.cpp"):
            with self.subTest(source=source), self.assertRaisesRegex(RuntimeError, "restricted"):
                fixture_build.fixture_identity_defines(self.root, source, True)
        with self.assertRaisesRegex(RuntimeError, "boolean"):
            fixture_build.fixture_identity_defines(self.root, self.source, {"gamma": "2"})

    def test_three_runner_contracts_recompile_only_fixture_and_keep_declared_run_controls(self):
        for path in ("validation/amr/run_rz_initial_population.py",
                     "validation/io/run_driver_checkpoint_geometry.py",
                     "validation/io/run_rz_checkpoint_continuation.py"):
            text = (ROOT / path).read_text()
            with self.subTest(path=path):
                self.assertIn('"recompiledSources":{source:source_sha(source)}', text)
                self.assertIn('compile_recipe="production"', text)
                self.assertIn('"OMP_NUM_THREADS":"2"', text)
                if path != "validation/io/run_rz_checkpoint_continuation.py":
                    self.assertIn('timeout=30', text)
                self.assertIn('verify_fixture_inputs(frozen,exe', text)
                self.assertNotIn('"ninja","-t","commands","ARCH"', text)
        continuation = (ROOT / "validation/io/run_rz_checkpoint_continuation.py").read_text()
        self.assertIn('"--initial-thermal-rejection"', continuation)
        self.assertNotIn('"--repair-position"', continuation)

    def test_warm_checkpoint_deadline_charges_compile_and_retains_frozen_providers(self):
        # Source-level runner contract only: no compile, process or model is run.
        # Inspect its actual AST so ordinary30 and warm2400 remain two explicit
        # resource modes, with compilation charged before the remaining run time.
        text = (ROOT / "validation/io/run_rz_checkpoint_continuation.py").read_text()
        tree = ast.parse(text)
        body = tree.body
        def assignment(name):
            found = [(index, node) for index, node in enumerate(body)
                     if isinstance(node, ast.Assign)
                     and any(isinstance(target, ast.Name) and target.id == name
                             for target in node.targets)]
            self.assertEqual(len(found), 1, name)
            return found[0]
        def named_call(node, name):
            return isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == name
        started_index, started = assignment("started")
        self.assertEqual(ast.unparse(started.value), "time.monotonic()")
        remaining_index, remaining = assignment("remaining")
        self.assertIsInstance(remaining.value, ast.IfExp)
        self.assertEqual(ast.unparse(remaining.value.test), "table")
        self.assertEqual(remaining.value.orelse.value, 30.)
        self.assertEqual(ast.unparse(remaining.value.body), "2400.0 - (time.monotonic() - started)")
        builds = [(index, node.value) for index, node in enumerate(body)
                  if isinstance(node, ast.Assign) and named_call(node.value, "build_cpu_fixture")]
        self.assertEqual(len(builds), 1)
        build_index, build = builds[0]
        self.assertLess(started_index, build_index)
        self.assertLess(build_index, remaining_index)
        keywords = {item.arg: ast.unparse(item.value) for item in build.keywords}
        self.assertEqual(keywords["reuse_compiled_sources"], "providers")
        self.assertEqual(keywords["owner_sources"], "list(RUNTIME_SOURCES)")
        self.assertEqual(keywords["compile_recipe"], "'production'")
        runs = [node for node in ast.walk(tree) if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Attribute) and isinstance(node.func.value, ast.Name)
                and node.func.value.id == "subprocess" and node.func.attr == "run"]
        self.assertEqual(len(runs), 1)
        self.assertEqual({item.arg: ast.unparse(item.value) for item in runs[0].keywords}["timeout"], "remaining")
        exclusive = [node for node in body if isinstance(node, ast.If)
                     and ast.unparse(node.test) == "a.warm_native_active and a.initial_thermal_rejection"]
        self.assertEqual(len(exclusive), 1)
        self.assertEqual(ast.unparse(exclusive[0].body[0].value.func), "p.error")
        exhausted = [(index, node) for index, node in enumerate(body) if isinstance(node, ast.If)
                     and ast.unparse(node.test) == "remaining <= 0.0"]
        self.assertEqual(len(exhausted), 1)
        self.assertIsInstance(exhausted[0][1].body[0], ast.Raise)
        tries = [index for index, node in enumerate(body) if isinstance(node, ast.Try)]
        self.assertEqual(len(tries), 1)
        self.assertLess(remaining_index, exhausted[0][0])
        self.assertLess(exhausted[0][0], tries[0])
        fences = [(index, node.value) for index, node in enumerate(body)
                  if isinstance(node, ast.Expr) and named_call(node.value, "verify_fixture_inputs")]
        self.assertEqual(len(fences), 1)
        self.assertGreater(fences[0][0], tries[0])
        self.assertEqual([ast.unparse(argument) for argument in fences[0][1].args],
                         ["frozen", "exe", "build_record['executableIdentity']"])
        self.assertIn('"prospectiveCompileAndRunCapSeconds":2400 if table else None', text)
        self.assertIn('table_identity(table)!=table_before', text)


class PrivateFixtureFreshnessTests(unittest.TestCase):
    """Regeneration metadata counterexamples only; every subprocess is mocked."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.build = Path(self.temporary.name)
        (self.build / "CMakeFiles").mkdir()
        self.script = self.build / "CMakeFiles/VerifyGlobs.cmake"
        self.stamp = self.build / "CMakeFiles/cmake.verify_globs"
        self.force = self.build / "CMakeFiles/VerifyGlobs.cmake_force"
        self.manifest = self.build / "build.ninja"
        self.inputs = [self.stamp, self.script, self.build / "CMakeCache.txt",
                       self.build / "CMakeLists.txt", self.build / "source.cpp", self.build / "header.h"]
        for path in self.inputs + [self.build / "CMakeFiles/rules.ninja"]:
            path.write_text("frozen metadata\n")
        self.script.write_text('# CMAKE generated file: DO NOT EDIT!\n'
            'cmake_policy(SET CMP0009 NEW)\n'
            f'file(GLOB NEW_GLOB LIST_DIRECTORIES false "{self.build}/*.cpp")\n'
            'set(OLD_GLOB\n  "' + str(self.build / "source.cpp") + '"\n  )\n'
            'if(NOT "${NEW_GLOB}" STREQUAL "${OLD_GLOB}")\n'
            '  message("-- GLOB mismatch!")\n'
            f'  file(TOUCH_NOCREATE "{self.stamp}")\nendif()\n')
        self.manifest.write_text("byte-identical metadata; never execute as a build\n")
        self.manifest_bytes = self.manifest.read_bytes()
        frozen_time = 1700000000000000000
        for path in self.inputs + [self.build / "CMakeFiles/rules.ninja"]:
            os.utime(path, ns=(frozen_time, frozen_time))
        os.utime(self.manifest, ns=(frozen_time + 10, frozen_time + 10))
        self.queries = {
            "build.ninja": "build.ninja:\n  input: RERUN_CMAKE\n"
                + "".join("    | " + str(path) + "\n" for path in self.inputs) + "  outputs:\n",
            str(self.force): str(self.force) + ":\n  input: phony\n  outputs:\n    " + str(self.stamp) + "\n",
            str(self.stamp): str(self.stamp) + ":\n  input: VERIFY_GLOBS\n    | "
                + str(self.force) + "\n  outputs:\n    build.ninja\n",
        }
        self.dry_output = "ninja: no work to do.\n"
        self.scan_output = ""
        self.scan_effect = lambda: None
        self.expected_targets = [fixture_build.TARGET]

    def metadata(self, tokens, build):
        self.assertEqual(build, self.build)
        if tokens[:3] == ["ninja", "-t", "query"]:
            output = self.queries[tokens[3]]
        elif tokens == ["ninja", "-t", "commands", str(self.stamp)]:
            output = "/usr/bin/cmake -P " + str(self.script) + "\n"
        elif tokens == ["/usr/bin/cmake", "-P", str(self.script)]:
            self.scan_effect()
            output = self.scan_output
        elif tokens[:2] == ["ninja", "-f"]:
            self.assertEqual(tokens[3:], ["-n", *self.expected_targets])
            self.assertEqual(Path(tokens[2]).read_bytes(), self.manifest_bytes)
            self.assertNotEqual(Path(tokens[2]), self.manifest)
            output = self.dry_output
        else:
            self.fail("unexpected command; a configure/compiler must never run: " + repr(tokens))
        return mock.Mock(returncode=0, stdout=output)

    def check(self):
        with mock.patch.object(fixture_build, "_metadata_command", side_effect=self.metadata):
            return fixture_build.require_fresh_target(self.build)

    def test_exact_copy_dry_run_retains_real_inputs_and_does_not_edit_production(self):
        before = fixture_build._stable_file_identity(self.manifest)
        result = self.check()
        self.assertEqual(result["manifestIdentity"], before)
        self.assertEqual(fixture_build._stable_file_identity(self.manifest), before)
        self.assertEqual(result["temporaryManifestSha256"], before["sha256"])
        self.assertEqual(len(result["rerunInputObservations"]), len(self.inputs))
        self.assertEqual(result["globScanArgv"], ["/usr/bin/cmake", "-P", str(self.script)])
        self.assertFalse(Path(result["dryRunArgv"][2]).exists())

    def test_source_header_cmake_and_cache_change_each_reject_before_scan(self):
        for name in ("source.cpp", "header.h", "CMakeLists.txt", "CMakeCache.txt"):
            path = self.build / name
            before = path.stat()
            os.utime(path, ns=(before.st_atime_ns, self.manifest.stat().st_mtime_ns + 1))
            with self.subTest(name=name), self.assertRaisesRegex(RuntimeError, "input newer"):
                self.check()
            os.utime(path, ns=(before.st_atime_ns, before.st_mtime_ns))

    def test_missing_real_input_cannot_be_ignored_as_phony(self):
        (self.build / "header.h").unlink()
        with self.assertRaisesRegex(RuntimeError, "missing real"):
            self.check()

    def test_unknown_missing_force_is_not_exempt_without_exact_graph_proof(self):
        self.queries[str(self.force)] = self.queries[str(self.force)].replace("input: phony", "input: SOMETHING")
        with self.assertRaisesRegex(RuntimeError, "metadata owner"):
            self.check()

    def test_phony_force_with_real_input_is_not_exempt(self):
        self.queries[str(self.force)] = self.queries[str(self.force)].replace(
            "  outputs:", "    | changed-config.cmake\n  outputs:")
        with self.assertRaisesRegex(RuntimeError, "unproved"):
            self.check()

    def test_real_regeneration_rule_cannot_be_replaced_by_phony(self):
        self.queries["build.ninja"] = self.queries["build.ninja"].replace("input: RERUN_CMAKE", "input: phony")
        with self.assertRaisesRegex(RuntimeError, "metadata owner"):
            self.check()

    def test_glob_stamp_and_generated_script_must_both_be_real_prerequisites(self):
        for path in (self.stamp, self.script):
            original = self.queries["build.ninja"]
            self.queries["build.ninja"] = original.replace("    | " + str(path) + "\n", "")
            with self.subTest(path=path), self.assertRaisesRegex(RuntimeError, "unproved"):
                self.check()
            self.queries["build.ninja"] = original

    def test_generated_scan_rejects_process_include_and_arbitrary_touch(self):
        original = self.script.read_text()
        before = self.script.stat()
        for suffix in ('execute_process(COMMAND dangerous)\n', 'include(other.cmake)\n',
                       'file(TOUCH "/other/path")\n'):
            self.script.write_text(original + suffix)
            os.utime(self.script, ns=(before.st_atime_ns, before.st_mtime_ns))
            with self.subTest(suffix=suffix), self.assertRaisesRegex(RuntimeError, "unsafe"):
                self.check()
        self.script.write_text(original)
        os.utime(self.script, ns=(before.st_atime_ns, before.st_mtime_ns))

    def test_generated_scan_rejects_untrusted_variable_in_glob(self):
        before = self.script.stat()
        self.script.write_text(self.script.read_text().replace(str(self.build) + "/*.cpp", "${EXTERNAL}/*.cpp"))
        os.utime(self.script, ns=(before.st_atime_ns, before.st_mtime_ns))
        with self.assertRaisesRegex(RuntimeError, "unsafe"):
            self.check()

    def test_glob_membership_change_touch_rejects_instead_of_configuring(self):
        self.scan_effect = lambda: self.stamp.write_text("changed glob\n")
        with self.assertRaisesRegex(RuntimeError, "input newer"):
            self.check()

    def test_scan_diagnostics_without_touch_are_still_not_silently_accepted(self):
        self.scan_output = "-- GLOB mismatch!\n"
        with self.assertRaisesRegex(RuntimeError, "changed metadata"):
            self.check()

    def test_manifest_or_rules_replacement_during_scan_rejects(self):
        for path in (self.manifest, self.build / "CMakeFiles/rules.ninja"):
            original = path.read_bytes()
            before = path.stat()
            self.scan_effect = lambda path=path: path.write_bytes(b"different manifest/rules\n")
            with self.subTest(path=path), self.assertRaisesRegex(RuntimeError, "manifest/rules changed"):
                self.check()
            path.write_bytes(original)
            os.utime(path, ns=(before.st_atime_ns, before.st_mtime_ns))

    def test_real_header_change_during_dry_run_is_not_accepted(self):
        real_metadata = self.metadata
        def changed(tokens, build):
            result = real_metadata(tokens, build)
            if tokens[:2] == ["ninja", "-f"]:
                (self.build / "header.h").write_text("changed during query\n")
            return result
        with mock.patch.object(fixture_build, "_metadata_command", side_effect=changed), \
                self.assertRaisesRegex(RuntimeError, "input newer"):
            fixture_build.require_fresh_target(self.build)

    def test_actual_pending_compile_or_archive_link_remains_rejected(self):
        for message in ("[1/1] Building CXX object owner.o\n", "[1/1] Linking CXX static library owner.a\n"):
            self.dry_output = message
            with self.subTest(message=message), self.assertRaisesRegex(RuntimeError, "not frozen/fresh"):
                self.check()

    def test_selected_object_targets_share_canonical_no_work_metadata_check(self):
        targets = ["CMakeFiles/ARCH.dir/src/io/chk/ChkIO.cpp.o",
                   "CMakeFiles/arch_solver_dispatch.dir/src/driver/io/DriverIO.cpp.o"]
        self.expected_targets += targets
        with mock.patch.object(fixture_build, "_metadata_command", side_effect=self.metadata):
            result = fixture_build.require_fresh_target(self.build, targets)
        self.assertEqual(result["dryRunArgv"][3:], ["-n", *self.expected_targets])

    def test_stale_selected_object_is_not_rebuilt_or_accepted(self):
        target = "CMakeFiles/ARCH.dir/src/io/chk/ChkIO.cpp.o"
        self.expected_targets += [target]
        self.dry_output = "[1/1] Building CXX object " + target + "\n"
        with mock.patch.object(fixture_build, "_metadata_command", side_effect=self.metadata), \
                self.assertRaisesRegex(RuntimeError, "not frozen/fresh"):
            fixture_build.require_fresh_target(self.build, [target])


if __name__ == "__main__":
    unittest.main()
