import importlib.util
import json
from pathlib import Path
import tempfile
import sys
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "validate_backend_results.py"


def load_module():
    spec = importlib.util.spec_from_file_location(
        "validate_backend_results", MODULE_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load backend validator module")
    module = importlib.util.module_from_spec(spec)
    with mock.patch.object(sys, "path", [str(MODULE_PATH.parent), *sys.path]):
        spec.loader.exec_module(module)
    return module


class BackendValidationTests(unittest.TestCase):
    def test_hydro_temporal_reference_and_order_negative_controls(self):
        import math
        spec = importlib.util.spec_from_file_location('hydro_time_reference', ROOT / 'validation/hydro/time_reference.py')
        oracle = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(oracle)
        self.assertEqual(oracle.fourier_evolution(0., .1, 2 * math.pi, .3), (1., 0.))
        forward = oracle.fourier_evolution(1., .1, 2 * math.pi, .3)
        backward = oracle.fourier_evolution(-1., .1, 2 * math.pi, .3)
        self.assertEqual(forward[0], backward[0])
        self.assertEqual(forward[1], -backward[1])
        self.assertLess(forward[0], 1.)
        for order in (1, 2, 3):
            self.assertEqual(oracle.temporal_orders([2.**(-order*k) for k in range(3)],
                                                    [.4, .2, .1], order-.1), [float(order)]*2)
        for errors in ([1., .6, .4], [1., 0., .1], [1., float('nan'), .1], [1., .1]):
            with self.assertRaises(ArithmeticError):
                oracle.temporal_orders(errors, [.4, .2, .1], 1.8)
        for cfls in ([.4, .2, .2], [.4, .2, -.1], [.4, .2, float('inf')]):
            with self.assertRaises(ValueError):
                oracle.temporal_orders([1., .25, .0625], cfls, 1.8)

    def test_external_gravity_uses_unchanged_analytic_budget(self):
        module = load_module()
        policy = {'reference': 'constant_external_acceleration', 'linf_max': 1e-12}
        self.assertEqual(module.qualification_mode(policy), 'gravity')
        metrics = dict(min_rho=1., min_eng=2.5, rho_linf=0., velocity_linf=0.,
                       pressure_linf=1e-15, energy_linf=1e-15)
        module.validate_qualification_metrics(metrics, policy, 'gravity')
        for field in ('rho', 'velocity', 'pressure', 'energy'):
            for error in (-1., 1e-11, float('nan'), float('inf')):
                with self.subTest(field=field, error=error), self.assertRaises(RuntimeError):
                    module.validate_qualification_metrics(dict(metrics, **{field + '_linf': error}), policy, 'gravity')
        with self.assertRaisesRegex(RuntimeError, 'actual run parameters'):
            module.qualify_checkpoint(Path('validator'), Path('checkpoint'), {'qualification': policy})

    def test_whole_regrid_costs_are_checked_without_double_counting(self):
        module = load_module()
        row = dict(macro_step=0, physical_time=0.0, backend='cuda', old_blocks=1, new_blocks=2,
                   topology_changed=1, wall_seconds=0.1, bytes_h2d=24, bytes_d2h=12,
                   kernel_count=3, stream_sync_count=2)
        rows = [row, dict(row, macro_step=1, physical_time=0.2, old_blocks=2)]
        summary = module.summarize_regrids(rows, 'cuda', 2)
        self.assertEqual(summary['bytes_h2d'], 48)
        self.assertEqual(summary['topology_changes'], 2)
        self.assertTrue(summary['overlaps_backend_trace'])
        for key, value in [('backend', 'cpu'), ('wall_seconds', float('nan')),
                           ('bytes_d2h', -1), ('kernel_count', 0.5), ('topology_changed', 0),
                           ('macro_step', 2), ('old_blocks', 0)]:
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                module.summarize_regrids([dict(row, **{key: value})], 'cuda', 2)
        with self.assertRaises(RuntimeError):
            module.summarize_regrids([row, row], 'cuda', 2)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'regrid.tsv'
            path.write_text('\t'.join(row) + '\n' + '\t'.join(str(value) for value in row.values()) + '\n')
            result = module.read_regrid_metrics(path, 'cuda', 2)
            self.assertEqual(result['records'], [row])
            self.assertEqual(len(result['file']['sha256']), 64)
            path.write_text(path.read_text() + 'incomplete\n')
            with self.assertRaises(RuntimeError):
                module.read_regrid_metrics(path, 'cuda', 2)

    def test_terminal_parity_without_oracle_still_checks_actual_metadata(self):
        module = load_module()
        case = {"id": "terminal", "accepted_steps": [], "scientific_time": 0.1,
                "reduction_policy": {"rtol": 2e-10, "atol": 2e-12}}
        def lane(*args):
            backend = args[3]
            return {"steps": 4, "checkpoint": Path(backend + ".h5"),
                    "parameter_file": Path(backend + ".par"), "parameter_sha256": "a" * 64}
        with mock.patch.object(module, "run_arch_terminal_lane", side_effect=lane), \
             mock.patch.object(module, "compare_hdf5_checkpoints", return_value={"passed": True}), \
             mock.patch.object(module, "checkpoint_metadata") as metadata:
            for time in (0.1, 0.09, float("nan"), float("inf")):
                metadata.return_value = {"time": time, "parameter_sha256": "a" * 64}
                if time == 0.1:
                    result = module.run_case(Path("arch"), Path("validator"),
                                             ROOT, case, Path("output"))
                    self.assertEqual(result["scientific"]["cpu_qualification"],
                                     {"status": "not-requested"})
                    self.assertEqual(metadata.call_count, 2)
                    self.assertEqual(metadata.call_args.kwargs["expected_steps"], 4)
                    self.assertEqual(metadata.call_args.kwargs["parameters"], Path("cuda.par"))
                    self.assertEqual(result["scientific"]["cuda"]["checkpoint_metadata"]["time"], 0.1)
                else:
                    with self.assertRaisesRegex(RuntimeError, "terminal time drifted"):
                        module.run_case(Path("arch"), Path("validator"), ROOT, case, Path("output"))
            metadata.return_value = {"time": 0.1, "parameter_sha256": "b" * 64}
            with self.assertRaisesRegex(RuntimeError, "parameter identity drifted"):
                module.run_case(Path("arch"), Path("validator"), ROOT, case, Path("output"))

    def test_physical_metrics_pass_actual_parameters_and_hash(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as directory:
            parameter = Path(directory) / "actual.par"
            parameter.write_text("geometry = cylindrical\nx1_min = 1\nx1_max = 2\n")
            completed = mock.Mock(returncode=0, stderr="", stdout=json.dumps({
                "measure": "physical_cell_volume", "geometry": "cylindrical", "mass": 1.5}))
            with mock.patch.object(module.subprocess, "run", return_value=completed) as run:
                result = module.read_conservation_metrics(Path("validator"), Path("checkpoint.h5"), parameter)
            self.assertEqual(run.call_args.args[0][-2:], ["--parameters", str(parameter)])
            self.assertEqual(result["parameter_sha256"], module._sha256(parameter))

    def test_physical_metrics_require_parameter_bound_measure(self):
        module = load_module()
        with self.assertRaisesRegex(RuntimeError, "actual run parameter identity"):
            module.validate_conservation(Path("validator"), Path("before"), Path("after"),
                                         {"measure": "physical_cell_volume"})
        with tempfile.TemporaryDirectory() as directory:
            parameter = Path(directory) / "actual.par"
            parameter.write_text("geometry = spherical\n")
            completed = mock.Mock(returncode=0, stderr="", stdout='{"mass": 1}')
            with mock.patch.object(module.subprocess, "run", return_value=completed), \
                    self.assertRaisesRegex(RuntimeError, "did not provide physical"):
                module.read_conservation_metrics(Path("validator"), Path("checkpoint.h5"), parameter)

    def test_parameter_mutation_during_metrics_is_rejected(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as directory:
            parameter = Path(directory) / "actual.par"
            parameter.write_text("geometry = cylindrical\nx1_min = 1\n")
            def mutate(*args, **kwargs):
                parameter.write_text("geometry = cylindrical\nx1_min = 2\n")
                return mock.Mock(returncode=0, stderr="", stdout='{"measure":"physical_cell_volume"}')
            with mock.patch.object(module.subprocess, "run", side_effect=mutate), \
                    self.assertRaisesRegex(RuntimeError, "parameter file changed"):
                module.read_conservation_metrics(Path("validator"), Path("checkpoint.h5"), parameter)

    def test_legacy_measure_preserves_cli_and_absolute_budget_units(self):
        module = load_module()
        metrics = {"mass": 80.0}
        completed = mock.Mock(returncode=0, stderr="", stdout=json.dumps(metrics))
        with mock.patch.object(module.subprocess, "run", return_value=completed) as run:
            result = module.validate_conservation(Path("validator"), Path("before"), Path("after"),
                {"fields": ["mass"], "rtol": 0, "atol": 1e-11},
                parameter_file=Path("not-used.par"), parameter_sha256="not-used")
        self.assertEqual(run.call_args.args[0], ["validator", "--metrics", "after"])
        self.assertEqual(result["before"]["mass"], 80.0)
        self.assertEqual(result["before"]["measure"], "legacy_level_normalized")
        with self.assertRaisesRegex(RuntimeError, "measure differs"):
            module.validate_conservation_metrics(metrics, metrics,
                {"measure": "physical_cell_volume", "fields": ["mass"]})

    def test_physical_measure_identity_is_checked_without_relaxing_budget(self):
        module = load_module()
        metrics = {"mass": 1.5, "measure": "physical_cell_volume", "geometry": "cylindrical",
                   "parameter_sha256": "a" * 64}
        policy = {"measure": "physical_cell_volume", "fields": ["mass"], "atol": 1e-11}
        module.validate_conservation_metrics(metrics, metrics, policy)
        for key in ("geometry", "parameter_sha256"):
            invalid = dict(metrics)
            invalid.pop(key)
            with self.assertRaisesRegex(RuntimeError, "geometry/parameter identity"):
                module.validate_conservation_metrics(invalid, metrics, policy)
        with self.assertRaisesRegex(RuntimeError, "conservation drift"):
            module.validate_conservation_metrics(metrics, {**metrics, "mass": 1.500001}, policy)

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

    def test_resolved_provider_and_physics_are_checked_not_only_backend(self):
        module = load_module()
        policy = {"network": "custom:audit31", "eos": "helmholtz", "ode": "bd",
                  "linear": {"cpu": "sparseklu", "cuda": "cudss"}}
        for backend, linear in (("cpu", "sparseklu"), ("cuda", "cudss")):
            plan = {"requested": backend, "resolved": backend,
                    "network": "custom:audit31", "eos": "helmholtz", "ode": "bd",
                    "linear": linear}
            module.validate_plan_values(plan, backend, policy)
            for field in policy:
                with self.subTest(backend=backend, field=field):
                    for bad in ({**plan, field: "none"},
                                {key: value for key, value in plan.items() if key != field}):
                        with self.assertRaisesRegex(RuntimeError, "resolved policy mismatch"):
                            module.validate_plan_values(bad, backend, policy)
            for malformed in ({"linear": {"cuda": "cudss"}}, {"linear": 1}, {"ode": ""}):
                with self.assertRaises(RuntimeError):
                    module.validate_plan_values(plan, backend, malformed)

    def test_application_network_matrix_has_all_methods_and_strict_auto_routes(self):
        module = load_module()
        manifest = module.load_manifest(ROOT / "validation/network/runtime_cases.json")
        coverage = set()
        for case in manifest["cases"]:
            policy = case["plan_policy"]
            coverage.add((policy["network"], policy["ode"]))
            self.assertEqual(case["overrides"]["linear_solver"], "Auto")
            self.assertEqual(case["overrides"]["network_name"], policy["network"])
            self.assertEqual(case["overrides"]["ode_solver"].lower(), policy["ode"])
            self.assertEqual(policy["eos"], "helmholtz")
            self.assertEqual(policy["linear"],
                             {"cpu": "sparseklu", "cuda": "cudss"}
                             if policy["network"] == "custom:audit31" else "denselu")
        self.assertEqual(coverage, {(network, ode)
                                   for network in ("custom:audit31", "custom:weak_urca")
                                   for ode in ("be_nr", "bd", "ros4")})

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

    def test_hdf_comparator_propagates_enuc_peak_tolerance(self):
        module = load_module()
        completed = mock.Mock(
            returncode=0,
            stdout='{"status":"pass","max_enuc_normalized":0.0004}\n',
            stderr="")
        with mock.patch.object(
            module.subprocess, "run", return_value=completed
        ) as run:
            result = module.compare_hdf5_checkpoints(
                Path("reference.h5"), Path("candidate.h5"),
                {"rtol": 5e-9, "atol": 5e-12,
                 "enuc_scale_rtol": 1e-3,
                 "dt_burn_rtol": 2e-3}, Path("validator"))
        self.assertTrue(result["passed"])
        self.assertEqual(run.call_args.args[0][-2:], ["0.001", "0.002"])

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
                "scientific_time": 0.2,
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

    def test_final_pair_spatial_gate_keeps_original_scope_and_time(self):
        module = load_module()
        cases = [dict(id=str(n), qualification_group='smooth', resolution=n, scientific_time=0.1,
                      qualification=dict(minimum_l1_order=1.8, convergence_pairs='final'))
                 for n in (64, 128, 256)]
        results = [dict(id=str(n), scientific={backend + '_qualification': dict(l1=error, l2=error)
                                              for backend in ('cpu', 'cuda')})
                   for n, error in zip((64, 128, 256), (1.0, 0.9, 0.2))]
        summary = module.validate_resolution_groups(cases, results)
        self.assertLess(summary['smooth']['orders']['cuda']['l1'][0], 1.8)
        self.assertGreater(summary['smooth']['orders']['cuda']['l1'][1], 1.8)
        for key, value in (('scientific_time', 0.2), ('scientific_time', float('nan')),
                           ('qualification', dict(minimum_l1_order=1.7, convergence_pairs='final')),
                           ('qualification', dict(minimum_l1_order=1.8, convergence_pairs='all'))):
            broken = json.loads(json.dumps(cases))
            broken[-1][key] = value
            with self.subTest(key=key, value=value), self.assertRaises(RuntimeError):
                module.validate_resolution_groups(broken, results)
        results[-1]['scientific']['cuda_qualification']['l1'] = 0.8
        with self.assertRaisesRegex(RuntimeError, 'L1 convergence'):
            module.validate_resolution_groups(cases, results)

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

    def test_scientific_route_requires_each_lane_minimum_not_equal_steps(self):
        module = load_module()
        case = {"id": "periodic", "minimum_scientific_steps": 1000}
        module.validate_scientific_steps(
            {"steps": 1012}, {"steps": 1012}, case)
        module.validate_scientific_steps({"steps": 1012}, {"steps": 1011}, case)
        for invalid in (999, 0, -1, 1001.5, True):
            for left, right in ((invalid, 1012), (1012, invalid)):
                with self.assertRaisesRegex(RuntimeError, "minimum"):
                    module.validate_scientific_steps({"steps": left}, {"steps": right}, case)

    def test_step_diagnostics_cannot_replace_physical_time_acceptance(self):
        module = load_module()
        self.assertEqual(module.checkpoint_comparison_mode({}), "reproducibility")
        for target in (None, 0, -1, float("nan"), float("inf")):
            case = {"checkpoint_comparison": "step-diagnostic"}
            if target is not None:
                case["scientific_time"] = target
            with self.assertRaisesRegex(RuntimeError, "prescribed physical-time"):
                module.checkpoint_comparison_mode(case)
        self.assertEqual(module.checkpoint_comparison_mode({
            "checkpoint_comparison": "step-diagnostic", "scientific_time": 0.01}), "step-diagnostic")

    def test_physical_time_command_does_not_reuse_field_tolerance_or_restart_mode(self):
        module = load_module()
        arguments = (Path("a.h5"), Path("b.h5"), {"rtol": 2e-8, "atol": 1e-12}, Path("validator"))
        completed = mock.Mock(returncode=0, stderr="", stdout='{"status":"pass"}')
        with mock.patch.object(module.subprocess, "run", return_value=completed) as run:
            module.compare_hdf5_checkpoints(*arguments, comparison_mode="physical-time", target_time=0.01)
            self.assertEqual(run.call_args.args[0], ["validator", "--compare-physical-time",
                "a.h5", "b.h5", "2e-08", "1e-12", "2e-08", "2e-08", "0.01"])
            with self.assertRaisesRegex(RuntimeError, "operation"):
                module.compare_hdf5_checkpoints(*arguments, comparison_mode="physical-time", target_time=0.01,
                                                terminal_source_pair=(Path("a"), Path("b")))
            with self.assertRaisesRegex(RuntimeError, "prescribed time"):
                module.compare_hdf5_checkpoints(*arguments, comparison_mode="physical-time")

    def test_topology_transition_requires_real_parent_child_changes(self):
        module = load_module()
        snapshots = [
            {"dimension": 1, "topology": [[0, 0, 0, 0], [0, 1, 0, 0]]},
            {"dimension": 1,
             "topology": [[1, 0, 0, 0], [1, 1, 0, 0], [0, 1, 0, 0]]},
            {"dimension": 1, "topology": [[0, 0, 0, 0], [0, 1, 0, 0]]},
        ]
        result = module.validate_topology_transitions(
            snapshots, require_refine=True, require_derefine=True)
        self.assertEqual(result, {"refined": True, "derefined": True})
        with self.assertRaisesRegex(RuntimeError, "derefine"):
            module.validate_topology_transitions(
                snapshots[:2], require_refine=True, require_derefine=True)

    def test_cuda_diffusion_schedule_proves_rkl2_cache_lifetime(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as directory:
            schedule = Path(directory) / "schedule.tsv"
            schedule.write_text(
                "macro_step\tcache_generation\torder\tstages\t"
                "negative_gamma_stages\tcaptures_initial_operator\t"
                "diffusion_dt\tdt_forward_euler\n"
                "0\t1\t2\t3\t2\t1\t0.1\t0.01\n"
                "0\t2\t2\t3\t2\t1\t0.1\t0.01\n",
                encoding="utf-8")
            result = module.validate_cuda_diffusion_schedule(
                schedule, 1,
                {"order": 2, "stages": 3, "lanes_per_step": 2})
            self.assertEqual(result["cache_generations"], [1, 2])
            schedule.write_text(
                schedule.read_text(encoding="utf-8").replace(
                    "0\t2\t2\t3", "0\t1\t2\t3"),
                encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "generation"):
                module.validate_cuda_diffusion_schedule(
                    schedule, 1,
                    {"order": 2, "stages": 3, "lanes_per_step": 2})

            schedule.write_text(
                "macro_step\tcache_generation\torder\tstages\t"
                "negative_gamma_stages\tcaptures_initial_operator\t"
                "diffusion_dt\tdt_forward_euler\n"
                "0\t1\t2\t3\t2\t1\t0.1\t0.01\n"
                "1\t2\t2\t3\t2\t1\t0.1\t0.01\n",
                encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "macro-step"):
                module.validate_cuda_diffusion_schedule(
                    schedule, 1,
                    {"order": 2, "stages": 3, "lanes_per_step": 2})


if __name__ == "__main__":
    unittest.main()
