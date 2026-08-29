import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "validate_backend_results.py"


def load_module():
    spec = importlib.util.spec_from_file_location(
        "validate_backend_results", MODULE_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load backend validator module")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class BackendValidationTests(unittest.TestCase):
    def test_empty_case_selection_runs_every_manifest_case(self):
        module = load_module()
        manifest = {"cases": [{"id": "alpha"}, {"id": "beta"}]}
        selected = module.select_cases(manifest, [])
        self.assertEqual([case["id"] for case in selected], ["alpha", "beta"])

    def test_manifest_uses_one_canonical_science_input(self):
        module = load_module()
        manifest = module.load_manifest(
            ROOT / "validation" / "backend" / "cases.json")
        self.assertGreaterEqual(len(manifest["cases"]), 3)
        for case in manifest["cases"]:
            self.assertIn("input", case)
            self.assertNotIn("cpu_input", case)
            self.assertNotIn("cuda_input", case)
            self.assertIn("reduction_policy", case)

    def test_render_changes_execution_only(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "case.par"
            source.write_text(
                "rho0 = 1.25\ncompute_backend = cpu\nout_dir = old\n"
                "base_name = old\nmax_steps = -1\n",
                encoding="utf-8")
            output = root / "cuda.par"
            module.render_parameter_file(
                source, output,
                backend="cuda", output_dir=root / "run",
                base_name="case_cuda", accepted_steps=5,
                scientific_overrides={"nblockx1": "2"})
            rendered = module.read_parameter_map(output)
            self.assertEqual(rendered["rho0"], "1.25")
            self.assertEqual(rendered["compute_backend"], "cuda")
            self.assertEqual(rendered["max_steps"], "5")
            self.assertEqual(rendered["nblockx1"], "2")

    def test_terminal_science_render_uses_fixed_physical_time(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "case.par"
            source.write_text(
                "tmax = 1\nmax_steps = 4\nchk_dt = -1\nchk_dstep = 4\n",
                encoding="utf-8")
            output = root / "terminal.par"
            module.render_terminal_parameter_file(
                source, output, backend="cuda", output_dir=root / "run",
                base_name="terminal", terminal_time=0.2)
            rendered = module.read_parameter_map(output)
            self.assertEqual(rendered["compute_backend"], "cuda")
            self.assertEqual(rendered["tmax"], "0.2")
            self.assertEqual(rendered["max_steps"], "-1")
            self.assertEqual(rendered["chk_dt"], "0.2")
            self.assertEqual(rendered["chk_dstep"], "-1")

    def test_cuda_fallback_is_rejected(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as directory:
            plan = Path(directory) / "plan.txt"
            plan.write_text(
                "requested=cuda\nresolved=cpu\nfallback_reason=no device\n",
                encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "resolved CUDA"):
                module.validate_resolved_plan(plan, "cuda")

    def test_synthetic_evidence_requires_explicit_unit_mode(self):
        module = load_module()
        payload = {"synthetic": True, "fields": {"rho": [1.0]}}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "synthetic.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "unit-test"):
                module.read_synthetic_checkpoint(path, unit_test=False)
            self.assertEqual(
                module.read_synthetic_checkpoint(path, unit_test=True)["rho"],
                [1.0])

    def test_hdf_field_mapping_and_first_mismatch(self):
        module = load_module()
        reference = {"rho": [1.0, 2.0], "eng": [3.0, 4.0]}
        candidate = {"rho": [1.0, 2.0], "eng": [3.0, 4.0]}
        policy = {"rtol": 0.0, "atol": 0.0}
        result = module.compare_numeric_fields(reference, candidate, policy)
        self.assertTrue(result["passed"])
        candidate["eng"][1] = 4.5
        result = module.compare_numeric_fields(reference, candidate, policy)
        self.assertFalse(result["passed"])
        self.assertEqual(result["first_mismatch"]["field"], "eng")

    def test_periodic_conservation_checks_every_hydro_invariant(self):
        module = load_module()
        qualification = {
            "mass_absolute_drift_max": 1.0e-11,
            "momentum_absolute_drift_max": 1.0e-11,
            "energy_absolute_drift_max": 1.0e-11,
        }
        metrics = {
            "mass": 1.0,
            "mom_u": 1.0,
            "mom_v": 0.0,
            "mom_w": 0.0,
            "eng_total": 3.0,
            "min_rho": 0.8,
            "min_eng": 2.9,
            "l1": 0.0,
            "mean": 1.0,
        }
        module.validate_qualification_metrics(metrics, qualification, "smooth")
        for field in ("mass", "mom_u", "mom_v", "mom_w", "eng_total"):
            broken = dict(metrics)
            broken[field] += 1.0e-6
            with self.assertRaisesRegex(RuntimeError, "conservation"):
                module.validate_qualification_metrics(
                    broken, qualification, "smooth")

    def test_release_evidence_root_must_be_empty(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            module.require_empty_output_root(root / "fresh")
            (root / "stale").mkdir()
            (root / "stale" / "old-checkpoint.h5").write_bytes(b"stale")
            with self.assertRaisesRegex(RuntimeError, "must be empty"):
                module.require_empty_output_root(root / "stale")

    def test_resolution_group_requires_frozen_l1_and_l2_orders(self):
        module = load_module()
        cases = [
            {
                "id": f"sod_ppm_n{resolution}",
                "resolution": resolution,
                "qualification_group": "sod_ppm",
                "qualification": {
                    "minimum_l1_order": 0.75,
                    "minimum_l2_order": 0.45,
                },
            }
            for resolution in (64, 128, 256)
        ]
        results = [
            {
                "id": case["id"],
                "scientific": {
                    "cpu_qualification": metrics,
                    "cuda_qualification": dict(metrics),
                },
            }
            for case, metrics in zip(
                cases,
                (
                    {"l1": 4.0e-2, "l2": 8.0e-2},
                    {"l1": 2.0e-2, "l2": 5.0e-2},
                    {"l1": 1.0e-2, "l2": 3.0e-2},
                ),
            )
        ]
        summary = module.validate_resolution_groups(cases, results)
        self.assertEqual(summary["sod_ppm"]["resolutions"], [64, 128, 256])
        broken = json.loads(json.dumps(results))
        broken[-1]["scientific"]["cuda_qualification"]["l2"] = 4.9e-2
        with self.assertRaisesRegex(RuntimeError, "L2 convergence"):
            module.validate_resolution_groups(cases, broken)

    def test_sod_qualification_checks_l2_and_shock_position(self):
        module = load_module()
        qualification = {
            "l1_max": 0.05,
            "l2_max": 0.10,
            "shock_position_cells_max": 2.5,
        }
        metrics = {
            "min_rho": 0.1,
            "min_eng": 0.2,
            "l1": 0.02,
            "l2": 0.04,
            "shock_position_error_cells": 1.0,
        }
        module.validate_qualification_metrics(
            metrics, qualification, "sod", scientific=True)
        for field in ("l2", "shock_position_error_cells"):
            broken = dict(metrics)
            broken[field] = 3.0
            with self.assertRaisesRegex(RuntimeError, "Sod"):
                module.validate_qualification_metrics(
                    broken, qualification, "sod", scientific=True)

    def test_long_scientific_route_requires_matching_minimum_steps(self):
        module = load_module()
        case = {"id": "periodic", "minimum_scientific_steps": 1000}
        module.validate_scientific_steps(
            {"steps": 1012}, {"steps": 1012}, case)
        with self.assertRaisesRegex(RuntimeError, "accepted-step"):
            module.validate_scientific_steps(
                {"steps": 1012}, {"steps": 1011}, case)
        with self.assertRaisesRegex(RuntimeError, "minimum"):
            module.validate_scientific_steps(
                {"steps": 999}, {"steps": 999}, case)


if __name__ == "__main__":
    unittest.main()
