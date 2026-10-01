"""Actual-binary v3 checks: partial input is not a default-filled runtime config."""
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ARCH = Path(sys.argv.pop(1)).resolve()
ROOT = Path(sys.argv.pop(1)).resolve()
BASE = (ROOT / "src/api/examples/configuration-v3-candidate/sod-valid.par").read_text()
ENV = dict(os.environ, OMP_NUM_THREADS="1", CUDA_VISIBLE_DEVICES="")

def edit(text, key, value=None):
    text = re.sub(rf"^{re.escape(key)}\s*=.*\n?", "", text, flags=re.MULTILINE)
    return text if value is None else text + f"\n{key}={value}\n"

def records(data):
    return {d["key"]: d for d in data["parameters"]}

class ConfigurationV3(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="arch-config-v3-")
        self.addCleanup(self.tmp.cleanup)
        self.cwd = Path(self.tmp.name)

    def call(self, args, text=None, expected=0):
        p = subprocess.run([str(ARCH), *args], input=text, text=True, capture_output=True,
                           cwd=self.cwd, env=ENV, timeout=30)
        self.assertEqual(p.returncode, expected, (p.stdout[:4000], p.stderr))
        self.assertEqual(list(self.cwd.iterdir()), [], "static API wrote files")
        self.assertLessEqual(len(p.stdout.encode()), 8 * 1024 * 1024)
        data = json.loads(p.stdout)
        self.assertEqual(data["version"], "3")
        return data

    def inspect(self, text=BASE, case="Sod", expected=0):
        data = self.call(["--inspect-config", case, "--config-stdin"], text, expected)
        self.assertEqual(data["identity"]["configRevision"], hashlib.sha256(text.encode()).hexdigest())
        self.assertEqual(data["identity"]["caseId"], case)
        for key, value in dict(setup="not_executed", eos="not_loaded", cuda="not_initialized",
                               filesystem="not_accessed", simulationReadiness="not_checked").items():
            self.assertEqual(data["execution"][key], value)
        self.assertEqual(data["status"], "ok" if expected == 0 else "error")
        return data

    def resources(self, text, expected=0):
        result = subprocess.run([str(ARCH), "--amr-resources", "count-context", "--config-stdin"],
                                input=text, text=True, capture_output=True,
                                cwd=self.cwd, env=ENV, timeout=30)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        self.assertEqual(list(self.cwd.iterdir()), [])
        data = json.loads(result.stdout)
        self.assertEqual(data["identity"]["configRevision"], hashlib.sha256(text.encode()).hexdigest())
        self.assertEqual(data["execution"]["configurationScope"], "resource-count-inputs")
        self.assertEqual(data["execution"]["simulationReadiness"], "not_checked")
        return data

    def test_resources_require_explicit_counts_not_physical_defaults(self):
        data = self.resources("", 3)
        self.assertIsNone(data["data"])
        self.assertEqual({d["parameterKey"] for d in data["diagnostics"]},
                         {"nblockx1", "nblockx2", "nblockx3", "lrefinemin", "lrefinemax"})
        text = "nblockx1=4\nnblockx2=0\nnblockx3=0\nlrefinemin=0\nlrefinemax=3\nmax_blocks=128\n"
        data = self.resources(text)["data"]
        reference = json.loads((ROOT / "src/api/examples/local-workflow/sod-resources.json").read_text())["data"]
        for key in ["rootBlocks", "dimension", "paddedCellsPerBlock", "configuredPoolCapacity",
                    "poolPreallocatedBaseBytes", "levels", "speciesCount", "oomPrediction"]:
            self.assertEqual(data[key], reference[key], key)
        # Counting a mesh does not require inventing EOS, burn, case or endpoint.
        for key in ["nblockx1", "nblockx2", "nblockx3", "lrefinemin", "lrefinemax"]:
            with self.subTest(key=key):
                bad = self.resources(edit(text, key), 3)
                self.assertIn(key, {d["parameterKey"] for d in bad["diagnostics"]})

    def test_resource_invalid_topology_and_explicit_bad_fields(self):
        text = "nblockx1=2\nnblockx2=0\nnblockx3=0\nlrefinemin=0\nlrefinemax=0\n"
        for bad in [edit(text, "nblockx3", "1"), edit(text, "nblockx1", "1.5"),
                    edit(text, "lrefinemax", "16"), text+"cfl=oops\n",
                    text+"bad line\n", text+"nblockx1=3\n"]:
            with self.subTest(bad=bad):
                self.assertIsNone(self.resources(bad, 3)["data"])
        self.assertEqual(self.resources(text)["data"]["dimension"], 1)
        self.assertEqual(self.resources(edit(text, "nblockx2", "3"))["data"]["rootBlocks"], 6)
        three = edit(edit(text, "nblockx2", "3"), "nblockx3", "4")
        self.assertEqual(self.resources(three)["data"]["rootBlocks"], 24)

    def test_resource_overflow_stays_unknown_and_advisory(self):
        text = "nblockx1=2147483647\nnblockx2=2147483647\nnblockx3=2147483647\nlrefinemin=0\nlrefinemax=15\n"
        data = self.resources(text)["data"]
        self.assertIsNone(data["rootBlocks"])
        self.assertTrue(all(x["overflow"] for x in data["levels"]))
        self.assertTrue(all(x["activeCells"] is None for x in data["levels"]))
        self.assertIsNone(data["speciesCount"])
        self.assertTrue(data["advisoryOnly"])
        self.assertEqual(data["oomPrediction"], "not-provided")

    def test_schema(self):
        data = self.call(["--config-schema"])
        p = records(data)
        self.assertEqual(len(p), 94)
        self.assertEqual(sum(v["allowedDefault"] is not None for v in p.values()), 25)
        self.assertIsNone(p["cfl"]["allowedDefault"])
        self.assertEqual(p["restart_file"]["requirement"]["condition"]["dependencies"], ["restart"])
        self.assertEqual(p["gravity_max_cycles"]["applicability"]["dependencies"], ["gravity_type"])
        self.assertIn("gravity_G", data["retiredKeys"])
        self.assertNotIn("gravity_G", p)
        cases = {c["caseId"]: c for c in data["caseDeclarations"]}
        self.assertEqual(len(cases), 14)
        self.assertEqual(len(cases["Sod"]["parameters"]), 7)
        self.assertRegex(cases["Sod"]["sourceSha256"], r"^[0-9a-f]{64}$")
        for v in [*p.values(), *cases["Sod"]["parameters"]]:
            self.assertNotIn("defaultValue", v)
            self.assertNotIn("defaultSource", v)

    def test_valid_provenance(self):
        data = self.inspect()
        self.assertEqual(data["completeness"]["state"], "complete")
        self.assertTrue(all(data["coverage"].values()))
        p = records(data)
        self.assertIsNone(p["dt_min"]["parsedValue"])
        self.assertIsNotNone(p["dt_min"]["resolvedValue"])
        self.assertEqual(p["dt_min"]["inputState"], "missing")
        self.assertEqual(p["dt_min"]["valueSource"], "documented-default")
        self.assertIsNotNone(p["dt_min"]["sourceEvidence"])
        self.assertEqual(p["log_dir"]["resolvedValue"], p["out_dir"]["resolvedValue"])
        self.assertEqual(p["log_dir"]["valueSource"], "derived")
        self.assertEqual(p["log_dir"]["sourceEvidence"]["dependencies"], ["out_dir"])
        self.assertEqual(p["x_pos"]["caseId"], "Sod")

    def test_empty_and_unknown_conditions(self):
        data = self.inspect("", expected=3)
        p = records(data)
        self.assertEqual(data["completeness"]["state"], "incomplete")
        self.assertFalse(data["coverage"]["conditionsComplete"])
        for key in ["geometry", "nblockx1", "eos_type", "use_burn", "cfl", "x_pos"]:
            self.assertEqual(p[key]["inputState"], "missing")
            for field in ["rawValue", "parsedValue", "resolvedValue", "valueSource"]:
                self.assertIsNone(p[key][field])
            self.assertEqual(p[key]["locations"], [])
        self.assertEqual(p["gamma"]["requirement"]["state"], "unknown-dependency")
        self.assertIsNone(p["gamma"]["requirement"]["required"])
        codes = {(d["code"], d["parameterKey"]) for d in data["diagnostics"]}
        self.assertIn(("UNRESOLVED_DEPENDENCY", "gamma"), codes)
        self.assertNotIn(("MISSING_PARAMETER", "gamma"), codes)

    def test_missing_required_classes(self):
        for key in ["compute_backend", "cfl", "tmax", "gamma", "rho_left", "x1_min", "use_burn"]:
            with self.subTest(key=key):
                data = self.inspect(edit(BASE, key), expected=3)
                self.assertIn(("MISSING_PARAMETER", key),
                              {(d["code"], d["parameterKey"]) for d in data["diagnostics"]})
        p = records(self.inspect(edit(BASE, "use_burn"), expected=3))
        self.assertEqual(p["ode_solver"]["requirement"]["state"], "unknown-dependency")

    def test_raw_identity_zero_false(self):
        a = self.inspect(edit(BASE, "x_pos", "   0.50  "))
        b = self.inspect(edit(BASE, "x_pos", "0.5"))
        p = records(a)
        self.assertEqual(p["x_pos"]["rawValue"], "   0.50  ")
        self.assertEqual(p["x_pos"]["parsedValue"], 0.5)
        self.assertEqual(p["nblockx2"]["parsedValue"], 0)
        self.assertIs(p["use_burn"]["parsedValue"], False)
        self.assertNotEqual(a["identity"]["configRevision"], b["identity"]["configRevision"])
        self.assertEqual(p["x_pos"]["locations"][0]["source"], "stdin")

    def test_duplicate_syntax_inactive_bad_token(self):
        data = self.inspect(BASE + "\nx_pos=0.25\nbroken line\node_rtol=oops\n", expected=3)
        p = records(data)
        self.assertEqual(p["x_pos"]["inputState"], "duplicate")
        self.assertEqual(len(p["x_pos"]["locations"]), 2)
        for key in ["parsedValue", "resolvedValue", "rawValue", "valueSource"]:
            self.assertIsNone(p["x_pos"][key])
        self.assertEqual(p["ode_rtol"]["inputState"], "invalid")
        self.assertIsNone(p["ode_rtol"]["parsedValue"])
        self.assertIsNone(p["ode_rtol"]["resolvedValue"])
        self.assertEqual(p["ode_rtol"]["applicability"]["state"], "not-applicable")
        self.assertTrue({"MALFORMED_LINE", "DUPLICATE_PARAMETER", "INVALID_NUMBER"}
                        <= {d["code"] for d in data["diagnostics"]})
        for d in data["diagnostics"]:
            self.assertTrue({"expected", "conditionId", "module", "locations", "relatedKeys"} <= d.keys())

    def test_range_invalid_keeps_parsed(self):
        p = records(self.inspect(edit(BASE, "cfl", "2"), expected=3))["cfl"]
        self.assertEqual(p["inputState"], "invalid")
        self.assertEqual(p["parsedValue"], 2)
        for key in ["resolvedValue", "valueSource", "sourceEvidence"]:
            self.assertIsNone(p[key])

    def test_retired_unknown_and_method(self):
        data = self.inspect(edit(BASE, "solver", "bogus") +
                            "\ntimeintegrator=RK2\ngravity_G=1\ntypo_parameter=3\n", expected=3)
        errors = {(d["code"], d["parameterKey"]) for d in data["diagnostics"]}
        self.assertTrue({("INVALID_OPTION", "solver"), ("RETIRED_PARAMETER", "gravity_G"),
                         ("RETIRED_PARAMETER", "timeintegrator"),
                         ("UNKNOWN_PARAMETER", "typo_parameter")} <= errors)

    def test_path_missing_vs_unchecked(self):
        text = edit(BASE, "restart", "true")
        self.assertIsNone(records(self.inspect(text, expected=3))["restart_file"]["resolvedValue"])
        p = records(self.inspect(text + "\nrestart_file=/not/a/checkpoint.h5\n"))["restart_file"]
        self.assertFalse(p["path"]["existenceChecked"])
        self.assertEqual(p["valueSource"], "input")

    def test_sparse_case_defined_composition(self):
        text = edit(BASE, "network_name", "aprox13")
        for key in ["x_pos", "rho_left", "p_left", "u_left", "rho_right", "p_right", "u_right"]:
            text = edit(text, key)
        text += '\nrho0=1e7\ntemperature0=3e9\nsmallx=1e-20\nxhe4=0.75\n'
        data = self.inspect(text, case="BurnOneZone")
        p = records(data)
        self.assertEqual(p["xhe4"]["parsedValue"], 0.75)
        self.assertEqual(p["xhe4"]["resolvedValue"], 0.75)  # no normalization
        self.assertEqual(p["xc12"]["inputState"], "missing")
        self.assertIsNone(p["xc12"]["parsedValue"])
        self.assertEqual(p["xc12"]["resolvedValue"], 0)
        self.assertEqual(p["xc12"]["valueSource"], "case-defined")
        self.assertEqual(p["xc12"]["sourceEvidence"]["dependencies"], ["network_name"])
        # Setup additionally requires burn=true; static declarations do not
        # certify model physical/runtime preconditions or execute that owner.
        self.assertEqual(data["execution"]["simulationReadiness"], "not_checked")

    def test_setup_domain_is_not_claimed_by_static_inspection(self):
        data = self.inspect(edit(BASE, "x_pos", "2"))
        self.assertEqual(data["completeness"]["scope"], "declared-configuration-before-setup")
        self.assertEqual(data["execution"]["setup"], "not_executed")

    def test_unknown_case(self):
        data = self.inspect(BASE, case="not-registered", expected=3)
        self.assertFalse(data["coverage"]["caseParametersComplete"])
        self.assertFalse(data["coverage"]["diagnosticsComplete"])
        self.assertEqual(data["execution"]["caseDeclarations"], "not_checked")
        self.assertIn("UNKNOWN_CASE", {d["code"] for d in data["diagnostics"]})

    def test_migrated_formal_examples_are_explicit_and_complete(self):
        examples = {
            "Sod": ("Sod/Sod.par", {"compute_backend": "cpu", "x_pos": .5}),
            "GravityBox": ("GravityBox/GravityBox.par",
                           {"network_name": "none", "gas_cv": 1.2471693927e8,
                            "width": 8e6, "center_x": 5e7, "hydrostatic_radial": "false"}),
            "RT": ("RTinstability/RT_instab.par", {"limiter": "minmod", "gravity_g_x": 0.0, "gravity_g_z": 0.0}),
            "Sedov": ("Sedov/Sedov.par", {"center_z": .5, "explosion_energy": 1.0}),
            "JeansWave": ("JeansWave/JeansWave.par",
                          {"phase": 0.0, "mode": 1, "standing_wave": "false",
                           "gravity_boundary": "periodic", "gravity_rtol": 1e-10}),
            "CooperativeHotspots": ("CooperativeHotspots/CooperativeHotspots.par",
                                    {"smallt": 1e5, "smallx": 1e-20,
                                     "eos_coulomb_mult": 1.0}),
        }
        for case, (path, explicit) in examples.items():
            with self.subTest(case=case):
                text = (ROOT / "simulation" / path).read_text()
                data = self.inspect(text, case=case)
                self.assertEqual(data["completeness"]["state"], "complete")
                values = records(data)
                for key, value in explicit.items():
                    self.assertEqual(values[key]["valueSource"], "input")
                    self.assertEqual(values[key]["resolvedValue"], value)

    def test_model_generated_composition_does_not_require_external_fractions(self):
        text = (ROOT / "simulation/GaussianPulse/Gaussian.par").read_text()
        data = self.inspect(text, case="Gaussian")
        fractions = [p for p in data["parameters"] if p["group"] == "Composition"]
        self.assertEqual(len(fractions), 19)
        for p in fractions:
            self.assertEqual(p["applicability"]["state"], "not-applicable")
            self.assertIsNone(p["parsedValue"])
            self.assertIsNone(p["resolvedValue"])
            self.assertIsNone(p["valueSource"])
        data = self.inspect(edit(text, "xhe4", .4), case="Gaussian")
        self.assertEqual(records(data)["xhe4"]["parsedValue"], .4)
        self.assertEqual(records(data)["xhe4"]["applicability"]["state"], "not-applicable")
        for raw in ["-1", "nan", "bad"]:
            self.inspect(edit(text, "xhe4", raw), case="Gaussian", expected=3)
        self.inspect(text + "\nxhe4=.2\nxhe4=.3\n", case="Gaussian", expected=3)
        # A model that actually consumes composition must still reject no seed.
        cellular = (ROOT / "simulation/Cellular/CellularPreview2D.par").read_text()
        for key in ["xhe4", "xc12", "xo16"]:
            cellular = edit(cellular, key)
        data = self.inspect(cellular, case="CellularDet", expected=3)
        self.assertIn("INVALID_COMPOSITION", {d["code"] for d in data["diagnostics"]})

    def test_gaussian_init_owns_spatial_fractions(self):
        text = (ROOT / "simulation/GaussianPulse/Gaussian.par").read_text()
        text = edit(text, "eos_table_path", ROOT / "EOS_toolkit/tables/helmholtz/helm_table.dat")
        responses = []
        for payload in [text, text + "\nxhe4=.9\nxc12=.1\n"]:
            p = subprocess.run([str(ARCH), "--inspect-case", "Gaussian", "--config-stdin"],
                               input=payload, text=True, capture_output=True, cwd=self.cwd,
                               env=ENV, timeout=30)
            self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
            data = json.loads(p.stdout)
            self.assertEqual(data["identity"]["configRevision"], hashlib.sha256(payload.encode()).hexdigest())
            self.assertEqual(data["execution"]["timeStepping"], "not_executed")
            self.assertEqual(list(self.cwd.iterdir()), [])
            responses.append(data)
        self.assertEqual(responses[0]["data"], responses[1]["data"])
        self.assertTrue({"xhe4", "xc12"} <= set(responses[1]["parameterMetadata"]["unobservedInputKeys"]))
        for sample in responses[0]["data"]["samples"]:
            x, y, z = sample["cartesianPosition"]
            pulse = .5 * math.exp(-((x/.1)**2 + (y/.1)**2 + (z/.1)**2))
            # Same frozen initializer formula, independent of external fractions.
            self.assertEqual(sample["massFractions"], [1-pulse, pulse] + [0]*17)

    def test_bounded_error_keeps_identity(self):
        text = BASE + "".join(f"unknown_key_{i}=0\n" for i in range(30000))
        self.assertLess(len(text.encode()), 1024 * 1024)
        data = self.inspect(text, expected=7)
        self.assertEqual(data["completeness"]["state"], "undetermined")
        self.assertFalse(any(data["coverage"].values()))
        self.assertIn("RESPONSE_TOO_LARGE", {d["code"] for d in data["diagnostics"]})
        for d in data["diagnostics"]:
            self.assertTrue({"expected", "conditionId", "module", "locations", "relatedKeys"} <= d.keys())

if __name__ == "__main__":
    unittest.main()
