"""Bounded handoff readback tests; synthetic files remain temporary."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

import h5py
import numpy as np

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("handoff", ROOT / "validation/io/export_plotfile_handoff.py")
handoff = importlib.util.module_from_spec(spec)
spec.loader.exec_module(handoff)


class HandoffTests(unittest.TestCase):
    def fixture(self, path, dtype="f8"):
        with h5py.File(path, "w") as h:
            h.attrs.update(dim=2, geometry="cartesian", plot_publication_state="complete")
            g = h.create_group("NativeGrid")
            g.attrs.update(block_kind="active-leaf", ghost_cells=0)
            h.create_group("SourceIdentity").attrs["build_id"] = "unknown"
            shape = (2, 2, 3)
            values = np.arange(12, dtype=dtype).reshape(shape)
            values[1, 1, 2] = -0.0
            field = h.create_dataset("/Data/DENS", data=values)
            field.attrs["unit"] = "g/cm^3"
            h.create_dataset("/Data/PRES", data=np.arange(12, dtype="f8").reshape(shape) + 100)
            for axis in (1, 2):
                for edge in ("lower", "upper"):
                    h.create_dataset("/NativeGrid/x%d_%s" % (axis, edge),
                                     data=np.full(12, 0 if edge == "lower" else 1, dtype="f8"))
            h.create_dataset("/NativeGrid/cell_measure", data=np.ones(12, dtype="f8"))
            for axis in "xyz":
                h.create_dataset("/Grid/" + axis, data=np.full(12, .5, dtype="f8"))
            h.create_dataset("/Grid/level", data=np.array([2, 1], dtype="u4"))
            h.create_dataset("/Grid/morton", data=np.array([30, 7], dtype="u8"))

    def test_nonsquare_order_all_fields_signed_zero_and_readonly(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "real.h5"
            self.fixture(p)
            before = handoff.digest(p)
            result = handoff.inspect(p, [1, 1, 2])
            self.assertEqual(result["shape"], [2, 2, 3])
            self.assertEqual(result["flatIndex"], 11)
            self.assertEqual(result["fields"]["PRES"]["value"], 111)
            self.assertEqual(result["fields"]["DENS"]["fp64Bits"], "8000000000000000")
            self.assertEqual(result["block"], {"level": 1, "morton": 7})
            self.assertEqual(before, handoff.digest(p))
            self.assertTrue(result["fileUnchanged"])

    def test_fp32_rejected_without_modification(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "real.h5"
            self.fixture(p, "f4")
            before = handoff.digest(p)
            with self.assertRaisesRegex(ValueError, "FP64"):
                handoff.inspect(p, [0, 0, 0])
            self.assertEqual(before, handoff.digest(p))

    def test_incomplete_and_temporary_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "real.h5"
            self.fixture(p)
            with h5py.File(p, "r+") as h:
                h.attrs["plot_publication_state"] = "partial"
            with self.assertRaisesRegex(ValueError, "complete"):
                handoff.inspect(p, [0, 0, 0])
            with self.assertRaisesRegex(ValueError, "temporary"):
                handoff.inspect(Path(d) / "output.partial.h5", [0, 0, 0])

    def test_external_link_and_invalid_index_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "real.h5"
            self.fixture(p)
            with self.assertRaisesRegex(ValueError, "index"):
                handoff.inspect(p, [1, 2, 0])
            with h5py.File(p, "r+") as h:
                del h["Data/PRES"]
                h["Data/PRES"] = h5py.ExternalLink("other.h5", "/value")
            with self.assertRaisesRegex(ValueError, "nonlocal"):
                handoff.inspect(p, [0, 0, 0])


if __name__ == "__main__":
    unittest.main()
