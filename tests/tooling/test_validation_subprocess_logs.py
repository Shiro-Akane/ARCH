"""Process/logging contracts only; no real ARCH execution or validation claim."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import validate_backend_results as validation
import validate_cuda_amr_restart as restart
import validation_sanitizer as instrumentation


class ValidationSubprocessLogsTests(unittest.TestCase):
    def test_checkpoint_selection_uses_step_and_phase_not_filename_index(self):
        records = [(Path("run_chk_0097.h5"), {"step": 4, "resume_after_regrid": False}),
                   (Path("run_chk_0002.h5"), {"step": 2, "resume_after_regrid": True})]
        self.assertEqual(restart.select_checkpoint(records, 4, False)[0], Path("run_chk_0097.h5"))
        for invalid in ([], records + [records[0]], [records[1]]):
            with self.assertRaises(RuntimeError):
                restart.select_checkpoint(invalid, 4, False)

    def test_terminal_output_delta_is_derived_and_not_arbitrary(self):
        source = {"checkpoint_metadata": {"chk_file_index": 7, "plt_file_index": 2},
                  "terminal_metadata": {"chk_file_index": 8, "plt_file_index": 3}}
        self.assertEqual(restart.output_index_offsets(source), {"checkpoint": 1, "plot": 1})
        for invalid in (-1, 7, 9, True, None):
            source["terminal_metadata"]["chk_file_index"] = invalid
            with self.assertRaises(RuntimeError):
                restart.output_index_offsets(source)

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.input = self.root / "canonical.par"
        self.input.write_text("geometry = cartesian\ncompute_backend = cpu\n")
        self.arch = self.root / "unit-test-only-ARCH"
        self.case = {"id": "unit_only", "input": "canonical.par", "problem": "Gaussian"}

    def test_timeout_preserves_partial_byte_text_and_empty_streams(self):
        for index, (stdout, stderr, expected_out, expected_err) in enumerate((
                (b"before timeout\n", b"device failure \xff", "before timeout\n", "device failure \ufffd"),
                ("text before timeout", "text error", "text before timeout", "text error"),
                (None, None, "", ""))):
            with self.subTest(index=index):
                lane = self.root / str(index)
                lane.mkdir()
                error = subprocess.TimeoutExpired("unit-only", 1, output=stdout, stderr=stderr)
                with mock.patch.object(validation.subprocess, "run", side_effect=error):
                    with self.assertRaises(subprocess.TimeoutExpired) as raised:
                        validation.run_arch_with_logs([str(self.arch)], source_root=self.root,
                            lane_root=lane, timeout=1)
                self.assertIs(raised.exception, error)
                self.assertEqual((lane / "arch.stdout").read_text(), expected_out)
                self.assertEqual((lane / "arch.stderr").read_text(), expected_err)

    def test_all_formal_lanes_persist_timeout_logs_and_remain_failed(self):
        calls = (
            (lambda output: validation.run_arch_lane(self.arch, self.root, self.case, "cpu", 2, output),
             Path("unit_only/step-2/cpu")),
            (lambda output: validation.run_arch_terminal_lane(self.arch, self.root, self.case,
                                                             "cuda", 0.1, output),
             Path("unit_only/scientific/cuda")),
            (lambda output: restart.run_lane(arch=self.arch, source_root=self.root,
                checkpoint_validator=self.arch,
                canonical_input=self.input, output_root=output, name="cpu_source",
                problem="Gaussian", backend="cpu", max_steps=3), Path("cpu_source")),
        )
        for index, (call, relative_lane) in enumerate(calls):
            with self.subTest(lane=str(relative_lane)):
                output = self.root / f"timeout-{index}"
                error = subprocess.TimeoutExpired("unit-only", 1,
                    output=b"partial stdout", stderr=b"partial stderr")
                with mock.patch.object(validation.subprocess, "run", side_effect=error):
                    with self.assertRaises(subprocess.TimeoutExpired):
                        call(output)
                lane = output / relative_lane
                self.assertEqual((lane / "arch.stdout").read_text(), "partial stdout")
                self.assertEqual((lane / "arch.stderr").read_text(), "partial stderr")
                self.assertFalse((output / "backend-validation-evidence.json").exists())
                self.assertFalse((output / "restart-validation-evidence.json").exists())

    def test_nonzero_exit_still_fails_after_saving_complete_logs(self):
        completed = subprocess.CompletedProcess([str(self.arch)], 77,
            stdout="complete stdout", stderr="CUDA unavailable")
        with mock.patch.object(validation.subprocess, "run", return_value=completed):
            with self.assertRaisesRegex(RuntimeError, "failed: 77"):
                validation.run_arch_lane(self.arch, self.root, self.case, "cuda", 2, self.root / "output")
        lane = self.root / "output/unit_only/step-2/cuda"
        self.assertEqual((lane / "arch.stdout").read_text(), "complete stdout")
        self.assertEqual((lane / "arch.stderr").read_text(), "CUDA unavailable")

    def test_sanitizer_requires_process_and_complete_clean_summaries(self):
        process = "========= Process ID: 123\n"
        memcheck = process + "========= ERROR SUMMARY: 0 errors\n" \
            "========= LEAK SUMMARY: 0 bytes leaked in 0 allocations\n"
        racecheck = process + "========= RACECHECK SUMMARY: 0 hazards displayed (0 errors, 0 warnings)\n"
        for tool, good in (("memcheck", memcheck), ("racecheck", racecheck)):
            self.assertTrue(instrumentation.check_report(good, tool))
            for bad in ("", good.replace(process, ""), good + process,
                        good + "========= Warning: incomplete instrumentation\n",
                        good + "========= ERROR SUMMARY: 0 errors\n",
                        good.replace("0 errors", "1 errors")):
                with self.subTest(tool=tool, report=bad), self.assertRaises(RuntimeError):
                    instrumentation.check_report(bad, tool)
        for bad in (memcheck.replace("0 bytes leaked", "4 bytes leaked"),
                    memcheck.split("========= LEAK")[0]):
            with self.assertRaises(RuntimeError):
                instrumentation.check_report(bad, "memcheck")
        with self.assertRaises(ValueError):
            instrumentation.check_report(memcheck, "unknown")

    def test_instrumentation_keeps_actual_application_and_rejects_reused_reports(self):
        executable = self.root / "unit-only-sanitizer"
        executable.write_bytes(b"mock tool, never executed")
        sanitizer = instrumentation.CudaSanitizer(executable, "memcheck")
        application = [str(self.arch), "Gaussian", str(self.input)]
        command = sanitizer.command(application, self.root)
        self.assertEqual(command[-3:], application)
        self.assertEqual(command[0], str(executable))
        self.assertIn("--require-cuda-init", command)
        self.assertEqual(command[command.index("--leak-check") + 1], "full")
        (self.root / "sanitizer.log").write_text("not accepted evidence")
        with self.assertRaises(RuntimeError):
            sanitizer.command(application, self.root)
        executable.write_bytes(b"replacement mock tool")
        with self.assertRaisesRegex(RuntimeError, "changed"):
            sanitizer.evidence(self.root)

    def test_exit_zero_does_not_allow_missing_instrumentation_report(self):
        sanitizer = mock.Mock()
        sanitizer.command.return_value = ["mock-prefix", str(self.arch)]
        sanitizer.evidence.side_effect = RuntimeError("missing instrumented process")
        completed = subprocess.CompletedProcess([], 0, stdout="ran", stderr="")
        with mock.patch.object(validation.subprocess, "run", return_value=completed):
            with self.assertRaisesRegex(RuntimeError, "missing instrumented process"):
                validation.run_arch_with_logs([str(self.arch)], source_root=self.root,
                    lane_root=self.root, timeout=1, sanitizer=sanitizer)
        self.assertEqual((self.root / "arch.stdout").read_text(), "ran")

    def test_failed_instrumented_application_keeps_exit_and_logs(self):
        sanitizer = mock.Mock()
        sanitizer.command.return_value = ["mock-prefix", str(self.arch)]
        completed = subprocess.CompletedProcess([], 86, stdout="ran", stderr="tool failure")
        with mock.patch.object(validation.subprocess, "run", return_value=completed):
            observed = validation.run_arch_with_logs([str(self.arch)], source_root=self.root,
                lane_root=self.root, timeout=1, sanitizer=sanitizer)
        self.assertEqual(observed.returncode, 86)
        sanitizer.evidence.assert_not_called()
        self.assertEqual((self.root / "arch.stderr").read_text(), "tool failure")


if __name__ == "__main__":
    unittest.main()
