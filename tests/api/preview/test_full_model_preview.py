"""Real initializer routing, native mapping and bounded field/AMR contracts.

Reuse maintained case-inspection inputs; no second production parameter catalog.
These tests do not certify evolution or scientific convergence.
"""
import hashlib
import json
import math
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest

ARCH, ROOT = map(lambda x: Path(x).resolve(), sys.argv[1:3])
del sys.argv[1:3]
_saved = sys.argv[:]
try:
    sys.argv = ["fixture", str(ARCH), str(ROOT)]
    _fixtures = runpy.run_path(str(ROOT/"tests/api/inspection/test_case_inspection.py"))
finally:
    sys.argv = _saved
CASES, edit, COMMON = (_fixtures[k] for k in ("CASES", "edit", "COMMON"))
ENV = dict(os.environ, OMP_NUM_THREADS="1", CUDA_VISIBLE_DEVICES="")

class FullInitialPreview(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="arch-full-initial-")
        self.addCleanup(self.temp.cleanup)
        self.cwd = Path(self.temp.name)

    def call(self, case, text, *options, command="--preview", code=0):
        before = list(self.cwd.rglob("*"))
        run = subprocess.run([str(ARCH), command, case, "--config-stdin",
                              "--request-id", "unsaved-full", *options],
                             input=text, text=True, capture_output=True, cwd=self.cwd,
                             env=ENV, timeout=360)
        self.assertEqual(run.returncode, code, (case, run.stdout[-6000:], run.stderr))
        obj = json.loads(run.stdout)
        self.assertEqual(obj["identity"]["configRevision"], hashlib.sha256(text.encode()).hexdigest())
        self.assertEqual(obj["identity"]["requestId"], "unsaved-full")
        self.assertEqual(before, list(self.cwd.rglob("*")), "init-only creates no output")
        self.assertEqual(obj["execution"]["timeStepping"], "not_executed")
        return obj

    def test_every_maintained_model_field_and_root_mesh(self):
        registry = json.loads(subprocess.check_output([str(ARCH), "--list-cases"], cwd=self.cwd, env=ENV))
        caps = json.loads(subprocess.check_output([str(ARCH), "--preview-capabilities"], cwd=self.cwd, env=ENV))
        rows = {v["caseId"]: v for v in registry["cases"]}
        self.assertEqual(set(rows), set(CASES), "new models need maintained coverage inputs")
        self.assertEqual({v["caseId"] for v in caps["modelCapabilities"]}, set(CASES))
        for case, (_, text) in CASES.items():
            with self.subTest(case=case):
                inspected = self.call(case, text, command="--inspect-case")
                dim = inspected["state"]["grid"]["dimension"]
                self.assertIn(dim, rows[case]["previewDimensions"])
                options = ["--samples", "2"] if dim == 1 else [
                    v for axis in range(1, dim+1) for v in (f"--samples-x{axis}", "2")]
                response = self.call(case, text, *options)
                data = response["data"]
                self.assertEqual(data["sampling"]["shape"], [2]*dim)
                self.assertEqual(data["sampling"]["count"], 2**dim)
                self.assertEqual(len(data["fields"]), 5+dim)
                self.assertEqual(len(data["axes"]), dim)
                self.assertEqual(response["state"]["species"], inspected["state"]["species"])
                fields = {f["key"]: f["values"] for f in data["fields"]}
                for values in fields.values():
                    self.assertEqual(len(values), 2**dim)
                    self.assertTrue(all(math.isfinite(v) for v in values))
                # Field bin centers match the inspection quarter/three-quarter
                # subset, so this checks routing and index order independently.
                for k in range(2 if dim == 3 else 1):
                    for j in range(2 if dim >= 2 else 1):
                        for i in range(2):
                            index = (k*(2 if dim >= 2 else 1)+j)*2+i
                            probe_index = (2*k*(3 if dim >= 2 else 1)+2*j)*3+2*i
                            raw = {f["key"]: f for f in inspected["data"]["samples"][probe_index]["fields"]}
                            for key in ("DENS", "PRES", "TEMP", "VELX", "VELY", "VELZ"):
                                if key in fields and raw[key]["consumedByConversion"]:
                                    self.assertTrue(math.isclose(fields[key][index], raw[key]["value"],
                                                                 rel_tol=2e-13, abs_tol=1e-14), (case,key,index))
                if case in ("JeansWave", "SmoothAdvection", "ExternalGravity"):
                    self.assertEqual(response["state"]["species"], [])
                if case == "BurnOneZone":
                    self.assertEqual(data["coordinates"]["representation"], "uniform-state")
                root_text = edit(text, nblockx1=1, nblockx2=int(dim>=2),
                                 nblockx3=int(dim==3), lrefinemin=0, lrefinemax=0, max_blocks=8)
                mesh = self.call(case, root_text, "--mesh-max-blocks", "8",
                                 "--mesh-memory-mib", "256", command="--preview-amr")
                self.assertEqual(mesh["status"], "ok")
                self.assertEqual(mesh["data"]["leafCount"], 1)
                self.assertEqual(len(mesh["data"]["leaves"][0]["lower"]), dim)
                self.assertNotIn("fields", mesh["data"])

    def test_three_dimensional_representative_models_and_curved_reference_inputs(self):
        payloads = [(case, text) for case, (_, text) in CASES.items()
                    if case in ("Sedov", "RT", "GravityBox")]
        for geometry in ("cartesian", "cylindrical", "spherical"):
            path = ROOT/f"simulation/SNIaCoupled/SNIaCoupled_3d_{geometry}_amr.par"
            text = path.read_text().replace("EOS_toolkit/tables/helmholtz/helm_table.dat",
                                           str(ROOT/"EOS_toolkit/tables/helmholtz/helm_table.dat"))
            payloads.append(("SNIaCoupled", text))
        for case, text in payloads:
            with self.subTest(case=case, config=text):
                # The 1D GravityBox file has no inactive-axis bounds/centres.
                # Make a fully explicit periodic cube with the same x1 extent;
                # this fixture checks initialization, not a new evolution budget.
                if case == "GravityBox":
                    text = edit(text, x2_min=0, x2_max=1e8, x3_min=0, x3_max=1e8,
                                center_y=5e7, center_z=5e7)
                text = edit(text, nblockx1=1, nblockx2=1, nblockx3=1,
                            max_blocks=8, lrefinemin=0, lrefinemax=0)
                result = self.call(case, text, "--samples-x1", "5",
                                   "--samples-x2", "3", "--samples-x3", "2")
                self.assertEqual(result["data"]["dimension"], 3)
                self.assertEqual(result["data"]["sampling"]["shape"], [2, 3, 5])
                self.assertEqual(len(result["data"]["fields"]), 8)
                mesh = self.call(case, text, "--mesh-max-blocks", "8",
                                 "--mesh-memory-mib", "256", command="--preview-amr")
                self.assertEqual(mesh["status"], "ok")
                self.assertEqual(mesh["data"]["leafCount"], 1)
                leaf = mesh["data"]["leaves"][0]
                self.assertEqual(len(leaf["lower"]), 3)
                self.assertEqual(leaf["cellShape"], [16, 16, 16])

    def gaussian(self, geometry="cartesian", dim=3):
        return edit(COMMON, geometry=geometry, nblockx1=1, nblockx2=int(dim>=2),
                    nblockx3=int(dim==3), max_blocks=8, lrefinemin=0, lrefinemax=0,
                    x1_min=.2, x1_max=1, x2_min=.3, x2_max=2.5, x3_min=-.4, x3_max=1.5,
                    rho0=2, p0=3, amp=.2, width=1, xc=.4, yc=.1, zc=.3,
                    pressure_amplitude=.7, u_amplitude=.2, v_amplitude=.3,
                    w_amplitude=.4, gas_cv=1)

    def test_non_cubic_native_mapping(self):
        for geometry in ("cartesian", "spherical", "cylindrical"):
            with self.subTest(geometry=geometry):
                result = self.call("Gaussian", self.gaussian(geometry),
                                   "--samples-x1", "5", "--samples-x2", "3", "--samples-x3", "2")
                data = result["data"]
                self.assertEqual(data["kind"], "volume")
                self.assertEqual(data["sampling"]["shape"], [2,3,5])
                self.assertEqual(data["sampling"]["order"], "x1-fastest")
                self.assertEqual(data["coordinates"]["velocityBasis"], "native-orthonormal")
                axes = [a["values"] for a in data["axes"]]
                self.assertEqual([a["unit"] for a in data["axes"]],
                                 {"cartesian":["cm","cm","cm"],"spherical":["cm","rad","rad"],
                                  "cylindrical":["cm","cm","rad"]}[geometry])
                if geometry != "cartesian":
                    native_names = [axis["nativeName"] for axis in data["coordinates"]["metadata"]["axes"]]
                    by_key = {f["key"]: f for f in data["fields"]}
                    for axis,key in enumerate(("VELX","VELY","VELZ")):
                        self.assertEqual(by_key[key]["displayName"], f"Native {native_names[axis]} velocity")
                fields = {f["key"]:f["values"] for f in data["fields"]}
                for k,c in enumerate(axes[2]):
                    for j,b in enumerate(axes[1]):
                        for i,a in enumerate(axes[0]):
                            if geometry == "spherical":
                                x,y,z = a*math.sin(b)*math.cos(c), a*math.sin(b)*math.sin(c), a*math.cos(b)
                            elif geometry == "cylindrical":
                                x,y,z = a*math.cos(c), a*math.sin(c), b
                            else:
                                x,y,z = a,b,c
                            pulse = math.exp(-((x-.4)**2+(y-.1)**2+(z-.3)**2))
                            index = (k*3+j)*5+i
                            for key,value in {"DENS":2,"PRES":3*(1+.7*pulse),
                                              "VELX":.2*pulse,"VELY":.3*pulse,"VELZ":.4*pulse}.items():
                                self.assertTrue(math.isclose(fields[key][index], value,
                                                             rel_tol=2e-13,abs_tol=1e-14), (geometry,key,index))

    def test_curved_inactive_coordinates_and_mesh_units(self):
        for geometry in ("spherical", "cylindrical"):
            for dim in (1,2):
                text = self.gaussian(geometry,dim)
                options = ["--samples","3"] if dim == 1 else ["--samples-x1","3","--samples-x2","2"]
                result = self.call("Gaussian",text,*options)
                fixed = {v["name"]:v for v in result["data"]["sampling"]["fixedCoordinates"]}
                if geometry == "spherical":
                    self.assertEqual(fixed["theta"]["unit"],"rad")
                    self.assertAlmostEqual(fixed["theta"]["value"],math.pi/2)
                    if dim == 1: self.assertEqual(fixed["phi"]["value"],0)
                else:
                    self.assertEqual(fixed["z_cy"]["value"],0)
                    if dim == 2: self.assertEqual(result["data"]["axes"][1]["unit"],"rad")
                mesh = self.call("Gaussian",text,"--mesh-max-blocks","8",
                                 "--mesh-memory-mib","256",command="--preview-amr")
                self.assertIsNone(mesh["data"]["unit"])
                self.assertEqual(mesh["data"]["coordinates"]["basis"],"native-grid")

    def test_limits_and_unapproved_domains(self):
        for options in [
            ["--samples","8"],["--samples-x1","3","--samples-x2","3"],
            ["--samples-x1","64","--samples-x2","64","--samples-x3","64"],
            ["--samples-x1","3","--samples-x2","3","--samples-x3","65"]]:
            result = self.call("Gaussian",self.gaussian(),*options,code=2)
            self.assertIsNone(result["data"])
        result = self.call("RT",edit(CASES["RT"][1],nblockx2=0),"--samples","3",code=4)
        self.assertEqual(result["stage"],"support")
        result = self.call("CellularDet",edit(CASES["CellularDet"][1],shock_dir=2),
                           "--samples-x1","2","--samples-x2","2",code=4)
        self.assertIsNone(result["data"])

if __name__ == "__main__":
    unittest.main()
