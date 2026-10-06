"""Exact symmetry/scaling checks of the independent RZ review reference."""
import importlib.util
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[3]
SPEC=importlib.util.spec_from_file_location("rz_reference",ROOT/"validation/gravity/rz_ring_axis_reference.py")
RZ=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(RZ)

class RingAxisReferenceTests(unittest.TestCase):
    def test_precision_convergence_and_force_symmetry(self):
        for case in RZ.CASES:
            args={k:v for k,v in case.items() if k!="name"}
            self.assertEqual(RZ.axis_reference(**args,precision=80),
                             RZ.axis_reference(**args,precision=120))
        below=RZ.axis_reference(0,2,-1,1,3,-3)
        above=RZ.axis_reference(0,2,-1,1,3,3)
        self.assertEqual(below["potential"],above["potential"])
        self.assertEqual(below["axial_acceleration"],-above["axial_acceleration"])
        self.assertGreater(below["axial_acceleration"],0)
        self.assertEqual(RZ.axis_reference(1,2,-1,1,3,0)["axial_acceleration"],0)
    def test_source_axis_interior_and_edge_are_finite(self):
        inside=RZ.axis_reference(0,2,-1,1,3,0)
        edge=RZ.axis_reference(0,2,-1,1,3,1)
        self.assertLess(inside["potential"],0)
        self.assertEqual(inside["axial_acceleration"],0)
        self.assertLess(edge["axial_acceleration"],0)
    def test_axial_translation_and_density_scaling(self):
        first=RZ.axis_reference(1,2,-1,1,3,3)
        shifted=RZ.axis_reference(1,2,4,6,3,8)
        self.assertEqual(first,shifted)
        scaled=RZ.axis_reference(1,2,-1,1,6,3)
        for key,value in first.items():self.assertEqual(scaled[key],2*value)
    def test_finite_volume_partition(self):
        whole=RZ.axis_reference(0,2,-1,1,3,3)
        left=RZ.axis_reference(0,1,-1,1,3,3)
        right=RZ.axis_reference(1,2,-1,1,3,3)
        for key,value in whole.items():
            self.assertAlmostEqual((left[key]+right[key])/value,1,places=14) if value else self.assertEqual(left[key]+right[key],0)
    def test_invalid_domain_rejected(self):
        for args in [(2,1,-1,1,3,3),(0,2,1,-1,3,3),
                     (0,2,-1,1,0,3),(-1,2,-1,1,3,0),
                     (0,2,-1,1,3,float("nan"))]:
            with self.assertRaises(ValueError):RZ.axis_reference(*args)

if __name__=="__main__":unittest.main()
