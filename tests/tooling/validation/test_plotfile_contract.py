"""Tiny analytic fixtures and counterexamples for the Plotfile reader bridge."""

import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import h5py
import numpy as np


ROOT = Path(__file__).resolve().parents[3]
MODULE_PATH = ROOT / "validation/backend/plotfile_contract.py"
SPEC = importlib.util.spec_from_file_location("plotfile_contract", MODULE_PATH)
PROBE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROBE)


def header(dim):
    """Declare exact Cartesian test semantics, independent of reader constants."""
    return {
        "schema_version": 1, "publication": "complete", "dimension": dim,
        "geometry": "cartesian", "axes": ["x", "y"][:dim],
        "storage_order": ["block", "i"] if dim == 1 else ["block", "j", "i"],
        "active_leaf_only": True, "time": 0.25,
        "identities": {k: {"state": "known", "value": k + "-A"}
                       for k in ("case", "config", "build", "binary", "eos")},
        "cell_measure": {"unit": "cm" if dim == 1 else "cm^2",
                         "normalization": "per_unit_transverse_area" if dim == 1
                         else "per_unit_transverse_length"},
        "fields": {"DENS": {"unit": "g/cm^3", "centering": "cell",
                            "basis": "scalar", "meaning": "mass_density"}},
    }


def fixture(path, dim=2, metadata=None, layout=None, legacy=False, scale=1.):
    """Tile a domain with one coarse leaf and its adjacent refined leaves.

    2D covers [0,4]x[0,4]: a 2x4 coarse rectangle plus four 1x2 fine
    rectangles. Each block has 2x2 cells. Index sentinels use exact binary
    fractions; Morton labels are deliberately unordered.
    """
    mapping = PROBE.DEFAULT_LAYOUT if layout is None else layout
    meta = header(dim) if metadata is None else copy.deepcopy(metadata)
    if dim == 1:
        boxes = [[(0., 2.)], [(2., 3.)], [(3., 4.)]]
        shape = (3, 2)
    else:
        boxes = [[(0., 2.), (0., 4.)], [(2., 3.), (0., 2.)],
                 [(2., 3.), (2., 4.)], [(3., 4.), (0., 2.)], [(3., 4.), (2., 4.)]]
        shape = (5, 2, 2)
    values, measures = np.empty(shape), np.empty(shape)
    bounds = np.empty(shape + (dim, 2))
    coords = [np.zeros(shape) for _ in range(3)]
    for index in np.ndindex(shape):
        b, native = index[0], index[1:][::-1]
        values[index] = b * 100 + sum(10 ** a * i for a, i in enumerate(native)) + .125
        widths = []
        for a, i in enumerate(native):
            lo, hi = boxes[b][a]
            dx = (hi - lo) * scale / 2
            edges = (lo * scale + i * dx, lo * scale + (i + 1) * dx)
            bounds[index + (a,)] = edges
            coords[a][index] = .5 * edges[0] + .5 * edges[1]
            widths.append(dx)
        measures[index] = np.prod(widths)
    values[(0,) * len(shape)] = np.nextafter(1., 2.)
    levels = [0] + [1] * (shape[0] - 1)
    mortons = [9, 2, 8, 1, 6][:shape[0]]
    with h5py.File(path, "w") as f:
        f.attrs.update(dim=dim, geometry="cartesian", time=.25)
        for field in meta["fields"]:
            f.create_dataset(mapping["data"] + "/" + field, data=values)
        for name, array in zip(mapping["coordinates"], coords):
            f.create_dataset(name, data=array.ravel())
        f.create_dataset(mapping["level"], data=np.array(levels, dtype="i4"))
        f.create_dataset(mapping["morton"], data=np.array(mortons, dtype="i8"))
        if not legacy:
            f.create_dataset(mapping["bounds"], data=bounds)
            f.create_dataset(mapping["measure"], data=measures)
            selector = mapping["header"]
            if isinstance(selector, str):
                f.create_dataset(selector, data=json.dumps(meta))
            else:
                f.require_group(selector["object"]).attrs[selector["attribute"]] = json.dumps(meta)
    return values, bounds, measures, levels, mortons


class PlotfileContractTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "candidate.h5"

    def invalid(self, result):
        self.assertEqual(result["status"], "invalid", result)
        self.assertTrue(result["errors"])
        self.assertTrue(all("code" in e and "message" in e for e in result["errors"]))
        json.dumps(result, allow_nan=False)

    def test_native_cells_fp64_order_and_morton(self):
        for dim in (1, 2):
            values, bounds, measures, levels, mortons = fixture(self.path, dim=dim)
            for cell in np.ndindex(values.shape):
                with self.subTest(dim=dim, cell=cell):
                    result = PROBE.probe_plotfile(self.path, cell=list(cell))
                    self.assertEqual(result["status"], "candidate", result)
                    q = result["query"]
                    self.assertEqual(q["cell"], list(cell))
                    self.assertEqual(float(q["value"]).hex(), float(values[cell]).hex())
                    np.testing.assert_array_equal(q["native_bounds"], bounds[cell])
                    self.assertEqual(q["measure"], measures[cell])
                    self.assertEqual((q["level"], q["morton"]), (levels[cell[0]], mortons[cell[0]]))
            # Integral measure is 4 cm in 1D and 16 cm^2 in 2D; omitted directions stay explicit.
            self.assertEqual(float(measures.sum()), 4. if dim == 1 else 16.)
            self.assertEqual(PROBE.probe_plotfile(self.path)["query"]["cell"], [0] * len(values.shape))

    def test_alternate_layout_and_scalar_json_dataset(self):
        layout = {"header": "/Metadata/json", "data": "/Fields", "bounds": "/Cells/bounds",
                  "measure": "/Cells/measure", "coordinates": ["/Centers/x", "/Centers/y", "/Centers/z"],
                  "level": "/Blocks/level", "morton": "/Blocks/key"}
        values, *_ = fixture(self.path, layout=layout)
        result = PROBE.probe_plotfile(self.path, layout, cell=[4, 1, 1])
        self.assertEqual(result["status"], "candidate", result)
        self.assertEqual(result["query"]["value"], values[4, 1, 1])
        for bad in ({}, {"header": "/Metadata/json"}, dict(layout, data="Fields")):
            self.invalid(PROBE.probe_plotfile(self.path, bad))

    def test_legacy_unknowns_and_explicit_expectations(self):
        fixture(self.path, legacy=True)
        result = PROBE.probe_plotfile(self.path)
        self.assertEqual(result["status"], "legacy", result)
        self.assertNotIn("metadata", result)
        self.assertNotIn("query", result)
        self.assertIn("units", result["missing_semantics"])
        self.invalid(PROBE.probe_plotfile(self.path, PROBE.DEFAULT_LAYOUT))
        self.invalid(PROBE.probe_plotfile(self.path, expected_identities={"binary": "binary-A"}))
        with h5py.File(self.path, "r+") as f:
            f.attrs["dim"] = 1
        self.invalid(PROBE.probe_plotfile(self.path))

    def test_publication_version_geometry_and_metadata_conflicts(self):
        for key, value in (("publication", "writing"), ("schema_version", True),
                           ("schema_version", 2), ("dimension", 3), ("dimension", True),
                           ("geometry", "cylindrical"), ("geometry", "spherical"),
                           ("axes", ["y", "x"]), ("storage_order", ["block", "i", "j"]),
                           ("active_leaf_only", False), ("time", -.1)):
            with self.subTest(key=key, value=value):
                meta = header(2); meta[key] = value
                fixture(self.path, metadata=meta)
                self.invalid(PROBE.probe_plotfile(self.path))
        fixture(self.path)
        with h5py.File(self.path, "r+") as f:
            f.attrs["time"] = .5
        self.invalid(PROBE.probe_plotfile(self.path))

    def test_identities_and_trusted_expected_values(self):
        fixture(self.path)
        self.assertEqual(PROBE.probe_plotfile(self.path, expected_identities={"binary": "binary-A"})["status"], "candidate")
        for expected in ({"binary": "other"}, {"unsupported": "A"}, {"binary": ""}, []):
            self.invalid(PROBE.probe_plotfile(self.path, expected_identities=expected))
        meta = header(2)
        meta["identities"]["eos"] = {"state": "unknown", "reason": "Not recorded by fixture."}
        fixture(self.path, metadata=meta)
        result = PROBE.probe_plotfile(self.path)
        self.assertEqual(result["status"], "candidate", result)
        self.assertIn("Unknown identity: eos", result["limitations"])
        self.invalid(PROBE.probe_plotfile(self.path, expected_identities={"eos": "eos-A"}))
        del meta["identities"]["build"]
        fixture(self.path, metadata=meta)
        self.invalid(PROBE.probe_plotfile(self.path))

    def test_units_basis_entropy_and_low_dimensional_measure(self):
        for changes in ({"unit": "kg/m^3"}, {"basis": "cartesian"}, {"centering": "face"}, {"meaning": ""}):
            meta = header(2); meta["fields"]["DENS"].update(changes)
            fixture(self.path, metadata=meta)
            self.invalid(PROBE.probe_plotfile(self.path))
        for dim in (1, 2):
            for key, value in (("unit", "cm^3"), ("normalization", "full_volume"),
                               ("normalization", "per_unit_transverse_length" if dim == 1 else "per_unit_transverse_area")):
                meta = header(dim); meta["cell_measure"][key] = value
                fixture(self.path, dim, metadata=meta)
                self.invalid(PROBE.probe_plotfile(self.path))
        meta = header(2)
        meta["fields"]["ENTR"] = {"unit": "(erg/cm^3)/(g/cm^3)^Gamma1", "centering": "cell",
                                  "basis": "scalar", "meaning": "pressure_density_proxy"}
        fixture(self.path, metadata=meta)
        self.assertEqual(PROBE.probe_plotfile(self.path, field="ENTR")["status"], "candidate")
        for key, value in (("meaning", "specific_entropy"), ("unit", "erg/g/K")):
            bad = copy.deepcopy(meta); bad["fields"]["ENTR"][key] = value
            fixture(self.path, metadata=bad)
            self.invalid(PROBE.probe_plotfile(self.path))

    def test_json_duplicates_nonfinite_and_size(self):
        for payload in ('{"schema_version":1,"schema_version":1}', '{"time":NaN}', '{"time":Infinity}',
                        '{"time":1e999}', json.dumps(header(2))[:-1] + ',"extra":1e999}', '[1]', 'not-json'):
            fixture(self.path)
            with h5py.File(self.path, "r+") as f:
                f.attrs["plotfile_candidate"] = payload
            self.invalid(PROBE.probe_plotfile(self.path))
        fixture(self.path)
        self.invalid(PROBE.probe_plotfile(self.path, max_metadata_bytes=32))
        self.invalid(PROBE.probe_plotfile(self.path, max_metadata_bytes=True))

    def test_precision_shapes_and_field_declarations(self):
        for name, shape, dtype in (("/Data/DENS", (5, 2, 2), "f4"), ("/Data/DENS", (5, 4), "f8"),
                                   ("/Grid/x", (20, 1), "f8"), ("/Grid/level", (5,), "f8"),
                                   ("/Native/bounds", (5, 2, 2, 2), "f8"), ("/Native/measure", (5, 4), "f8")):
            fixture(self.path)
            with h5py.File(self.path, "r+") as f:
                del f[name]; f.create_dataset(name, data=np.ones(shape, dtype=dtype))
            self.invalid(PROBE.probe_plotfile(self.path))
        fixture(self.path)
        with h5py.File(self.path, "r+") as f:
            f.create_dataset("Data/undeclared", data=f["Data/DENS"][:])
        self.invalid(PROBE.probe_plotfile(self.path))

    def test_indexes_bounds_measure_and_axis_swap(self):
        fixture(self.path)
        for cell in ([0, 0], [-1, 0, 0], [5, 0, 0], [0, 2, 0], [0, 0, 2], [False, 0, 0]):
            self.invalid(PROBE.probe_plotfile(self.path, cell=cell))
        self.invalid(PROBE.probe_plotfile(self.path, field="EINT"))
        for target, value in (("Native/measure", 0.), ("Native/measure", 3.),
                              ("Grid/x", 99.), ("Grid/level", -1)):
            fixture(self.path)
            with h5py.File(self.path, "r+") as f:
                f[target][(0,) * f[target].ndim] = value
            self.invalid(PROBE.probe_plotfile(self.path))
        fixture(self.path)
        with h5py.File(self.path, "r+") as f:
            edges = f["Native/bounds"][0, 0, 0]
            f["Native/bounds"][0, 0, 0] = edges[::-1]
        self.invalid(PROBE.probe_plotfile(self.path))

    def test_nonfinite_truncated_and_linked_inputs(self):
        for target in ("Data/DENS", "Grid/x", "Native/measure", "Native/bounds"):
            fixture(self.path)
            with h5py.File(self.path, "r+") as f:
                f[target][(0,) * f[target].ndim] = np.nan
            self.invalid(PROBE.probe_plotfile(self.path))
        for link in (h5py.SoftLink("/Data/DENS"), h5py.ExternalLink("other.h5", "/DENS")):
            fixture(self.path)
            with h5py.File(self.path, "r+") as f:
                del f["Native/measure"]; f["Native/measure"] = link
            self.invalid(PROBE.probe_plotfile(self.path))
        self.path.write_bytes(b"truncated HDF5")
        self.invalid(PROBE.probe_plotfile(self.path))

    def test_one_cell_read_budget_and_read_only(self):
        fixture(self.path)
        before = self.path.read_bytes()
        reads = []
        original = h5py.Dataset.__getitem__
        def observe(ds, key):
            reads.append((ds.name, key))
            return original(ds, key)
        with mock.patch.object(h5py.Dataset, "__getitem__", observe):
            result = PROBE.probe_plotfile(self.path, cell=[4, 1, 1])
        self.assertEqual(result["status"], "candidate", result)
        self.assertIn(("/Data/DENS", (4, 1, 1)), reads)
        self.assertIn(("/Native/bounds", (4, 1, 1)), reads)
        self.assertIn(("/Grid/level", 4), reads)
        self.assertEqual(len(reads), 8)
        self.assertEqual(result["logical_payload_bytes_read"], 84)
        self.assertEqual(self.path.read_bytes(), before)

    def test_large_origin_roundoff_and_tiny_density(self):
        fixture(self.path, dim=1)
        with h5py.File(self.path, "r+") as f:
            f["Native/bounds"][0, 0] = [[1e12, 1e12 + .001]]
            f["Grid/x"][0] = 1e12 + .0005
            # Core dx and endpoint subtraction differ after origin rounding.
            f["Native/measure"][0, 0] = .001
        self.assertEqual(PROBE.probe_plotfile(self.path)["status"], "candidate")
        fixture(self.path, dim=1, scale=1e-260)
        with h5py.File(self.path, "r+") as f:
            f["Data/DENS"][0, 0] = 1e-300
        result = PROBE.probe_plotfile(self.path)
        self.assertEqual(result["status"], "candidate", result)
        self.assertEqual(result["query"]["value"], 1e-300)
        with h5py.File(self.path, "r+") as f:
            f["Grid/x"][0] = 1e-250
        self.invalid(PROBE.probe_plotfile(self.path))
        fixture(self.path, dim=2, scale=1e-260)
        with h5py.File(self.path, "r+") as f:
            f["Native/measure"][0, 0, 0] = 1e-323
        # A positive substituted measure cannot repair an unrepresentable area.
        self.invalid(PROBE.probe_plotfile(self.path))

    def test_cli_status_codes_and_malformed_arguments(self):
        def run(*args):
            p = subprocess.run([sys.executable, str(MODULE_PATH), *args], capture_output=True, text=True)
            return p.returncode, json.loads(p.stdout)
        fixture(self.path)
        code, result = run("--file", str(self.path), "--cell", "4,1,1")
        self.assertEqual((code, result["status"]), (0, "candidate"))
        fixture(self.path, legacy=True)
        self.assertEqual(run("--file", str(self.path))[0], 2)
        for args in ((), ("--file", str(self.path), "--cell", "oops"),
                     ("--file", str(self.path), "--unknown", "x")):
            code, result = run(*args)
            self.assertEqual(code, 1)
            self.invalid(result)


if __name__ == "__main__":
    unittest.main()
