#!/usr/bin/env python3
"""Stopping/IO semantics with synthetic data and real tool children, never ARCH."""
import math
import os
import json
import subprocess
import sys
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from subprocess import CompletedProcess

import h5py
import numpy as np

from compare_backends import compare_pair, compare_cpu_baselines
from run_cuda_matrix import changed_input, one_run, paired_trials, timing_summary, parse_affinity, run_command, main
from verify_coupled import physical_times_agree, verify


def fixture(folder, steps, final_time=1.0, backend="cpu"):
    folder.mkdir(exist_ok=True)
    for epoch, time in enumerate((0.0, final_time)):
        with h5py.File(folder / f"case_plt_{epoch:04}.h5", "w") as file:
            file.attrs["time"] = time
            file.attrs["dim"] = 2
            file["Grid/level"] = [0, 1]
            file["Grid/morton"] = [0, 1]
            for field in ("DENS", "PRES", "TEMP", "ENER", "ENUC", "GPOT",
                          "GACX", "GACY", "c12", "o16"):
                file[f"Data/{field}"] = np.ones((2, 1))
    (folder / "state_repairs.txt").write_text("events=0\n")
    rows = [f"{i} 0 0 0 0 0.125" for i in range(1, steps+1)]
    (folder / "case_log.dat").write_text(
        "\n".join(rows) + f"\nSimulation Done. Total Steps: {steps}\n")
    (folder / "gravity_solves.tsv").write_text(
        "residual\ttarget\titerations\n0.01\t0.1\t2\n")
    (folder / "case_regrid.tsv").write_text("topology_changed\n1\n")
    (folder / "case_backend_plan.txt").write_text(f"resolved={backend}\n")


class PhysicalEndpointTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root/"input").write_bytes(b"# owner input\ncfl=0.4\ntmax=8\n")

    def tearDown(self):
        self.temp.cleanup()

    def test_endpoint_overrides_step_cap_preserves_physics(self):
        source = self.root / "source.par"
        source.write_text("# keep\ntmax=8 # old end\nmax_steps=2\ncfl=0.4\ngamma=1.4\n")
        text = changed_input(source, "cuda", self.root/"run", None, end_time=0.375)
        self.assertIn("tmax=0.375\n", text)
        self.assertIn("max_steps=-1\n", text)
        self.assertIn("cfl=0.4\ngamma=1.4\n", text)
        steps = changed_input(source, "cpu", self.root/"short", 3)
        self.assertIn("tmax=8 # old end\n", steps)
        self.assertIn("max_steps=3\n", steps)

    def test_invalid_stopping_conditions_rejected(self):
        source = self.root / "unused"
        for value in (0.0, -1.0, math.nan, math.inf):
            with self.assertRaises(ValueError):
                changed_input(source, "cpu", self.root, None, end_time=value)
        with self.assertRaises(ValueError):
            changed_input(source, "cpu", self.root, 3, end_time=1.0)
        with self.assertRaises(ValueError):
            changed_input(source, "cpu", self.root, None)

    def test_same_endpoint_different_adaptive_steps(self):
        cpu, cuda = self.root/"cpu", self.root/"cuda"
        fixture(cpu, 2)
        fixture(cuda, 3, backend="cuda")
        result = compare_pair("endpoint", cpu, cuda, None, expected_time=1.0)
        self.assertEqual(result["cpu"]["steps"], 2)
        self.assertEqual(result["cuda"]["steps"], 3)
        self.assertEqual(result["final"]["time_seconds"], 1.0)
        with self.assertRaisesRegex(ValueError, "wrong accepted step count"):
            compare_pair("fixed-steps", cpu, cuda, 2)

    def test_exit_success_below_endpoint_is_not_completion(self):
        folder = self.root/"early"
        fixture(folder, 2, final_time=0.5)
        with self.assertRaisesRegex(ValueError, "endpoint was not reached"):
            verify("early", folder, None, expected_time=1.0)

    def test_scientific_checks_retained(self):
        folder = self.root/"repairs"
        fixture(folder, 2)
        (folder/"state_repairs.txt").write_text("events=1\n")
        with self.assertRaisesRegex(ValueError, "state repairs"):
            verify("repairs", folder, None, expected_time=1.0)
        (folder/"state_repairs.txt").write_text("events=0\n")
        (folder/"gravity_solves.tsv").write_text(
            "residual\ttarget\titerations\n0.2\t0.1\t2\n")
        with self.assertRaisesRegex(ValueError, "missed target"):
            verify("gravity", folder, None, expected_time=1.0)

    def test_missing_rows_and_zero_steps_rejected(self):
        folder = self.root/"rows"
        fixture(folder, 2)
        (folder/"case_log.dat").write_text("1 0 0 0 0 .1\nSimulation Done. Total Steps: 2\n")
        with self.assertRaisesRegex(ValueError, "step rows missing"):
            verify("rows", folder, None, expected_time=1.0)
        (folder/"case_log.dat").write_text("Simulation Done. Total Steps: 0\n")
        with self.assertRaisesRegex(ValueError, "no accepted evolution"):
            verify("zero", folder, None, expected_time=1.0)

    def test_each_run_rejects_early_exit_before_pair_continues(self):
        source = self.root/"input.par"
        source.write_text("tmax=8\nmax_steps=2\n")
        destination = self.root/"early-cpu"
        def process_stub(*args, **kwargs):
            fixture(destination, 2, final_time=0.5)
            return CompletedProcess(args[0], 0, "stub success", "")
        with patch("run_cuda_matrix.subprocess.run", side_effect=process_stub) as process:
            with self.assertRaisesRegex(ValueError, "endpoint was not reached"):
                one_run(self.root/"unused-ARCH", source, destination,
                        "cpu", None, 2, end_time=1.0)
            self.assertEqual(process.call_count, 1)
        self.assertIn("max_steps=-1\n", (destination/"input.par").read_text())
        self.assertTrue((destination/"run.log").exists())

    def test_real_failed_child_streams_full_binary_log_and_bounds_diagnostics(self):
        destination = self.root / "large-log-failure"
        child = (r"import os; os.write(1,b'FIRST-ONLY\n'); "
                 r"os.write(1,b'x'*1048576); os.write(2,b'\xff\nLAST-ERROR\n'); "
                 "raise SystemExit(7)")
        with patch("run_cuda_matrix.run_command", return_value=[sys.executable, "-c", child]):
            with self.assertRaisesRegex(RuntimeError, "launch returned 7") as caught:
                one_run(Path(sys.executable), self.root/"input", destination, "cpu", 2, 1)
        raw = (destination/"run.log").read_bytes()
        self.assertEqual(len(raw), 1048576 + len(b"FIRST-ONLY\n") + len(b"\xff\nLAST-ERROR\n"))
        self.assertTrue(raw.startswith(b"FIRST-ONLY\n"))
        self.assertTrue(raw.endswith(b"\xff\nLAST-ERROR\n"))
        self.assertIn("LAST-ERROR", str(caught.exception))
        self.assertNotIn("FIRST-ONLY", str(caught.exception))
        self.assertLess(len(str(caught.exception)), 66000)
        identity = json.loads((destination/"execution.json").read_text())
        self.assertEqual(identity["launch_exit_code"], 7)

    def test_success_uses_file_descriptor_without_capture_buffer(self):
        destination = self.root / "streamed-success"
        def child(*args, **kwargs):
            self.assertNotIn("capture_output", kwargs)
            self.assertNotIn("text", kwargs)
            self.assertEqual(kwargs["stderr"], subprocess.STDOUT)
            kwargs["stdout"].write(b"live child log\n")
            kwargs["stdout"].flush()
            self.assertEqual((destination/"run.log").read_bytes(), b"live child log\n")
            (destination/"case_backend_plan.txt").write_text("resolved=cpu\n")
            return CompletedProcess(args[0], 0)
        with patch("run_cuda_matrix.subprocess.run", side_effect=child), patch(
                "run_cuda_matrix.driver_seconds", return_value=1), patch(
                "run_cuda_matrix.gravity_times", return_value={}), patch(
                "run_cuda_matrix.regrid_metrics", return_value={}):
            result = one_run(self.root/"unused", self.root/"input", destination, "cpu", 2, 1)
        self.assertEqual(result["execution"]["launch_exit_code"], 0)
        self.assertEqual((destination/"run.log").read_bytes(), b"live child log\n")

    def test_missing_executable_preserves_launch_error_without_retry(self):
        destination = self.root / "missing-executable"
        with self.assertRaises(FileNotFoundError):
            one_run(self.root/"no-such-executable", self.root/"input", destination, "cpu", 2, 1)
        record = json.loads((destination/"execution.json").read_text())
        self.assertIn("launch_error", record)
        self.assertNotIn("launch_exit_code", record)
        self.assertEqual((destination/"run.log").read_bytes(), b"")

    def test_warmup_and_alternating_pairs_keep_explicit_threads(self):
        calls = []
        state = {}
        def run_stub(executable, source, destination, backend, steps, threads, **kwargs):
            calls.append((destination.parent.name, backend, threads, kwargs["end_time"]))
            phase_value = 1000.0 if destination.parent.name == "warmup" else 10.0
            return {"elapsed_seconds": phase_value if backend == "cpu" else phase_value/2,
                    "driver_seconds": phase_value/2, "threads": threads}
        snapshots = []
        import copy
        with patch("run_cuda_matrix.one_run", side_effect=run_stub), patch(
                "run_cuda_matrix.compare_pair", return_value={"verified": True}):
            paired_trials(self.root/"binary", self.root/"input", self.root/"pair",
                          None, 6, 4, 3, end_time=1.0,
                          state=state, notify=lambda: snapshots.append(copy.deepcopy(state)))
        self.assertEqual([backend for _, backend, _, _ in calls],
                         ["cpu", "cuda", "cpu", "cuda", "cuda", "cpu", "cpu", "cuda"])
        self.assertEqual([threads for _, backend, threads, _ in calls if backend == "cuda"],
                         [4, 4, 4, 4])
        self.assertTrue(all(endpoint == 1.0 for _, _, _, endpoint in calls))
        self.assertEqual(state["status"], "passed")
        self.assertEqual(len(state["trials"]), 3)
        metrics = timing_summary(state["trials"])
        self.assertEqual(metrics["cpu"]["elapsed_seconds"]["samples"], [10.0]*3)
        self.assertEqual(metrics["end_to_end_speedup"], 2.0)
        self.assertTrue(any(s["attempts"][0]["status"] == "running" for s in snapshots))

    def test_failed_warmup_stops_before_next_backend_and_retains_failure(self):
        state = {}
        with patch("run_cuda_matrix.one_run", side_effect=RuntimeError("early endpoint")) as run:
            with self.assertRaisesRegex(RuntimeError, "early endpoint"):
                paired_trials(self.root/"binary", self.root/"input", self.root/"pair",
                              None, 6, 4, 3, end_time=1.0, state=state)
            self.assertEqual(run.call_count, 1)
        self.assertEqual(state["status"], "failed")
        self.assertEqual(state["trials"], [])
        self.assertEqual(state["attempts"][0]["active_backend"], "cpu")
        self.assertEqual(state["attempts"][0]["error"], "early endpoint")

    def test_scheduler_refuses_insufficient_endpoint_pairs(self):
        with patch("run_cuda_matrix.one_run") as run:
            for repeats in (1, 2):
                with self.assertRaises(ValueError):
                    paired_trials(self.root/"binary", self.root/"input", self.root/"pair",
                                  None, 6, 4, repeats, end_time=1.0)
            run.assert_not_called()

    def test_legacy_short_check_has_no_warmup(self):
        with patch("run_cuda_matrix.one_run", return_value={"elapsed_seconds": 1.0}) as run, patch(
                "run_cuda_matrix.compare_pair", return_value={}):
            state = paired_trials(self.root/"binary", self.root/"input", self.root/"pair",
                                  2, 8, 1, 1)
        self.assertIsNone(state["warmup"])
        self.assertEqual(run.call_count, 2)

    def test_timing_summary_preserves_negative_speedup_and_invalid_values(self):
        trials = [{"cpu": {"elapsed_seconds": value, "driver_seconds": value},
                   "cuda": {"elapsed_seconds": value*2, "driver_seconds": value*2}}
                  for value in (2.0, 1.0, 3.0)]
        metrics = timing_summary(trials)
        self.assertEqual(metrics["end_to_end_speedup"], 0.5)
        self.assertEqual(metrics["cpu"]["elapsed_seconds"]["samples"], [2.0, 1.0, 3.0])
        self.assertEqual(metrics["cpu"]["elapsed_seconds"]["minimum"], 1.0)
        self.assertEqual(metrics["cpu"]["elapsed_seconds"]["maximum"], 3.0)
        for value in (0.0, -1.0, math.inf, math.nan):
            trials[0]["cpu"]["elapsed_seconds"] = value
            with self.assertRaises(ValueError):
                timing_summary(trials)

    def test_affinity_list_validation(self):
        self.assertEqual(parse_affinity("3,0-1", {0, 1, 3}), (0, 1, 3))
        for text in ("", "0,0", "0-2,1", "2-1", "-1", "0;echo", "0,,1", "4096", "0-999999", "2"):
            with self.assertRaises(ValueError, msg=text):
                parse_affinity(text, {0, 1})

    def test_actual_linux_taskset_child_binding(self):
        selected = min(os.sched_getaffinity(0))
        result = subprocess.run(
            ["/usr/bin/taskset", "--cpu-list", str(selected), sys.executable,
             "-c", "import os,json; print(json.dumps(sorted(os.sched_getaffinity(0))))"],
            check=True, capture_output=True, text=True)
        self.assertEqual(json.loads(result.stdout), [selected])

    def test_affinity_command_is_argv_and_no_fallback(self):
        with patch("run_cuda_matrix.os.sched_getaffinity", return_value={2, 4}):
            command = run_command(Path("/safe path/ARCH"), Path("/config path/input.par"), (4, 2))
            self.assertEqual(command, ["/usr/bin/taskset", "--cpu-list", "2,4",
                                      "/safe path/ARCH", "SNIaCoupled", "/config path/input.par"])
            with self.assertRaises(ValueError):
                run_command(Path("/ARCH"), Path("/input"), (3,))
        self.assertEqual(run_command(Path("/ARCH"), Path("/input")),
                         ["/ARCH", "SNIaCoupled", "/input"])

    def test_invalid_affinity_fails_before_output_or_process(self):
        source = self.root/"input.par"
        source.write_text("tmax=1\n")
        destination = self.root/"no-output"
        with patch("run_cuda_matrix.os.sched_getaffinity", return_value={0}), patch(
                "run_cuda_matrix.subprocess.run") as run:
            with self.assertRaises(ValueError):
                one_run(self.root/"ARCH", source, destination, "cpu", 2, 1, affinity=(1,))
            with self.assertRaises(ValueError):
                one_run(self.root/"ARCH", source, destination, "cpu", 2, 2, affinity=(0,))
        run.assert_not_called()
        self.assertFalse(destination.exists())

    def test_scheduler_keeps_backend_affinity_during_warmup_and_pairs(self):
        calls = []
        def run_stub(*args, **kwargs):
            calls.append((args[3], kwargs["affinity"]))
            return {"elapsed_seconds": 1.0, "driver_seconds": 0.5}
        with patch("run_cuda_matrix.one_run", side_effect=run_stub), patch(
                "run_cuda_matrix.compare_pair", return_value={}):
            paired_trials(self.root/"binary", self.root/"input", self.root/"pair",
                          None, 2, 1, 3, end_time=1.0,
                          cpu_affinity=(0, 1), cuda_affinity=(2,))
        self.assertEqual(len(calls), 8)
        self.assertTrue(all(cpus == ((0, 1) if backend == "cpu" else (2,))
                            for backend, cpus in calls))

    def test_failed_taskset_preserves_execution_record_without_retry(self):
        source = self.root/"input.par"
        source.write_text("tmax=1\n")
        destination = self.root/"binding-failure"
        selected = min(os.sched_getaffinity(0))
        def failed_taskset(*args, **kwargs):
            kwargs["stdout"].write(b"taskset: failed\n")
            return CompletedProcess(args[0], 1)
        with patch("run_cuda_matrix.subprocess.run", side_effect=failed_taskset) as run:
            with self.assertRaisesRegex(RuntimeError, "launch returned 1"):
                one_run(self.root/"ARCH", source, destination, "cpu", 2, 1, affinity=(selected,))
        self.assertEqual(run.call_count, 1)
        evidence = json.loads((destination/"execution.json").read_text())
        self.assertEqual(evidence["requested_affinity"], [selected])
        self.assertEqual(evidence["affinity_enforcement"], "taskset-before-exec")
        self.assertEqual(evidence["launch_exit_code"], 1)
        self.assertIsNone(evidence["actual_openmp_team_size"])
        self.assertEqual(evidence["openmp_environment"]["OMP_PROC_BIND"], "spread")
        self.assertIn("taskset: failed", (destination/"run.log").read_text())

    def test_original_input_edit_does_not_change_frozen_campaign(self):
        source = self.root/"input"
        original = source.read_bytes()
        calls = []
        def run_stub(executable, frozen, *args, **kwargs):
            calls.append(frozen.read_bytes())
            source.write_text("cfl=0.9\n")
            return {"elapsed_seconds": 1.0, "driver_seconds": 0.5}
        with patch("run_cuda_matrix.one_run", side_effect=run_stub), patch(
                "run_cuda_matrix.compare_pair", return_value={}):
            state = paired_trials(self.root/"binary", source, self.root/"pair", None, 1, 1, 3, end_time=1.0)
        self.assertEqual(calls, [original]*8)
        self.assertEqual(state["source"]["sha256"], __import__("hashlib").sha256(original).hexdigest())

    def test_frozen_input_tampering_stops_before_next_backend(self):
        def run_stub(executable, frozen, *args, **kwargs):
            frozen.chmod(0o644)
            frozen.write_text("cfl=0.9\n")
            return {"elapsed_seconds": 1.0}
        state = {}
        with patch("run_cuda_matrix.one_run", side_effect=run_stub) as run:
            with self.assertRaisesRegex(ValueError, "frozen source input changed"):
                paired_trials(self.root/"binary", self.root/"input", self.root/"pair",
                              None, 1, 1, 3, end_time=1.0, state=state)
        self.assertEqual(run.call_count, 1)
        self.assertEqual(state["status"], "failed")
        self.assertEqual(state["trials"], [])

    def test_cli_collision_and_bad_labels_preserve_existing_evidence(self):
        binary = self.root/"binary"
        binary.write_bytes(b"not executed")
        existing = self.root/"existing"
        existing.mkdir()
        sentinel = b"old independent evidence"
        (existing/"summary.json").write_bytes(sentinel)
        base = ["runner", "--arch", str(binary), "--output", str(existing), "--pair"]
        with patch("sys.argv", base+[f"good:{self.root/'input'}:2"]), patch(
                "run_cuda_matrix.paired_trials") as run:
            with self.assertRaises(FileExistsError):
                main()
        run.assert_not_called()
        self.assertEqual((existing/"summary.json").read_bytes(), sentinel)
        output = self.root/"not-created"
        for labels in (["../escape"], ["/absolute"], ["same", "same"], ["has space"], ["summary.json"]):
            args = ["runner", "--arch", str(binary), "--output", str(output)]
            for label in labels:
                args += ["--pair", f"{label}:{self.root/'input'}:2"]
            with patch("sys.argv", args), patch("run_cuda_matrix.paired_trials") as run:
                with self.assertRaises(SystemExit) as error:
                    main()
                self.assertEqual(error.exception.code, 2)
            run.assert_not_called()
            self.assertFalse(output.exists())

    def test_missing_binary_does_not_create_campaign_output(self):
        output = self.root/"no-binary-output"
        args = ["runner", "--arch", str(self.root/"missing"), "--output", str(output),
                "--pair", f"case:{self.root/'input'}:2"]
        with patch("sys.argv", args), patch("run_cuda_matrix.paired_trials") as run:
            with self.assertRaises(FileNotFoundError):
                main()
        run.assert_not_called()
        self.assertFalse(output.exists())

    def test_existing_pair_directory_refuses_before_overwriting_snapshot(self):
        folder = self.root/"pair"
        folder.mkdir()
        (folder/"frozen-source.par").write_bytes(b"preserved")
        with patch("run_cuda_matrix.one_run") as run:
            with self.assertRaises(FileExistsError):
                paired_trials(self.root/"binary", self.root/"input", folder, 2, 1, 1, 1)
        run.assert_not_called()
        self.assertEqual((folder/"frozen-source.par").read_bytes(), b"preserved")

    def test_separate_cpu_baseline_schedule_and_both_speedups(self):
        calls = []
        def run_stub(executable, source, destination, backend, steps, threads, **kwargs):
            role = destination.name
            calls.append((role, executable.name, backend, threads, kwargs["affinity"]))
            seconds = {"cpu_only": 3.0, "cpu": 4.0, "cuda": 2.0}[role]
            return {"elapsed_seconds": seconds, "driver_seconds": seconds/2}
        with patch("run_cuda_matrix.one_run", side_effect=run_stub), patch(
                "run_cuda_matrix.compare_pair", return_value={}), patch(
                "run_cuda_matrix.compare_cpu_baselines", return_value={}) as parity:
            state = paired_trials(self.root/"cuda-release", self.root/"input", self.root/"pair",
                                  None, 6, 2, 3, end_time=1.0,
                                  cpu_only_executable=self.root/"cpu-release",
                                  cpu_affinity=tuple(range(6)), cuda_affinity=(6, 7))
        self.assertEqual([row[0] for row in calls],
                         ["cpu_only", "cpu", "cuda", "cpu_only", "cpu", "cuda",
                          "cuda", "cpu", "cpu_only", "cpu_only", "cpu", "cuda"])
        self.assertTrue(all(row[1:] == ("cpu-release", "cpu", 6, tuple(range(6)))
                            for row in calls if row[0] == "cpu_only"))
        self.assertEqual(parity.call_count, 4)
        summary = timing_summary(state["trials"])
        self.assertEqual(summary["end_to_end_speedup"], 2.0)
        self.assertEqual(summary["cpu_only_end_to_end_speedup"], 1.5)
        self.assertEqual(summary["cpu_only"]["elapsed_seconds"]["samples"], [3.0]*3)
        del state["trials"][0]["cpu_only"]
        with self.assertRaisesRegex(ValueError, "baseline missing"):
            timing_summary(state["trials"])

    def test_cpu_only_failure_stops_campaign_and_keeps_attempt(self):
        state = {}
        with patch("run_cuda_matrix.one_run", side_effect=RuntimeError("CPU-only failed")) as run:
            with self.assertRaisesRegex(RuntimeError, "CPU-only failed"):
                paired_trials(self.root/"cuda", self.root/"input", self.root/"pair",
                              None, 1, 1, 3, end_time=1.0,
                              cpu_only_executable=self.root/"cpu", state=state)
        self.assertEqual(run.call_count, 1)
        self.assertEqual(state["status"], "failed")
        self.assertEqual(state["attempts"][0]["active_backend"], "cpu_only")
        self.assertEqual(state["trials"], [])

    def test_cpu_baseline_comparison_requires_cpu_and_same_science_budget(self):
        left, right = self.root/"cpu-only", self.root/"cpu-in-cuda"
        fixture(left, 2)
        fixture(right, 3)
        result = compare_cpu_baselines("baseline", left, right, None, expected_time=1.0)
        self.assertEqual(result["cpu_only"]["steps"], 2)
        self.assertEqual(result["cpu_in_cuda"]["steps"], 3)
        with self.assertRaisesRegex(ValueError, "resolve CPU"):
            (right/"case_backend_plan.txt").write_text("resolved=cuda\n")
            compare_cpu_baselines("wrong-backend", left, right, None, expected_time=1.0)
        (right/"case_backend_plan.txt").write_text("resolved=cpu\n")
        with h5py.File(right/"case_plt_0001.h5", "r+") as file:
            file["Data/DENS"][...] = 1.001
        with self.assertRaisesRegex(ValueError, "exceeds"):
            compare_cpu_baselines("wrong-science", left, right, None, expected_time=1.0)
        with self.assertRaisesRegex(ValueError, "CUDA run did not resolve"):
            compare_pair("still-requires-cuda", left, right, None, expected_time=1.0)

    def test_cli_rejects_same_binary_cpu_baseline_before_output(self):
        binary = self.root/"binary"
        duplicate = self.root/"same-bytes"
        binary.write_bytes(b"identical")
        duplicate.write_bytes(binary.read_bytes())
        output = self.root/"not-created"
        args = ["runner", "--arch", str(binary), "--cpu-only-arch", str(duplicate),
                "--output", str(output), "--pair", f"case:{self.root/'input'}:2"]
        with patch("sys.argv", args), patch("run_cuda_matrix.paired_trials") as run:
            with self.assertRaises(SystemExit) as error:
                main()
        self.assertEqual(error.exception.code, 2)
        self.assertFalse(output.exists())
        run.assert_not_called()

    def test_existing_parity_budget_not_relaxed(self):
        cpu, cuda = self.root/"cpu", self.root/"cuda"
        fixture(cpu, 2)
        fixture(cuda, 3, backend="cuda")
        with h5py.File(cuda/"case_plt_0001.h5", "r+") as file:
            file["Data/DENS"][...] = 1.001
        with self.assertRaisesRegex(ValueError, "exceeds"):
            compare_pair("parity", cpu, cuda, None, expected_time=1.0)
        for value in (math.nan, math.inf, -math.inf):
            self.assertFalse(physical_times_agree(value, 1.0))

if __name__ == "__main__":
    unittest.main()
