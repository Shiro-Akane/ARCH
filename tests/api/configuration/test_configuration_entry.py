"""Check declared-input rejection at the linked ARCH CLI and initial-state entries."""
import hashlib
import json
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


class ConfigurationEntry(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="arch-config-entry-")
        self.addCleanup(self.directory.cleanup)
        self.cwd = Path(self.directory.name)

    def cli(self, text, case="Sod"):
        path = self.cwd / "input.par"
        path.write_text(text)
        before = sorted(p.relative_to(self.cwd) for p in self.cwd.rglob("*"))
        result = subprocess.run([str(ARCH), case, str(path)], cwd=self.cwd,
                                env=ENV, capture_output=True, text=True, timeout=30)
        self.assertNotEqual(result.returncode, 0, "invalid input entered production")
        self.assertEqual(before, sorted(p.relative_to(self.cwd) for p in self.cwd.rglob("*")),
                         "invalid configuration/Setup created scientific output or logs")
        return result

    def api(self, text, mode="--preview", expected=3):
        options = ["--samples", "4"] if mode == "--preview" else []
        before = list(self.cwd.rglob("*"))
        result = subprocess.run([str(ARCH), mode, "Sod", "--config-stdin", *options],
                                input=text, cwd=self.cwd, env=ENV,
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, expected, (result.stdout, result.stderr))
        self.assertEqual(before, list(self.cwd.rglob("*")), "initial API wrote files")
        data = json.loads(result.stdout)
        self.assertEqual(data["identity"]["configRevision"], hashlib.sha256(text.encode()).hexdigest())
        self.assertEqual(data["status"], "ok" if expected == 0 else "error")
        return data

    def test_cli_aggregates_missing_before_output(self):
        text = edit(edit(edit(BASE, "tmax"), "cfl"), "rho_left")
        result = self.cli(text)
        for key in ("tmax", "cfl", "rho_left"):
            self.assertIn(f"[{key}]", result.stderr)
        self.assertNotIn("[Main] Problem created", result.stdout)

    def test_cli_aggregates_explicit_range_errors(self):
        text = edit(edit(edit(BASE, "cfl", "2"), "gamma", "1"), "tmax", "-1")
        text += "\ndt_min=0\node_rtol=2\n"
        result = self.cli(text)
        for key in ("cfl", "gamma", "tmax", "dt_min", "ode_rtol"):
            self.assertIn(f"INVALID_RANGE [{key}]", result.stderr)

    def test_cli_syntax_unknown_and_setup_failure(self):
        result = self.cli(BASE + "\nbroken line\nx_pos=0.3\nnot_a_parameter=1\n")
        for code in ("MALFORMED_LINE", "DUPLICATE_PARAMETER", "UNKNOWN_PARAMETER"):
            self.assertIn(code, result.stderr)
        result = self.cli(BASE, case="not-registered")
        self.assertIn("UNKNOWN_CASE", result.stderr)
        result = self.cli(edit(BASE, "x_pos", "2"))
        self.assertIn("Sod requires an interior interface", result.stderr)
        self.assertIn("[Fatal Error]", result.stderr)

    def test_preview_and_inspection_share_missing_gate(self):
        text = edit(edit(BASE, "rho_left"), "cfl")
        for mode in ("--preview", "--inspect-case"):
            with self.subTest(mode=mode):
                data = self.api(text, mode)
                self.assertEqual(data["stage"], "configuration")
                self.assertIsNone(data["data"])
                messages = "\n".join(d["message"] for d in data["diagnostics"])
                self.assertIn("[rho_left]", messages)
                self.assertIn("[cfl]", messages)

    def test_initial_state_needs_no_evolution_endpoint(self):
        text = edit(BASE, "tmax")
        data = self.api(text, expected=0)
        fields = {field["key"]: field["values"] for field in data["data"]["fields"]}
        self.assertEqual(fields["DENS"], [1, 1, 0.125, 0.125])
        for got, expected in zip(fields["PRES"], [1, 1, 0.1, 0.1]):
            self.assertAlmostEqual(got, expected)
        self.api(text, "--inspect-case", expected=0)


if __name__ == "__main__":
    unittest.main()
