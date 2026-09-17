"""Synthetic protocol checks only; not CUDA/physics qualification."""
import importlib.util
from pathlib import Path
import unittest

PATH = Path(__file__).resolve().parents[2] / 'validation/network/native-wave-candidate/launch-shape/run_diagnostic.py'
spec = importlib.util.spec_from_file_location('shape_diagnostic', PATH)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def sample(network=150, method='bd', threads=32):
    mid, index = module.METHODS[method]
    index += 3 if network == 200 else 0
    lines = [f'controls,custom:audit{network},{network+1},10000000,3000000000,1e-10,100000000,1e-7,4,selected_ode,{mid}',
             'storage_controls,32,33,32']
    for storage in (32, 33):
        for step in range(4):
            lines.extend(f'{kind},{mid},{storage},{step},10,1,0.5' for kind in ('cpu_step', 'gpu_step'))
    lines.extend([f'metrics,{mid},100,10,1e-14,1e-15,6e-5,32,116676', 'GENERATED_SPARSE_BURN_PARITY_PASS'])
    stderr = (f'ADVANCE_SHAPE_DIAGNOSTIC index={index} calls=100 errors=0 max_cells=32 '
              f'max_blocks={32//threads} threads={threads} kernel=_NetCustom_audit{network}\n'
              'ADVANCE_SHAPE_DIAGNOSTIC_END matched_launches=100 scope=not_formal_performance\n')
    return '\n'.join(lines), stderr


class ShapeDiagnosticTests(unittest.TestCase):
    def test_all_protocol_routes(self):
        for n in (150, 200):
            for method in module.METHODS:
                for threads in (1, 32):
                    self.assertTrue(module.validate(*sample(n, method, threads), n, method, threads)['numerical_pass'])

    def test_wrong_or_unexecuted_shape(self):
        out, err = sample()
        for bad in ('', err.replace('calls=100', 'calls=0'), err.replace('errors=0', 'errors=1'),
                    err.replace('max_blocks=1', 'max_blocks=32'), err.replace('index=1', 'index=4')):
            with self.assertRaises(ValueError):
                module.validate(out, bad, 150, 'bd', 32)

    def test_original_numerical_controls_and_completion(self):
        out, err = sample()
        for bad in (out.replace('1e-10', '1e-9'), out.replace('selected_ode,2', 'selected_ode,3'),
                    out.replace('storage_controls,32,33,32', 'storage_controls,2,3,2'),
                    out.replace('GENERATED_SPARSE_BURN_PARITY_PASS', ''),
                    out.replace('1e-14', '3e-10'), out.replace('1e-15', '3e-8'),
                    out.replace('1e-14', 'nan'), out.replace('6e-5', '0')):
            with self.assertRaises(ValueError):
                module.validate(bad, err, 150, 'bd', 32)

    def test_missing_duplicate_or_wrong_steps(self):
        out, err = sample()
        for bad in (out.replace('gpu_step,2,33,3,10,1,0.5', ''),
                    out + '\ngpu_step,2,33,3,10,1,0.5', out.replace('cpu_step,2,32,0', 'cpu_step,3,32,0')):
            with self.assertRaises(ValueError):
                module.validate(bad, err, 150, 'bd', 32)

    def test_abba_and_immutable_physical_arguments(self):
        self.assertEqual(module.THREADS, (32, 1, 1, 32))
        self.assertEqual(module.arguments('/test', 'bd'),
            ['/test', '1e7', '3e9', '1e-10', '1e8', '1e-7', '4', '--ode', 'bd',
             '--storage-cells', '32', '33', '--pool-cells', '32', 'c12=0.5', 'o16=0.5'])


if __name__ == '__main__':
    unittest.main()
