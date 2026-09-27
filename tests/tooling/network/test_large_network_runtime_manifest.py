import copy
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]


class LargeNetworkRuntimeManifestTests(unittest.TestCase):
    def test_original_application_protocol_is_preserved(self):
        original = json.loads((ROOT/'validation/network/runtime_cases.json').read_text())['cases']
        large = json.loads((ROOT/'validation/network/large_runtime_cases.json').read_text())['cases']
        controls = [c for c in original if c['id'].startswith('generated_audit31_')]
        self.assertEqual(len(controls),3)
        self.assertEqual(len(large),6)
        expected = []
        for network in ('audit150','audit200'):
            for c in controls:
                c = copy.deepcopy(c)
                c['id'] = c['id'].replace('audit31',network)
                c['plan_policy']['network'] = 'custom:'+network
                c['overrides']['network_name'] = 'custom:'+network
                c['timeout_seconds'] = 3600
                expected.append(c)
        self.assertEqual(large,expected)
        for c in large:
            self.assertEqual(c['plan_policy']['linear'],{'cpu':'sparseklu','cuda':'cudss'})
            self.assertEqual(c['scientific_time'],1e-10)
            self.assertEqual(c['accepted_steps'],[1,2,5])


if __name__ == '__main__': unittest.main()
