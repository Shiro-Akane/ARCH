"""Real CellularDet CPU initial-preview contract; never runs a simulation."""
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

ARCH = Path(sys.argv.pop(1)).resolve()
ROOT = Path(sys.argv.pop(1)).resolve()
REFERENCE = Path(sys.argv.pop(1)).resolve()
BASE = (ROOT / 'simulation/Cellular/CellularPreview2D.par').read_bytes()
ENV = dict(os.environ, OMP_NUM_THREADS='1', CUDA_VISIBLE_DEVICES='')
FIELDS = ['DENS', 'PRES', 'TEMP', 'VELX', 'ENER', 'EINT', 'VELY']


def config(**values):
    # Use the real reference EOS: nuclear species have zero IdealGas Cv.
    values = {'eos_table_path': ROOT / 'EOS_toolkit/tables/helmholtz/helm_table.dat', **values}
    return BASE + ('\n' + '\n'.join(f'{k} = {v}' for k, v in values.items()) + '\n').encode()


class CellularPreviewContract(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='arch-cellular-preview-')
        self.addCleanup(self.tmp.cleanup)
        self.cwd = Path(self.tmp.name)

    def invoke(self, payload, options=None, expected=0, case='CellularDet'):
        if options is None:
            options = ['--samples-x1', '5', '--samples-x2', '3']
        before = sorted(self.cwd.rglob('*'))
        run = subprocess.run([str(ARCH), '--preview', case, '--config-stdin', '--request-id', 'cellular-b', *options],
                             input=payload, cwd=self.cwd, env=ENV, capture_output=True, timeout=120)
        self.assertEqual(run.returncode, expected, (run.stdout[:1800], run.stderr[:1800]))
        self.assertLessEqual(len(run.stdout), 8 * 1024 * 1024)
        result = json.loads(run.stdout)
        self.assertEqual(result['schemaVersion'], '1.0')
        self.assertEqual(result['status'], 'ok' if expected == 0 else 'error')
        if result['identity'] is not None:
            self.assertEqual(result['identity']['requestId'], 'cellular-b')
            self.assertEqual(result['identity']['configRevision'], hashlib.sha256(payload).hexdigest())
        self.assertEqual(before, sorted(self.cwd.rglob('*')), 'Preview created files')
        return result

    def validate_data(self, result, nx, ny):
        data = result['data']
        self.assertEqual((data['dimension'], data['kind']), (2, 'grid'))
        self.assertEqual(data['sampling'], {'kind': 'uniform', 'valueLocation': 'init-sample',
            'position': 'bin-center', 'count': nx * ny, 'shape': [ny, nx], 'order': 'x1-fastest',
            'fixedCoordinates': [{'name': 'x3', 'value': 0, 'unit': None}]})
        for axis, name, n in zip(data['axes'], ['x1', 'x2'], [nx, ny]):
            self.assertEqual(axis['name'], name)
            self.assertEqual(len(axis['values']), n)
            self.assertTrue(all(math.isfinite(x) for x in axis['values']))
            self.assertTrue(all(a < b for a, b in zip(axis['values'], axis['values'][1:])))
        self.assertEqual([f['key'] for f in data['fields']], FIELDS)
        for field in data['fields']:
            self.assertEqual(len(field['values']), nx * ny)
            self.assertTrue(all(math.isfinite(x) for x in field['values']))
            self.assertEqual(field['min'], min(field['values']))
            self.assertEqual(field['max'], max(field['values']))
            self.assertIsNone(field['unit'])
        self.assertNotIn('parameterMetadata', result)
        self.assertNotIn('graphicalBindings', result)

    def test_capabilities_are_per_model_and_keep_legacy_sod(self):
        run = subprocess.run([str(ARCH), '--preview-capabilities'], check=True, capture_output=True, timeout=15)
        caps = json.loads(run.stdout)
        self.assertEqual(caps['cases'], ['Sod'])
        models = {m['caseId']: m for m in caps['modelCapabilities']}
        self.assertEqual(set(models), {'Sod', 'CellularDet'})
        model = models['CellularDet']
        self.assertEqual(model['dimensions'], [2])
        self.assertEqual(model['geometries'], ['cartesian'])
        self.assertEqual(model['supportedShockDirections'], [0, 1])
        self.assertEqual(model['fields'], FIELDS)
        self.assertEqual(model['maxFields'], 7)
        self.assertEqual(model['sampling'], {'defaultShape': [128, 128], 'minPerAxis': 2,
                                            'maxPerAxis': 256, 'maxTotalSamples': 65536})
        self.assertEqual(model['maxResponseBytes'], 8 * 1024 * 1024)

    def test_non_square_fields_match_direct_real_init_both_directions(self):
        for direction in [0, 1]:
            payload = config(shock_dir=direction, radiusPerturb=5.7, x1_min=-2, x1_max=22,
                             x2_min=1, x2_max=13, x3_min=50, x3_max=70)
            result = self.invoke(payload)
            self.validate_data(result, 5, 3)
            run = subprocess.run([str(REFERENCE), '5', '3'], input=payload, capture_output=True,
                                 env=ENV, cwd=self.cwd, check=True, timeout=120)
            expected = json.loads(run.stdout)
            fields = {f['key']: f['values'] for f in result['data']['fields']}
            for j in range(3):
                for i in range(5):
                    row = expected[j * 5 + i]
                    self.assertAlmostEqual(result['data']['axes'][0]['values'][i], row[0])
                    self.assertAlmostEqual(result['data']['axes'][1]['values'][j], row[1])
                    self.assertEqual(row[2], 0)
                    for key, wanted in zip(FIELDS, row[3:]):
                        self.assertTrue(math.isclose(fields[key][j * 5 + i], wanted, rel_tol=2e-12, abs_tol=1e-12),
                                        (direction, i, j, key, fields[key][j * 5 + i], wanted))

    def test_noise_changes_interior_fields_and_not_interface_location(self):
        for direction in [0, 1]:
            runs = [self.invoke(config(shock_dir=direction, radiusPerturb=5.7, noiseAmplitude=n)) for n in [0, .02]]
            fields = [{f['key']: f['values'] for f in r['data']['fields']} for r in runs]
            self.assertEqual(fields[0]['VELX'], fields[1]['VELX'])
            self.assertEqual(fields[0]['VELY'], fields[1]['VELY'])
            velocity = fields[0]['VELX' if direction == 0 else 'VELY']
            outside = [k for k, v in enumerate(velocity) if v == 0]
            inside = [k for k, v in enumerate(velocity) if v != 0]
            self.assertTrue(outside and inside)
            for key in ['DENS', 'PRES']:
                self.assertTrue(all(fields[0][key][k] == fields[1][key][k] for k in outside))
                self.assertTrue(any(fields[0][key][k] != fields[1][key][k] for k in inside))

    def test_defaults_and_axis_limits_do_not_change_grid_configuration(self):
        payload = config(noiseAmplitude=0, velxPerturb=0)
        result = self.invoke(payload, [])
        self.validate_data(result, 128, 128)
        grid = result['state']['grid']
        for nx, ny in [(2, 2), (2, 256), (256, 256)]:
            result = self.invoke(payload, ['--samples-x1', str(nx), '--samples-x2', str(ny)])
            self.validate_data(result, nx, ny)
            self.assertEqual(result['state']['grid'], grid)

    def test_invalid_sampling_syntax_and_budgets(self):
        invalid = [['--samples', '10'], ['--samples-x1', '3'], ['--samples-x2', '3'],
                   ['--samples-x1', '3', '--samples-x2', '3', '--samples', '512'],
                   ['--samples-x1', '3', '--samples-x1', '4', '--samples-x2', '3'],
                   ['--fields', 'DENS']]
        invalid += [['--samples-x1', n, '--samples-x2', '3']
                    for n in ['0', '-1', '1', '257', '2147483647', '99999999999999999', '2.5', '2abc']]
        for options in invalid:
            with self.subTest(options=options):
                result = self.invoke(config(), options, expected=2)
                self.assertIsNone(result['data'])
        result = self.invoke(config(), ['--samples-x1', '257', '--samples-x2', '3'], expected=2)
        self.assertEqual(result['diagnostics'][0]['code'], 'SAMPLING_LIMIT_EXCEEDED')
        sod = (ROOT / 'simulation/Sod/Sod.par').read_bytes()
        self.invoke(sod, ['--samples-x1', '3', '--samples-x2', '3'], expected=2, case='Sod')

    def test_support_and_configuration_errors(self):
        for values, code, diagnostic in [
            ({'shock_dir': 2}, 4, 'UNSUPPORTED_PREVIEW'),
            ({'shock_dir': -1}, 4, 'UNSUPPORTED_PREVIEW'),
            ({'nblockx2': 0}, 4, 'UNSUPPORTED_PREVIEW'),
            ({'nblockx3': 1}, 4, 'UNSUPPORTED_PREVIEW'),
            ({'geometry': 'cylindrical'}, 4, 'UNSUPPORTED_PREVIEW'),
            ({'x2_max': 0}, 3, 'INVALID_CONFIGURATION'),
            ({'max_blocks': 1}, 3, 'INVALID_CONFIGURATION'),
            ({'shock_dir': '1e100'}, 3, 'INVALID_CONFIGURATION'),
            ({'noiseAmplitude': 'nan'}, 3, 'INVALID_CONFIGURATION'),
            ({'network_name': 'unknown'}, 5, 'SETUP_FAILED'),
        ]:
            with self.subTest(values=values):
                result = self.invoke(config(**values), expected=code)
                self.assertIsNone(result['data'])
                self.assertEqual(result['diagnostics'][0]['code'], diagnostic)

    def test_eos_and_sampling_failures_keep_confirmed_state(self):
        result = self.invoke(config(eos_type='helmholtz', eos_table_path='absent.dat'), expected=5)
        self.assertEqual(result['stage'], 'eos')
        self.assertEqual(result['diagnostics'][0]['code'], 'EOS_FAILED')
        self.assertEqual(result['state']['eos']['status'], 'error')
        self.assertEqual(len(result['state']['species']), 19)
        for values in [{'noiseAmplitude': 100}, {'velxPerturb': '1e308'}, {'eos_type': 'ideal'}]:
            result = self.invoke(config(**values), expected=6)
            self.assertEqual(result['stage'], 'sampling')
            self.assertIsNone(result['data'])
            self.assertEqual(result['state']['setup'], 'ready')

    def test_response_budget_preserves_request_and_state(self):
        payload = config(radiusPerturb=1e6, noiseAmplitude=.037531246897,
                         rhoPerturb=4.321234567891234e7, tempPerturb=4.670123456789e9,
                         velxPerturb=1.011234567891234e-101)
        result = self.invoke(payload, ['--samples-x1', '256', '--samples-x2', '256'], expected=7)
        self.assertEqual(result['stage'], 'response')
        self.assertEqual(result['diagnostics'][0]['code'], 'RESPONSE_TOO_LARGE')
        self.assertEqual(result['state']['setup'], 'ready')
        self.assertEqual(result['state']['eos']['status'], 'ready')
        self.assertIsNone(result['data'])
        self.validate_data(self.invoke(payload), 5, 3)

    def test_real_reference_helmholtz_default_grid_and_cpu_only(self):
        table = ROOT / 'EOS_toolkit/tables/helmholtz/helm_table.dat'
        payload = BASE + f'\neos_table_path = {table}\ncompute_backend = cuda\ncuda_device = 999\n'.encode()
        result = self.invoke(payload, [])
        self.validate_data(result, 128, 128)
        self.assertEqual(result['execution'], {'previewBackend': 'cpu', 'simulationReadiness': 'not_checked',
            'timeStepping': 'not_executed', 'scientificOutput': 'not_created'})
        state = result['state']
        self.assertEqual(state['computeBackendRequested'], 'cuda')
        self.assertEqual(state['eos']['resolved'], 'helmholtz')
        self.assertEqual(state['eos']['sourceFingerprint'], hashlib.sha256(table.read_bytes()).hexdigest())
        self.assertEqual(len(state['species']), 19)
        self.assertEqual(state['grid']['hierarchy'], 'not_constructed')
        self.assertIsNone(state['amr']['actualHierarchy'])

    def test_cancelled_request_leaves_no_output_files(self):
        for complete_input in [False, True]:
            proc = subprocess.Popen([str(ARCH), '--preview', 'CellularDet', '--config-stdin',
                                     '--samples-x1', '256', '--samples-x2', '256'],
                                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                    cwd=self.cwd, env=ENV)
            try:
                proc.stdin.write(config() if complete_input else BASE[:50])
                proc.stdin.flush()
                if complete_input:
                    proc.stdin.close()
                    proc.stdin = None
                    time.sleep(.05)
                proc.terminate()
                stdout, _ = proc.communicate(timeout=10)
                self.assertNotEqual(proc.returncode, 0)
                self.assertEqual(stdout, b'')
                self.assertEqual(list(self.cwd.iterdir()), [])
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.communicate()



if __name__ == '__main__':
    unittest.main()
