"""Independent reference identities, not production scientific acceptance."""
import sys
import unittest
from pathlib import Path
from decimal import Decimal, localcontext
sys.path.insert(0, str(Path(__file__).resolve().parents[3]/"validation/gravity"))
import rz_ring_offaxis_reference as rz
from rz_ring_axis_reference import axis_reference

class OffAxisTests(unittest.TestCase):
    def case(self, **extra):
        return dict(rz.CASES[1], **extra)

    def test_agm_known_values_and_modulus_convention(self):
        with localcontext() as ctx:
            ctx.prec=80
            k,e=rz.elliptic_ke(Decimal(0))
            self.assertEqual(k, rz.PI/2)
            self.assertEqual(e,k)
            k,e=rz.elliptic_ke(Decimal(".5"))
            self.assertEqual(float(k),1.8540746773013719)
            self.assertEqual(float(e),1.3506438810476755)
            for bad in [Decimal(-1),Decimal(1)]:
                with self.assertRaises(ValueError):rz.elliptic_ke(bad)

    def test_gauss_polynomial_moments(self):
        with localcontext() as ctx:
            ctx.prec=60
            for n in [2,8,16]:
                nodes=rz.decimal_gauss(n,60)
                self.assertEqual(len(nodes),n)
                for power in range(2*n):
                    actual=sum(w*x**power for x,w in nodes)
                    exact=Decimal(2)/(power+1) if power%2==0 else Decimal(0)
                    self.assertLess(abs(actual-exact),Decimal("1e-50"))

    def test_axis_matches_closed_form(self):
        case=self.case(r_observer=0,z_observer=3)
        actual=rz.finite_volume_reference(case,32,80)
        expected=axis_reference(0,2,-1,1,3,3,80)
        for k,v in expected.items():
            if v:self.assertAlmostEqual(float(actual[k])/v,1,places=13)
            else:self.assertEqual(actual[k],0)

    def test_symmetry_density_translation_and_direction(self):
        a=rz.finite_volume_reference(self.case(),16,60)
        b=rz.finite_volume_reference(self.case(z_observer=-2),16,60)
        self.assertAlmostEqual(float(a["potential"]/b["potential"]),1,places=14)
        self.assertAlmostEqual(float(a["radial_acceleration"]/b["radial_acceleration"]),1,places=14)
        self.assertAlmostEqual(float(a["axial_acceleration"]/b["axial_acceleration"]),-1,places=14)
        self.assertLess(a["radial_acceleration"],0);self.assertLess(a["axial_acceleration"],0)
        translated=rz.finite_volume_reference(self.case(z_lower=4,z_upper=6,z_observer=7),16,60)
        doubled=rz.finite_volume_reference(self.case(density=6),16,60)
        for key in a:
            self.assertAlmostEqual(float(translated[key]/a[key]),1,places=14)
            self.assertAlmostEqual(float(doubled[key]/a[key]),2,places=14)
        cavity=rz.finite_volume_reference(rz.CASES[3],16,60)
        self.assertGreater(cavity["radial_acceleration"],0)
        self.assertEqual(cavity["axial_acceleration"],0)
        for case in [rz.CASES[0],rz.CASES[3],rz.CASES[4]]:
            low=rz.finite_volume_reference(case,16,60)
            high=rz.finite_volume_reference(case,16,100)
            self.assertEqual({k:float(v) for k,v in low.items()},
                             {k:float(v) for k,v in high.items()})

    def test_force_is_negative_potential_gradient(self):
        case=self.case()
        base=rz.finite_volume_reference(case,16,60)
        with localcontext() as ctx:
            ctx.prec=60
            h=Decimal("1e-6")
            for coordinate,force in [("r_observer","radial_acceleration"),
                                     ("z_observer","axial_acceleration")]:
                c=Decimal(case[coordinate])
                left=rz.finite_volume_reference(dict(case,**{coordinate:c-h}),16,60)["potential"]
                right=rz.finite_volume_reference(dict(case,**{coordinate:c+h}),16,60)["potential"]
                derivative=-(right-left)/(2*h)
                self.assertAlmostEqual(float(derivative/base[force]),1,places=10)

    def test_partition_preserves_full_source_mass_and_symmetry(self):
        case=rz.CASES[0]
        whole=rz.finite_volume_reference(case,32,80)
        divided=rz.partitioned_reference(case,16,80)
        self.assertEqual(float(whole["mass"]),float(divided["mass"]))
        self.assertEqual(whole["axial_acceleration"],0)
        # Opposite source halves must cancel, including Decimal accumulation.
        self.assertEqual(divided["axial_acceleration"],0)
        for key in ["potential","radial_acceleration"]:
            self.assertAlmostEqual(float(divided[key]/whole[key]),1,places=13)

    def test_full_volume_multipole_force_gradient_and_trace(self):
        case=rz.CASES[-1]
        result=rz.finite_source_multipoles(case)
        self.assertTrue(result["available"])
        q=result["quadrupole"]
        self.assertEqual(q["Qxx"]+q["Qyy"]+q["Qzz"],0)
        h=1e-3
        for key,force in [("r_observer","radial_acceleration"),
                          ("z_observer","axial_acceleration")]:
            left=rz.finite_source_multipoles(dict(case,**{key:case[key]-h}))
            right=rz.finite_source_multipoles(dict(case,**{key:case[key]+h}))
            gradient=-(right["through_quadrupole"]["potential"]-left["through_quadrupole"]["potential"])/(2*h)
            self.assertAlmostEqual(gradient/result["through_quadrupole"][force],1,places=8)
        self.assertFalse(rz.finite_source_multipoles(rz.CASES[3])["available"])

    def test_invalid_or_singular_observers_rejected(self):
        for kwargs in [dict(r_observer=1,z_observer=0),
                       dict(r_observer=2,z_observer=1),
                       dict(r_observer=-1),dict(density=0),
                       dict(z_observer=float("nan"))]:
            with self.assertRaises(ValueError):
                rz.finite_volume_reference(self.case(**kwargs),8,60)
        # A hollow cavity is outside source despite being within outer cylinder.
        self.assertLess(rz.finite_volume_reference(rz.CASES[3],8,60)["potential"],0)

if __name__ == "__main__":unittest.main()
