"""Checks independent closed-form identities, including known wrong alternatives."""
import unittest
from fractions import Fraction as Q

from decimal import Decimal

from analytic_reference import (force_difference_interval, newton_force_modulus, add, axis_equilibrium, external_source, multiply,
                               poly, rotating_native_mean, scale, swirl_stress, value_at)


class RzAnalyticReferenceTests(unittest.TestCase):
    def test_mixed_measure_proxy_can_reject_a_positive_rotating_field(self):
        first = rotating_native_mean(0, 1)
        self.assertEqual(first["volume_per_2pi"], Q(1, 2))
        self.assertEqual(first["angular_measure_per_2pi"], Q(1, 3))
        self.assertEqual(first["inertia_per_2pi"], Q(1, 4))
        self.assertEqual(first["m_phi_mean_W"], Q(3, 4))
        self.assertEqual(first["energy_mean_V"], Q(17, 64))
        self.assertEqual(first["actual_specific_internal"], Q(1, 64))
        self.assertEqual(first["generic_proxy_specific_internal"], -Q(1, 64))
        for left, right in ((0, 1), (1, 2), (2, 3), (Q(1, 10), Q(11, 10))):
            for rho in (Q(1, 10), Q(1), Q(7)):
                with self.subTest(left=left, right=right, rho=rho):
                    native = rotating_native_mean(left, right, rho, omega=-3)
                    self.assertEqual(native["actual_kinetic_mean_V"],
                                     native["minimum_angular_kinetic_mean_V"])
                    proxy_energy = (native["m_phi_mean_W"] ** 2 / (2 * rho))
                    self.assertGreater(proxy_energy, native["actual_kinetic_mean_V"])
        with self.assertRaises(ValueError):
            rotating_native_mean(0, 1, rho=0)
        with self.assertRaises(ValueError):
            rotating_native_mean(0, 1, internal=0)

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

    def test_force_interval_sign_and_contact(self):
        # Independent Machin alternating-series rational enclosure for pi.
        def atan_interval(q, count):
            total = sum((Q((-1)**i, (2*i+1)*q**(2*i+1))
                         for i in range(count)), Q(0))
            omitted = Q((-1)**count, (2*count+1)*q**(2*count+1))
            return min(total,total+omitted), max(total,total+omitted)
        al, au = atan_interval(5, 56)
        bl, bu = atan_interval(239, 16)
        pi_bounds = (16*al-4*bu, 16*au-4*bl)
        from decimal import ROUND_FLOOR, ROUND_CEILING
        from analytic_reference import _decimal_rational
        def ball_phi(x):
            x = abs(x)
            coefficient = -2*(4-x*x/Q(3)) if x <= 2 else -Q(32,3)/x
            return tuple(_decimal_rational(coefficient*pi_bounds[1-i], 80,
                         ROUND_FLOOR if i == 0 else ROUND_CEILING) for i in (0,1))
        for x in (Q(0),Q(1,10),Q(1),Q(2),Q(3),Q(-2)):
            for hs in (Q(1,1000), Q(1,10), Q(3)):
                h = _decimal_rational(hs,80,ROUND_CEILING)
                lower, upper = force_difference_interval(ball_phi(x-hs),ball_phi(x+hs),
                    h,1,_decimal_rational(abs(x)+2,80,ROUND_CEILING),1)
                coefficient = -Q(4,3)*x if abs(x) <= 2 else -Q(32,3)*x/abs(x)**3
                exact = sorted(coefficient*p for p in pi_bounds)
                self.assertLessEqual(Q(lower),exact[0])
                self.assertGreaterEqual(Q(upper),exact[1])
        # A decreasing linear potential has positive g; this sign test uses
        # G=0 only to test enclosure arithmetic, not as a gravitational fixture.
        self.assertEqual(force_difference_interval((1,1),(-1,-1),1,0,0,0),
                         (Decimal(1),Decimal(1)))

    def test_force_interval_rejects_nonfinite_and_invalid_enclosures(self):
        for args in ((0,1,1,1),(-1,1,1,1),(1,-1,1,1),
                     (1,1,-1,1),(1,1,1,-1),(1,1,'NaN',1)):
            with self.assertRaises(ValueError):
                newton_force_modulus(*args)
        with self.assertRaises(ValueError):
            force_difference_interval((1,0),(0,1),1,1,1,1)
        with self.assertRaises(ValueError):
            force_difference_interval((0,'Infinity'),(0,1),1,1,1,1)
        self.assertEqual(newton_force_modulus(1,0,4,1),Decimal(0))
        self.assertEqual(newton_force_modulus(1,1,4,0),Decimal(0))

    def test_density_mean_does_not_determine_inertia(self):
        from analytic_reference import variable_density_rotation
        result = variable_density_rotation(0,1,internal=Q(1,1000))
        self.assertEqual(result["mass_per_2pi"],Q(1,2))
        self.assertEqual(result["inertia_per_2pi"],Q(25,96))
        self.assertEqual(result["J_per_2pi"],Q(25,96))
        self.assertEqual(result["constant_density_proxy_internal"],Q(1,1000)-Q(25,2304))
        self.assertGreater(result["actual_specific_internal"],0)
        self.assertLess(result["constant_density_proxy_internal"],0)
        # Same density mean and a distinct true inertia: no mean-only theorem.
        uniform = rotating_native_mean(0,1)
        self.assertNotEqual(result["inertia_per_2pi"],uniform["inertia_per_2pi"])

    def test_energy_ceiling_domain_is_not_convex(self):
        # Both physical point endpoints have e=1/2; midpoint has e=1.
        e = lambda momentum,energy: Q(energy)-Q(momentum)**2/2
        self.assertEqual(e(1,1),Q(1,2))
        self.assertEqual(e(-1,1),Q(1,2))
        self.assertGreater(e(0,1),Q(1,2))

    def test_contact_tube_force_bound_is_exact_and_scale_linear(self):
        from analytic_reference import newton_contact_tube_force_bound as bound
        d=Q(1,10**14)
        for r in (Q(0), d, 2*d, Q(1), Q(10**100)):
            self.assertEqual(bound(r,d),90*d)
            self.assertEqual(bound(r,d,3,2),540*d)
            self.assertEqual(bound(r,d,0,2),0)
            self.assertEqual(bound(r,d,3,0),0)
        for args in ((-1,d),(0,0),(0,-d),(0,d,-1),(0,d,1,-1)):
            with self.assertRaises(ValueError):
                bound(*args)

    def test_invalid_domains_and_scales_are_rejected(self):
        for args in ((1, 1, 1, 1), (-1, 1, 1, 1), (0, 1, 0, 1), (0, 1, 1, 0)):
            with self.assertRaises(ValueError):
                external_source(*args, (0,), (0, 1), 1)
        with self.assertRaises(ValueError):
            axis_equilibrium(0)


if __name__ == "__main__":
    unittest.main()
