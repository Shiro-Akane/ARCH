"""Input/schema checks only: no ARCH execution or scientific validation claim."""

from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import validate_backend_results as validator
sys.path.insert(0, str(ROOT / 'validation/amr'))
import geometry_reference
import gaussian_reference


class ViscousEvidenceTests(unittest.TestCase):
    def test_thermal_activity_rejects_flags_without_a_resolved_state_change(self):
        self.assertGreater(gaussian_reference.check_activity(1e-6), 0)
        for change in (0, -1, 1e-16, float('nan'), float('inf')):
            with self.subTest(change=change), self.assertRaises(ArithmeticError):
                gaussian_reference.check_activity(change)

    def transcript(self, backend='cpu', error=None):
        return '\n'.join(
            f'VISCOUS_SPATIAL_CONVERGENCE backend={backend} geometry={geometry} '
            f'dim={dimension} uniform={uniform} density_slope={slope} h={spacing} '
            f'error={error if error is not None else spacing*spacing*.01}'
            for geometry in ('cartesian', 'cylindrical', 'spherical')
            for dimension in (1, 2, 3) for slope in (0., .1)
            for uniform in ((0,) if dimension == 1 else (0, 1))
            for spacing in (.05, .025, .0125))

    def test_complete_independent_spatial_matrix(self):
        for backend in ('cpu', 'cuda'):
            summary = geometry_reference.viscous_transcript(self.transcript(backend), backend)
            self.assertEqual(summary['samples'], 90)
            self.assertEqual(summary['cases'], 30)

    def test_missing_duplicate_wrong_backend_or_failed_values_are_rejected(self):
        complete = self.transcript()
        for text in ('', '\n'.join(complete.splitlines()[1:]),
                     complete+'\n'+complete.splitlines()[0], self.transcript('cuda'),
                     self.transcript(error='nan'), self.transcript(error='-1'),
                     self.transcript(error='.01'), self.transcript(error='1e-5')):
            with self.subTest(text=text[:80]), self.assertRaises(ValueError):
                geometry_reference.viscous_transcript(text, 'cpu')

    def test_origin_balance_and_contraction_coverage(self):
        lines = []
        for geometry in ('cylindrical', 'spherical'):
            for h in (.025, .0125, .00625):
                for dimension in (1, 2, 3):
                    lines.append(f'VISCOUS_ORIGIN backend=cpu geometry={geometry} '
                                 f'dim={dimension} h={h} error=1e-14')
                lines.append(f'VISCOUS_RADIAL_STABILITY backend=cpu geometry={geometry} '
                             f'h={h} minimum_entry=0 maximum_row_sum=.99')
        for geometry in ('cartesian', 'cylindrical', 'spherical'):
            for contrast in (1, 10, 100):
                lines.append(f'VISCOUS_DENSITY_STABILITY backend=cpu geometry={geometry} '
                             f'contrast={contrast} minimum_entry=0 maximum_row_sum=.99')
        text = '\n'.join(lines)
        result = geometry_reference.origin_transcript(text, 'cpu')
        self.assertEqual(result['origin_samples'], 18)
        self.assertEqual(result['contraction_matrices'], 6)
        self.assertEqual(result['density_contraction_matrices'], 9)
        for invalid in ('', '\n'.join(lines[1:]), text+'\n'+lines[0],
                        text.replace('backend=cpu', 'backend=cuda'),
                        text.replace('error=1e-14', 'error=nan'),
                        text.replace('error=1e-14', 'error=-1'),
                        text.replace('error=1e-14', 'error=1e-3'),
                        text.replace('minimum_entry=0', 'minimum_entry=-.01'),
                        text.replace('maximum_row_sum=.99', 'maximum_row_sum=1.01')):
            with self.subTest(text=invalid[:70]), self.assertRaises(ValueError):
                geometry_reference.origin_transcript(invalid, 'cpu')

    def test_scalar_thermal_and_species_refinement_coverage(self):
        lines = [f'{tag} geometry={geometry} dim={dim} h={h} '
                 f'host_error={h*h*.0001} device_error={h*h*.0001}'
                 for tag in ('DIFFUSION_SPATIAL_CONVERGENCE', 'THERMAL_SPATIAL_CONVERGENCE')
                 for geometry in ('cartesian', 'cylindrical', 'spherical')
                 for dim in (1, 2, 3) for h in (.05, .025, .0125)]
        text = '\n'.join(lines)
        result = geometry_reference.scalar_transcript(text)
        self.assertEqual(result['samples'], 54)
        for invalid in ('', '\n'.join(lines[1:]), text+'\n'+lines[0],
                        text.replace('THERMAL_SPATIAL_CONVERGENCE', 'unknown'),
                        text.replace('host_error=', 'host_error=nan #='),
                        text.replace('device_error=', 'device_error=-1 #=')):
            with self.subTest(text=invalid[:70]), self.assertRaises(ValueError):
                geometry_reference.scalar_transcript(invalid)


class CurvilinearManifestTests(unittest.TestCase):
    def setUp(self):
        self.manifest = validator.load_manifest(
            ROOT / "validation/amr/gpu_curvilinear_cases.json")

    def test_all_dimensions_keep_existing_tolerance_budgets(self):
        previous = validator.load_manifest(ROOT / "validation/amr/gpu_cases.json")
        references = {case["id"]: case for case in previous["cases"]}
        self.assertEqual(len(self.manifest["cases"]), 24)
        coverage = set()
        for case in self.manifest["cases"]:
            overrides = case["overrides"]
            method = overrides["diff_integrator"]
            dimension = 3 if int(overrides["nblockx3"]) else (2 if int(overrides["nblockx2"]) else 1)
            coupled = "_coupled_" in case["id"]
            coverage.add((overrides["geometry"], dimension, method, coupled))
            reference = references[f"diffusion_amr_{method.lower()}_5stage"]
            self.assertEqual(case["reduction_policy"], reference["reduction_policy"])
            self.assertEqual(case["rkl_policy"], reference["rkl_policy"])
            self.assertEqual(case["topology_policy"], reference["topology_policy"])
            for key in ("rtol", "atol"):
                self.assertEqual(case["conservation_policy"][key],
                                 reference["conservation_policy"][key])
            self.assertEqual(case["conservation_policy"]["measure"], "physical_cell_volume")
            self.assertNotIn("measure", reference["conservation_policy"])
            self.assertEqual(case["conservation_policy"]["fields"], ["mass", "energy", "rhoX"])
            self.assertEqual(case["accepted_steps"], [2, 5])
            self.assertNotIn(case["id"], references)
        self.assertEqual(coverage, {
            (geometry, dimension, method, coupled)
            for geometry in ("cylindrical", "spherical")
            for dimension in (1, 2, 3)
            for method in ("RKL1", "RKL2")
            for coupled in (False, True)})

    def test_cpu_cuda_render_one_canonical_input_without_changing_science(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            for case in self.manifest["cases"]:
                source = ROOT / case["input"]
                self.assertEqual(source, ROOT / "validation/amr/inputs/gaussian_diffusion_amr.par")
                original = source.read_bytes()
                rendered = {}
                for backend in ("cpu", "cuda"):
                    parameter = output / f"{case['id']}_{backend}.par"
                    validator.render_parameter_file(
                        source, parameter, backend=backend,
                        output_dir=output / backend, base_name=backend,
                        accepted_steps=2, scientific_overrides=case["overrides"])
                    values = validator.read_parameter_map(parameter)
                    self.assertEqual(values.pop("compute_backend"), backend)
                    values.pop("out_dir")
                    values.pop("base_name")
                    rendered[backend] = values
                self.assertEqual(rendered["cpu"], rendered["cuda"])
                self.assertEqual(source.read_bytes(), original)
                values = rendered["cpu"]
                coupled = "_coupled_" in case["id"]
                for key, expected in {
                    "nblockx1": "4" if case["id"].endswith("_1d") else "8",
                    "x1_min": "1.0", "x1_max": "2.0" if case["id"].endswith("_1d") else "3.0",
                    "xc": "1.5",
                    "x1l_boundary_type": "reflecting", "x1r_boundary_type": "reflecting",
                    "lrefinemin": "0", "lrefinemax": "1", "regrid_interval": "1",
                    "use_diffusion": "true", "use_species_diff": "true",
                    "use_thermal_diff": "true" if coupled else "false",
                    "use_viscous_diff": "true" if coupled else "false",
                    "use_burn": "false", "eos_type": "ideal",
                    "network_name": "none", "gas_cv": "717.5",
                    "refine_var": "SPECIES", "refine_threshold": "0.7",
                    "derefine_threshold": "0.15", "width": "0.08", "amp": "0.5",
                }.items():
                    self.assertEqual(values[key], expected)
                if coupled:
                    for key in ("alpha_therm", "nu_visc", "pressure_amplitude", "u_amplitude"):
                        self.assertGreater(float(values[key]), 0.0)
                    if not case["id"].endswith("_1d"):
                        self.assertNotEqual(float(values["v_amplitude"]), 0.0)
                    if case["id"].endswith("_3d"):
                        self.assertNotEqual(float(values["w_amplitude"]), 0.0)
                if case["id"].endswith("_1d"):
                    self.assertEqual(values["nblockx2"], "0")
                    self.assertEqual(values["nblockx3"], "0")
                    self.assertEqual(values["max_blocks"], "32")
                else:
                    self.assertEqual(values["max_blocks"], "512" if case["id"].endswith("_3d") else "128")
                    self.assertEqual(values["nblockx2"], "2")
                    self.assertEqual(values["nblockx3"], "2" if case["id"].endswith("_3d") else "0")
                    for direction in (1, 2, 3):
                        for side in ("l", "r"):
                            self.assertEqual(values[f"x{direction}{side}_boundary_type"], "reflecting")

    def test_radial_momentum_not_an_invariant_but_mass_drift_is_rejected(self):
        for case in self.manifest["cases"]:
            before = {
                "mass": 1.0, "energy": 2.5, "rhoX": [0.8, 0.2], "mom_u": 0.0,
                "measure": "physical_cell_volume", "geometry": case["overrides"]["geometry"],
                "parameter_sha256": "0" * 64,
            }
            after = {**before, "mom_u": 0.01}
            policy = case["conservation_policy"]
            validator.validate_conservation_metrics(before, after, policy)
            with self.assertRaisesRegex(RuntimeError, "conservation drift field=mass"):
                validator.validate_conservation_metrics(before, {**after, "mass": 1.001}, policy)


if __name__ == "__main__":
    unittest.main()
