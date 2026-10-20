import importlib.util
from pathlib import Path
import unittest
import json
import copy

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

    def observation(self):
        return dict(team_size=2,num_places=2,proc_bind=4,openmp_version=201511,
            members=[dict(thread=i,cpu=i,place=i,affinity_readable=True,affinity=[i]) for i in range(2)])

    def test_actual_team_and_affinity_validated(self):
        expected=self.observation()
        self.assertEqual(module.parse_openmp_probe(json.dumps(expected),2,{0,1}),expected)

    def test_invalid_team_member_and_binding_rejected(self):
        for kind in ("team","duplicate","mask","cpu","place","duplicate_place","unreadable","bind"):
            value=copy.deepcopy(self.observation())
            if kind=="team":value["team_size"]=1
            if kind=="duplicate":value["members"][1]["thread"]=0
            if kind=="mask":value["members"][1]["affinity"]=[2]
            if kind=="cpu":value["members"][1]["cpu"]=0
            if kind=="place":value["members"][1]["place"]=-1
            if kind=="duplicate_place":value["members"][1]["place"]=0
            if kind=="unreadable":value["members"][1]["affinity_readable"]=False
            if kind=="bind":value["proc_bind"]=0
            with self.subTest(kind=kind),self.assertRaises(ValueError):
                module.parse_openmp_probe(json.dumps(value),2,{0,1})

    def test_oversubscribed_probe_rejected_before_exec(self):
        with self.assertRaises(ValueError):
            module.observe_openmp_probe("/not-an-executable",3,[0,1])

if __name__ == "__main__":
    unittest.main()
