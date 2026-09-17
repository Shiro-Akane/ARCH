"""Synthetic Host-observer protocol checks, not GPU validation."""
import importlib.util
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'validation/network/native-wave-candidate'
sys.path.insert(0, str(BASE / 'batched-kernels'))
try:
    spec = importlib.util.spec_from_file_location('native_api_cost', BASE / 'api-cost/run_focused_cost.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
finally:
    sys.path.pop(0)


def observed():
    name = '_advance_ode_NetCustom_audit150_Solver_BE_NR'
    return ('\n'.join(f'NATIVE_OBSERVER,{phase},1,0.1' for phase in ('analysis', 'factorization', 'solve', 'stream_sync'))
            + f'\nCUDA_OBSERVER,launch:{name},100,0.1,0.01,0,100,3200'
            + f'\nCUDA_OBSERVER,memcpy_async:2:after:{name},100,0.2,0.01,1000,0,0')


class NativeApiCostTests(unittest.TestCase):
    def test_real_observations_required(self):
        result = module.observer_result(observed(), 150, 'be_nr')
        self.assertEqual(result['native']['factorization']['calls'], 1)
        self.assertEqual(result['scope'], 'nested-host-latency-not-additive-GPU-kernel-time')

    def test_missing_invalid_duplicate_or_wrong_shape_rejected(self):
        text = observed()
        for bad in ('', text.replace('NATIVE_OBSERVER,solve,1,0.1', ''),
                    text + '\nNATIVE_OBSERVER,solve,1,0.1', text.replace(',0,100,3200', ',0,100,100'),
                    text.replace('NetCustom_audit150', 'NetCustom_audit200'),
                    text.replace('Solver_BE_NR', 'Solver_BD'), text.replace(',1,0.1', ',0,0.1'),
                    text.replace(',1,0.1', ',1,nan'), text.replace('memcpy_async:2:after:', 'unknown:')):
            with self.assertRaises(ValueError):
                module.observer_result(bad, 150, 'be_nr')


if __name__ == '__main__':
    unittest.main()
