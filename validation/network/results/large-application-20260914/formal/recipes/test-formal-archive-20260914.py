"""Check postprocessing gates against an already archived formal record."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('archive_formal',ROOT/'build/archive-formal-timing-20260914.py')
archive=importlib.util.module_from_spec(spec)
spec.loader.exec_module(archive)
SOURCE=ROOT/'validation/backend/results/hpc-cuda-optimization/P12-kernel-batch-20260914/timing/formal-diffusion_rkl1-v1.json'

class FormalArchiveGateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls): cls.original=json.loads(SOURCE.read_text(encoding='utf-8'))

    def test_real_complete_record(self): archive.check_report(self.original,False)

    def test_incomplete_failed_pilot_and_tampered_samples_rejected(self):
        mutations=[lambda r:r.update(status='running'),lambda r:r.update(pilot=True),
            lambda r:r.update(identity_error='changed'),lambda r:r['lanes'].pop(),
            lambda r:r['cases'].pop(),lambda r:r['comparisons'].pop(),lambda r:r['statistics'].pop(),
            lambda r:r['lanes'][0].update(status='failed'),lambda r:r['lanes'][0].update(returncode=1),
            lambda r:r['lanes'][0].update(timed_out=True),
            lambda r:r['comparisons'][0].update(workload_aligned=False),
            lambda r:r['comparisons'][0]['fields'].update(passed=False),
            lambda r:r['statistics'][0].update(median=-1),
            lambda r:r['statistics'][0]['samples'].__setitem__(0,0),
            lambda r:r['lanes'][0].update(repeat=8),
            lambda r:r['statistics'].__setitem__(1,copy.deepcopy(r['statistics'][0]))]
        for i,mutate in enumerate(mutations):
            with self.subTest(mutation=i):
                r=copy.deepcopy(self.original)
                mutate(r)
                with self.assertRaises(AssertionError): archive.check_report(r,False)

if __name__=='__main__': unittest.main()
