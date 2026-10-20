"""Protect the explicitly limited IdealGas low-G regrid migration."""
import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "validation/gravity"))
from radial_1d import G, RHO, RadialCampaign


class StopBeforeSimulation(Exception):
    pass


class SimilarityContract(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="arch-radial-contract-")
        self.addCleanup(self.temp.cleanup)
        self.campaign = RadialCampaign("/not-launched/ARCH", Path(self.temp.name))
        self.campaign.run = Mock(side_effect=StopBeforeSimulation)

    def test_only_density_is_migrated_and_no_writable_G(self):
        before = dict(self.campaign.base)
        with self.assertRaises(StopBeforeSimulation):
            self.campaign.regrid_cycle("spherical")
        kwargs = self.campaign.run.call_args.kwargs
        self.assertNotIn("gravity_G", kwargs)
        self.assertTrue(math.isclose(G * kwargs["rho0"], 1e-20 * RHO, rel_tol=2e-16))
        self.assertEqual(kwargs["temperature0"], 1e9)
        self.assertEqual((kwargs["tmax"], kwargs["max_steps"]), (.1, 40))
        self.assertEqual((kwargs["refine_threshold"], kwargs["derefine_threshold"]), (.01, .005))
        self.assertEqual(before, self.campaign.base)

    def test_other_closures_and_physics_do_not_enter_simulation(self):
        original = dict(self.campaign.base)
        for key, value in [("eos_type", "helm"), ("network_name", "aprox13"),
                           ("use_burn", "true"), ("use_diffusion", "true")]:
            with self.subTest(key=key):
                self.campaign.base = original | {key: value}
                with self.assertRaisesRegex(RuntimeError, "similarity requires"):
                    self.campaign.regrid_cycle("spherical")
        self.campaign.run.assert_not_called()

    def test_changed_absolute_floor_cannot_silently_repair_migrated_sample(self):
        self.campaign.base["sml_rho"] = "1e-5"
        with self.assertRaisesRegex(RuntimeError, "density floor"):
            self.campaign.regrid_cycle("spherical")
        self.campaign.run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
