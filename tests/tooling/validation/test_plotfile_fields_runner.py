"""Guard the local t=0 runner against accidentally starting an evolved run."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[3] / "validation/io/run_plotfile_fields_t0.py"

class PlotfileFieldsRunnerTests(unittest.TestCase):
    def test_non_t0_or_non_cpu_reference_never_launches_binary(self):
        for change in ("tmax=1", "max_steps=1", "compute_backend=cuda"):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                marker = root / "launched"
                binary = root / "ARCH"
                binary.write_text("#!" + sys.executable + "\nfrom pathlib import Path\nPath(" + repr(str(marker)) + ").write_text('started')\n")
                binary.chmod(0o755)
                reference = root / "reference"
                reference.mkdir()
                values = {"tmax": "0", "max_steps": "-1", "compute_backend": "cpu",
                          "plt_variables": "DENS", "out_dir": str(root / "prior")}
                key, value = change.split("=")
                values[key] = value
                config = reference / "Sod.par"
                config.write_text("".join(key + "=" + value + "\n" for key, value in values.items()))
                digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
                (reference / "summary.json").write_text(json.dumps({"case": "Sod", "inputSha256": digest(config), "binarySha256": digest(binary)}))
                refs = root / "references.json"
                refs.write_text(json.dumps([str(reference)]))
                result = subprocess.run([sys.executable, str(TOOL), "--binary", str(binary),
                                         "--references", str(refs), "--output-root", str(root / "output")],
                                        capture_output=True, text=True, timeout=10)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Only explicitly recorded CPU t=0 inputs may run", result.stderr)
                self.assertFalse(marker.exists())
                self.assertEqual(config.read_text(), "".join(key + "=" + value + "\n" for key, value in values.items()))

if __name__ == "__main__":
    unittest.main()
