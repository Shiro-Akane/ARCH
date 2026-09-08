"""Smoke-runner contracts only: fake processes are confined to these unit tests."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import smoke_cuda_amr_runtime as smoke


class SmokeRunnerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.source = self.directory / "source"
        self.output = self.directory / "output"
        self.source.mkdir()
        self.output.mkdir()
        self.canonical = self.source / "canonical.par"
        self.canonical.write_text("geometry = cartesian\nuse_diffusion = true\n")
        self.binary = self.directory / "unit-test-only-ARCH"
        self.binary.write_bytes(b"not executable; mocked process only")
        self.validator = self.directory / "unit-test-only-comparator"
        self.validator.write_bytes(b"not executable; mocked metadata reader only")
        self.case = {"id": "unit_case", "problem": "Gaussian", "input": "canonical.par",
                     "steps": 2, "restart_steps": 1,
                     "overrides": {"geometry": "spherical"}}

    def run_lane(self, backend="cpu", restart=None):
        return smoke.run_lane(arch=self.binary, checkpoint_validator=self.validator,
            source_root=self.source, output_root=self.output,
            case=self.case, backend=backend, timeout=1, threads=1, restart=restart,
            restart_parameters=restart.parent / "run.par" if restart else None)

    def success_process(self, command, **kwargs):
        if command[1] == "--metrics":
            self.assertEqual(command[0], str(self.validator))
            self.assertEqual(command[3], "--parameters")
            step = json.loads(Path(command[2]).read_text())["unit_test_only_step"]
            return SimpleNamespace(returncode=0, stderr="", stdout=json.dumps({
                "step": step, "time": step * 0.1,
                "measure": "physical_cell_volume", "geometry": "spherical"}) + "\n")
        parameters = smoke.validation.read_parameter_map(Path(command[-1]))
        directory = Path(parameters["out_dir"])
        name = parameters["base_name"]
        kwargs["stdout"].write(f"Simulation Done. Total Steps: {parameters['max_steps']}\n")
        (directory / f"{name}_backend_plan.txt").write_text(
            f"requested={parameters['compute_backend']}\nresolved={parameters['compute_backend']}\n")
        (directory / f"{name}_chk_0001.h5").write_text(json.dumps(
            {"unit_test_only_step": int(parameters["max_steps"])}))
        return SimpleNamespace(returncode=0)

    def test_manifest_declares_both_curved_geometries_and_dynamic_control(self):
        cases = smoke.cases_from(ROOT / "tests/smoke/cuda_amr_cases.json")
        curved = {case["overrides"].get("geometry") for case in cases if case.get("diffusion")}
        self.assertEqual(curved, {"cylindrical", "spherical"})
        self.assertTrue(any(case.get("require_device_topology_change") for case in cases))
        self.assertTrue(all(int(case["steps"]) <= 3 for case in cases))
        self.assertEqual(sum(2 * (1 + (int(case.get("restart_steps", 0)) > 0))
                             for case in cases), 10)

    def test_success_isolated_parameters_leave_canonical_unchanged(self):
        before = self.canonical.read_bytes()
        with mock.patch.object(smoke.subprocess, "run", side_effect=self.success_process):
            result = self.run_lane()
        self.assertEqual(result["status"], "passed")
        self.assertEqual(result["effective_parameters"]["geometry"], "spherical")
        self.assertEqual(result["effective_parameters"]["compute_backend"], "cpu")
        self.assertEqual(self.canonical.read_bytes(), before)
        self.assertTrue(Path(result["checkpoint"]).is_relative_to(self.output))
        with self.assertRaises(FileExistsError):
            self.run_lane()  # A prior lane may never be overwritten.

    def test_cuda_unavailable_exit_77_is_failed_not_skipped(self):
        with mock.patch.object(smoke.subprocess, "run", return_value=SimpleNamespace(returncode=77)):
            result = self.run_lane("cuda")
        self.assertEqual(result["status"], "failed")
        self.assertEqual(result["returncode"], 77)
        self.assertIn("ARCH exited 77", result["error"])

    def test_timeout_is_failed_and_recorded(self):
        with mock.patch.object(smoke.subprocess, "run",
                               side_effect=subprocess.TimeoutExpired("unit-test-only", 1)):
            result = self.run_lane("cuda")
        self.assertEqual(result["status"], "failed")
        self.assertTrue(result["timed_out"])

    def test_success_exit_without_checkpoint_fails(self):
        def without_checkpoint(command, **kwargs):
            result = self.success_process(command, **kwargs)
            for checkpoint in Path(smoke.validation.read_parameter_map(Path(command[-1]))["out_dir"]).glob("*.h5"):
                checkpoint.unlink()
            return result
        with mock.patch.object(smoke.subprocess, "run", side_effect=without_checkpoint):
            result = self.run_lane()
        self.assertEqual(result["status"], "failed")
        self.assertIn("checkpoint is missing", result["error"])

    def test_normal_restart_cli_continues_total_step_count(self):
        with mock.patch.object(smoke.subprocess, "run", side_effect=self.success_process):
            source = self.run_lane()
            resumed = self.run_lane(restart=Path(source["checkpoint"]))
        self.assertEqual(resumed["status"], "passed")
        self.assertEqual(resumed["expected_total_steps"], 3)
        self.assertEqual(resumed["effective_parameters"]["restart"], "true")
        self.assertEqual(resumed["effective_parameters"]["restart_file"], source["checkpoint"])
        self.assertEqual(resumed["restart_sha256"], source["checkpoint_sha256"])
        self.assertEqual(source["checkpoint_metadata"]["step"], 2)
        self.assertEqual(resumed["restart_metadata"]["step"], 2)
        self.assertEqual(resumed["checkpoint_metadata"]["step"], 3)
        self.assertEqual(resumed["restart_metadata"]["parameter_sha256"], source["parameter_sha256"])

    def test_initial_checkpoint_cannot_replace_missing_terminal_checkpoint(self):
        def initial_only(command, **kwargs):
            result = self.success_process(command, **kwargs)
            if command[1] != "--metrics":
                values = smoke.validation.read_parameter_map(Path(command[-1]))
                name = values["base_name"]
                directory = Path(values["out_dir"])
                terminal = directory / f"{name}_chk_0001.h5"
                initial = directory / f"{name}_chk_0000.h5"
                terminal.rename(initial)
                initial.write_text(json.dumps({"unit_test_only_step": 0}))
            return result
        with mock.patch.object(smoke.subprocess, "run", side_effect=initial_only):
            result = self.run_lane()
        self.assertEqual(result["status"], "failed")
        self.assertIn("checkpoint metadata step mismatch", result["error"])

    def test_restart_source_wrong_step_is_rejected_before_arch_runs(self):
        with mock.patch.object(smoke.subprocess, "run", side_effect=self.success_process):
            source = self.run_lane()
        checkpoint = Path(source["checkpoint"])
        checkpoint.write_text(json.dumps({"unit_test_only_step": 0}))
        with mock.patch.object(smoke.subprocess, "run", side_effect=self.success_process) as run:
            resumed = self.run_lane(restart=checkpoint)
        self.assertEqual(resumed["status"], "failed")
        self.assertIn("checkpoint metadata step mismatch", resumed["error"])
        self.assertEqual(run.call_count, 1)
        self.assertEqual(run.call_args.args[0][1], "--metrics")

    def test_checkpoint_metadata_requires_exact_integer_step(self):
        checkpoint = self.directory / "unit-only.h5"
        checkpoint.write_bytes(b"metadata result mocked separately")
        for step in (0, 1, 3, 2.0, "2", True, None):
            with self.subTest(step=step), mock.patch.object(
                    smoke.validation, "read_conservation_metrics", return_value={"step": step}):
                with self.assertRaisesRegex(RuntimeError, "checkpoint metadata step mismatch"):
                    smoke.checkpoint_metadata(validator=self.validator, checkpoint=checkpoint,
                        parameters=self.canonical, expected_steps=2)

    def test_metadata_comparator_failure_is_failed_not_skipped(self):
        def failing_metrics(command, **kwargs):
            if command[1] == "--metrics":
                return SimpleNamespace(returncode=77, stdout="", stderr="unit-only read failure")
            return self.success_process(command, **kwargs)
        with mock.patch.object(smoke.subprocess, "run", side_effect=failing_metrics):
            result = self.run_lane()
        self.assertEqual(result["status"], "failed")
        self.assertIn("checkpoint metrics failed", result["error"])

    def test_restart_requires_source_parameters(self):
        checkpoint = self.directory / "unit-only.h5"
        checkpoint.write_bytes(b"not a real checkpoint")
        with mock.patch.object(smoke.subprocess, "run") as run:
            result = smoke.run_lane(arch=self.binary, checkpoint_validator=self.validator,
                source_root=self.source, output_root=self.output, case=self.case,
                backend="cpu", timeout=1, threads=1, restart=checkpoint)
        self.assertEqual(result["status"], "failed")
        self.assertIn("source lane's actual parameters", result["error"])
        run.assert_not_called()

    def test_report_explicitly_is_not_scientific_validation(self):
        failed = {"name": "unit-only", "status": "failed", "returncode": 77}
        with mock.patch.object(smoke, "run_lane", return_value=failed):
            code = smoke.main(["--arch", str(self.binary), "--output-root", str(self.output),
                               "--checkpoint-validator", str(self.validator),
                               "--case", "cartesian_dynamic_sedov", "--backend", "cuda"])
        report = json.loads((self.output / "smoke-report.json").read_text())
        self.assertEqual(code, 1)
        self.assertEqual(report["status"], "failed")
        self.assertFalse(report["scientific_validation"])
        self.assertEqual(report["kind"], "development-smoke")
        self.assertEqual(report["backends"], ["cuda"])


if __name__ == "__main__":
    unittest.main()
