"""Checks independent closed-form identities, including known wrong alternatives."""
import unittest
from fractions import Fraction as Q

from analytic_reference import (add, axis_equilibrium, external_source, multiply,
                               poly, scale, swirl_stress, value_at)


class RzAnalyticReferenceTests(unittest.TestCase):
    def test_native_measures_are_distinct(self):
        r = external_source(1, 3, 2, 2, (-Q(1, 40),), (0, 2), Q(1, 10000))
        self.assertEqual(r["volume_per_2pi"], 8)
        self.assertEqual(r["angular_measure_per_2pi"], Q(52, 3))
        self.assertEqual(r["m_phi_mean_W"], Q(60, 13))
        self.assertEqual(r["m_phi_mean_V"], Q(13, 3))

    def test_external_work_rejects_W_as_V_shortcut(self):
        dt, g = Q(1, 10000), -Q(1, 40)
        r = external_source(1, 3, 2, 2, (g,), (0, 2), dt)
        self.assertEqual(r["delta_J_per_2pi"], -Q(13, 150000))
        self.assertEqual(r["delta_E_total_per_2pi"], -Q(13, 150000))
        self.assertNotEqual(r["delta_E_total_per_2pi"],
                            dt * r["m_phi_mean_W"] * g * r["volume_per_2pi"])
        self.assertEqual(r["delta_m_phi_W"], dt * 2 * g)

    def test_regular_axis_external_source(self):
        r = external_source(0, 1, 1, 1, (0, Q(1, 40)), (0, 1), Q(1, 10000))
        self.assertEqual(r["delta_J_per_2pi"], Q(1, 1600000))
        self.assertEqual(r["delta_E_total_per_2pi"], Q(1, 1600000))
        with self.assertRaises(ValueError):
            external_source(0, 1, 1, 1, (Q(1, 40),), (0, 1), 1)

    def test_rigid_rotation_zero_even_with_variable_mu(self):
        r = swirl_stress((1, 2, 3), omega=3)
        for key in ("strain", "tau_rphi", "force_phi", "energy_work_divergence", "heating"):
            self.assertEqual(r[key], (Q(0),))
        # Old variable-mu vector-Laplacian yields Omega*mu' != 0.
        self.assertNotEqual(3 * (2 + 6 * Q(3, 2)), 0)

    def test_constant_mu_cubic_closed_form(self):
        r = swirl_stress((2,), omega=3, cubic=Q(1, 4))
        radius = Q(3, 2)
        self.assertEqual(value_at(r["tau_rphi"], radius), radius ** 2)
        self.assertEqual(value_at(r["force_phi"], radius), 4 * radius)
        self.assertEqual(value_at(r["heating"], radius), radius ** 4 / 2)

    def test_variable_mu_work_and_dissipation_identity(self):
        for mu in ((2,), (1, 2), (1, 0, 3)):
            r = swirl_stress(mu, omega=3, cubic=Q(1, 4))
            rhs = add(multiply(r["velocity"], r["force_phi"]), r["heating"])
            self.assertEqual(r["energy_work_divergence"], rhs)
            for radius in (Q(0), Q(1, 4), Q(3, 2), Q(2)):
                self.assertGreaterEqual(value_at(r["heating"], radius), 0)
                self.assertEqual(value_at(r["force_phi"], radius),
                    Q(1, 2) * sum((i + 4) * Q(c) * radius ** (i + 1)
                                  for i, c in enumerate(mu)))

    def test_documented_axis_cell_integrals(self):
        h = Q(1, 16)
        r = axis_equilibrium(h)
        self.assertEqual(r["kinetic_mean_V"], h * h / 4)
        self.assertEqual(r["kinetic_representative"], 9 * h * h / 32)
        self.assertEqual(r["pressure_representative"], 5 + 19 * h * h / 80)
        self.assertEqual(r["source_exact_mean_V"], 10 / h + h)
        self.assertEqual(r["source_exact_mean_V"], r["pressure_divergence"])
        self.assertEqual(r["source_representative"], 10 / h + 8 * h / 5)
        self.assertEqual(r["source_only_defect"], 3 * h / 5)

    def test_axis_source_only_defect_is_first_order(self):
        residuals = [axis_equilibrium(Q(1, n))["source_only_defect"]
                     for n in (16, 32, 64, 128)]
        self.assertTrue(all(a == 2 * b for a, b in zip(residuals, residuals[1:])))
        # Exact balance is zero; this historical defect must not become expected PASS.
        self.assertTrue(all(x > 0 for x in residuals))

    def test_general_swirl_balance_is_not_omega_only(self):
        for omega in (1, 4):
            for n in (16, 32, 64, 128):
                r = axis_equilibrium(Q(1, n), omega=omega, cubic=Q(1, 4))
                self.assertEqual(r["source_exact_mean_V"], r["pressure_divergence"])
                self.assertGreater(r["kinetic_representative"], r["kinetic_mean_V"])

    def test_invalid_domains_and_scales_are_rejected(self):
        for args in ((1, 1, 1, 1), (-1, 1, 1, 1), (0, 1, 0, 1), (0, 1, 1, 0)):
            with self.assertRaises(ValueError):
                external_source(*args, (0,), (0, 1), 1)
        with self.assertRaises(ValueError):
            axis_equilibrium(0)


if __name__ == "__main__":
    unittest.main()
