import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("platform_preflight", ROOT/"validation/gravity/curved/platform_preflight.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class PlatformPreflightTests(unittest.TestCase):
    def test_unknown_metrics_are_not_zero(self):
        row = module.parse_gpus("GPU-one, NVIDIA GPU, 12288, 100, N/A, [Not Supported], [N/A]")[0]
        self.assertEqual(row["power_draw_w"]["state"], "unavailable")
        self.assertNotIn("value", row["power_draw_w"])
        self.assertEqual(row["memory_total_mib"]["value"], 12288)

    def test_invalid_and_ambiguous_telemetry_rejected(self):
        for raw in ("", "bad,row", "GPU-one, GPU, 1, 2, 3, 4, 5",
                    "GPU-one, GPU, 1, 0, NaN, 4, 5",
                    "GPU-one, GPU, 1, 0, 3, 4, 101",
                    "GPU-one, GPU, 1, 0, 3, 4, 5\nGPU-one, GPU, 1, 0, 3, 4, 5"):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                module.parse_gpus(raw)

    def test_multiple_devices_keep_separate_identity(self):
        rows = module.parse_gpus("GPU-one, GPU A, 1, 0, 3, 4, 5\nGPU-two, GPU B, 2, 1, 6, 7, 8")
        self.assertEqual([r["name"] for r in rows], ["GPU A", "GPU B"])

if __name__ == "__main__":
    unittest.main()
