"""Small independent counterexamples for read-only Cartesian coverage diagnostics."""
import importlib.util
from fractions import Fraction as F
from pathlib import Path
import tempfile
import unittest
import h5py
import numpy as np
spec = importlib.util.spec_from_file_location("coverage_diagnostic", Path(__file__).resolve().parents[3]/"validation/io/verify_plotfile_coverage.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class CoverageTests(unittest.TestCase):
    def test_mixed_level_partition(self):
        cells = [[(F(0), F(1, 2))], [(F(1, 2), F(3, 4))], [(F(3, 4), F(1))]]
        result = module.coverage(cells, [(F(0), F(1))])
        self.assertTrue(result["exactlyOnceWithinDomain"])
        self.assertEqual(result["measures"]["covered"]["exact"], "1")

    def test_missing_duplicate_and_outside_are_not_accepted(self):
        domain = [(F(0), F(1))]
        for cells, key, value in [
            ([[(F(0), F(1, 2))]], "gap", "1/2"),
            ([[(F(0), F(1))], [(F(0), F(1))]], "overlap_excess", "1"),
            ([[(F(0), F(2))]], "outside", "1")]:
            result = module.coverage(cells, domain)
            self.assertFalse(result["exactlyOnceWithinDomain"])
            self.assertEqual(result["measures"][key]["exact"], value)

    def test_one_ulp_gap_is_reported_without_tolerance(self):
        a = module.exact(1.)
        b = module.exact(np.nextafter(1., 2.))
        result = module.coverage([[(F(0), a)], [(b, F(2))]], [(F(0), F(2))])
        self.assertFalse(result["exactlyOnceWithinDomain"])
        self.assertEqual(result["measures"]["gap"]["exact"], str(b-a))

    def test_non_square_2d_hdf_mapping_and_read_only_digest(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)/"fixture.h5"
            with h5py.File(path, "w") as h:
                h.attrs["dim"], h.attrs["geometry"] = 2, "cartesian"
                h.create_dataset("Data/DENS", data=np.arange(6, dtype=np.float64).reshape(1, 2, 3))
                h.create_dataset("Grid/level", data=[0])
                for axis in [1, 2]:
                    h.create_dataset(f"NativeGrid/logical_x{axis}", data=[0])
                x, y = np.tile(np.arange(3, dtype=np.float64), 2), np.repeat(np.arange(2, dtype=np.float64), 3)
                for axis, v in [(1, x), (2, y)]:
                    h.create_dataset(f"NativeGrid/x{axis}_lower", data=v)
                    h.create_dataset(f"NativeGrid/x{axis}_upper", data=v+1)
            result = module.inspect(path, [[0, 3], [0, 2]], [1, 1])
            self.assertTrue(result["nativeStoredCoverage"]["exactlyOnceWithinDomain"])
            self.assertTrue(result["logicalDyadicCoverage"]["exactlyOnceWithinDomain"])
            self.assertEqual(result["maxStoredEndpointDifferenceFromLogicalDomain"]["exact"], "0")
            self.assertEqual(result["cells"], 6)
            with h5py.File(path, "r+") as h:
                h["NativeGrid/x1_lower"][1] = 0
                h["NativeGrid/x1_upper"][1] = 1
            broken = module.inspect(path, [[0, 3], [0, 2]], [1, 1])
            self.assertFalse(broken["nativeStoredCoverage"]["exactlyOnceWithinDomain"])
            self.assertTrue(broken["logicalDyadicCoverage"]["exactlyOnceWithinDomain"])

    def test_budget_and_invalid_extent_fail(self):
        with self.assertRaisesRegex(ValueError, "budget"):
            module.coverage([[(F(0), F(1)), (F(0), F(1))]], [(F(0), F(1))]*2, budget=1)
        with self.assertRaisesRegex(ValueError, "extent"):
            module.coverage([[(F(1), F(0))]], [(F(0), F(1))])

if __name__ == "__main__":
    unittest.main()
