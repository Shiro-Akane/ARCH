#!/usr/bin/env python3
"""Synthetic IO tests of stopping semantics; no ARCH process is launched."""
import math
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from subprocess import CompletedProcess

import h5py
import numpy as np

from compare_backends import compare_pair
from run_cuda_matrix import changed_input, one_run
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
