"""Exercise the real ARCH application boundary using only Python's stdlib."""
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ARCH = Path(sys.argv.pop(1)).resolve()
ROOT = Path(sys.argv.pop(1)).resolve()
BASE = (ROOT / 'simulation/Sod/Sod.par').read_bytes()
ENV = dict(os.environ, OMP_NUM_THREADS='1', CUDA_VISIBLE_DEVICES='')


def config(**overrides):
    return BASE + ('\n' + '\n'.join(f'{k} = {v}' for k, v in overrides.items()) + '\n').encode()


class PreviewContract(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='arch-preview-test-')
        self.addCleanup(self.tmp.cleanup)
        self.cwd = Path(self.tmp.name)

    def invoke(self, payload=BASE, *options, case='Sod', expected=0):
        before = sorted(p.relative_to(self.cwd) for p in self.cwd.rglob('*'))
        result = subprocess.run([str(ARCH), '--preview', case, '--config-stdin', *options],
                                input=payload, cwd=self.cwd, env=ENV, capture_output=True, timeout=60)
        self.assertEqual(result.returncode, expected, (result.stdout[:3000], result.stderr[:3000]))
        data = json.loads(result.stdout)
        self.assertEqual(data['schemaVersion'], '1.0')
        self.assertEqual(data['status'], 'ok' if expected == 0 else 'error')
        after = sorted(p.relative_to(self.cwd) for p in self.cwd.rglob('*'))
        self.assertEqual(before, after, 'Preview must not create directories or files')
        return data

    def test_capabilities(self):
        run = subprocess.run([str(ARCH), '--preview-capabilities'], cwd=self.cwd,
                             capture_output=True, check=True, timeout=15)
        caps = json.loads(run.stdout)
        self.assertEqual(caps['cases'], ['Sod'])
        self.assertEqual(caps['previewBackend'], 'cpu')
        self.assertFalse(caps['amrHierarchy'])
        self.assertEqual(caps['maxSamples'], 4096)
        self.assertEqual(list(self.cwd.iterdir()), [])

    def test_real_fields_identity_and_snapshot(self):
        payload = config(x1_min=-2, x1_max=6, x_pos=1, rho_left=2, p_left=3, u_left=.25,
                         rho_right=.4, p_right=.2, u_right=-.1, gamma=1.6)
        result = self.invoke(payload, '--samples', '128', '--request-id', '实验-"A"')
        self.assertEqual(result['identity']['configRevision'], hashlib.sha256(payload).hexdigest())
        self.assertEqual(result['identity']['requestId'], '实验-"A"')
        x = result['data']['axes'][0]['values']
        fields = {f['key']: f for f in result['data']['fields']}
        self.assertEqual(len(x), 128)
        for i, point in enumerate(x):
            self.assertAlmostEqual(point, -2 + 8 * (i + .5) / 128)
            rho, p, u = (2, 3, .25) if point < 1 else (.4, .2, -.1)
            self.assertEqual(fields['DENS']['values'][i], rho)
            self.assertAlmostEqual(fields['PRES']['values'][i], p)
            self.assertEqual(fields['VELX']['values'][i], u)
            self.assertAlmostEqual(fields['ENER']['values'][i], p / .6 + .5 * rho * u * u)
            self.assertAlmostEqual(fields['EINT']['values'][i], p / (.6 * rho))
        for field in fields.values():
            self.assertEqual(field['min'], min(field['values']))
            self.assertEqual(field['max'], max(field['values']))
            self.assertEqual(field['unit'], {'DENS':'g/cm^3','PRES':'erg/cm^3','TEMP':'K','VELX':'cm/s','ENER':'erg/cm^3','EINT':'erg/g'}[field['key']])
            self.assertTrue(all(math.isfinite(v) for v in field['values']))
        state = result['state']
        self.assertEqual(state['eos']['resolved'], 'ideal')
        self.assertEqual(state['eos']['status'], 'ready')
        self.assertEqual(state['species'], [{'index': 0, 'name': 'SodGas'}])
        self.assertEqual(state['grid']['axes'][0]['rootCells'], 128)
        self.assertEqual(state['grid']['axes'][0]['coordinateSpacing'], 8 / 128)
        self.assertEqual(state['amr']['initialRefinement'], 'not_executed')
        self.assertIsNone(state['amr']['actualHierarchy'])

    def test_unsaved_values_defaults_and_resolution_independence(self):
        original = self.cwd / 'original.par'
        original.write_bytes(BASE)
        self.invoke(config(x_pos=.2), '--samples', '17')
        self.assertEqual(original.read_bytes(), BASE)
        payload = b'\n'.join(line for line in BASE.splitlines() if not line.startswith(b'x_pos')) + b'\n'
        result = self.invoke(payload, '--samples', '2')
        fields = {f['key']: f['values'] for f in result['data']['fields']}
        self.assertEqual(fields['DENS'], [1, .125])
        self.assertEqual(result['state']['grid']['axes'][0]['rootCells'], 128)
        self.assertEqual(result['data']['sampling']['count'], 2)

    def test_cuda_request_is_not_a_device_requirement(self):
        result = self.invoke(config(compute_backend='cuda', cuda_device=999))
        self.assertEqual(result['execution']['previewBackend'], 'cpu')
        self.assertEqual(result['execution']['simulationReadiness'], 'not_checked')
        self.assertEqual(result['state']['computeBackendRequested'], 'cuda')

    def test_amr_reports_config_and_filtered_indicators_without_building_mesh(self):
        result = self.invoke(config(lrefinemax=3, refine_var='DENS,VELY,ENUC', use_burn='false'))
        amr = result['state']['amr']
        self.assertTrue(amr['enabled'])
        self.assertEqual(amr['maxLevel'], 3)
        self.assertEqual(amr['requestedIndicators'], 'DENS,VELY,ENUC')
        self.assertEqual(amr['parsedIndicators'], ['DENS'])
        self.assertEqual(amr['indicatorEvaluation'], 'not_executed')
        self.assertTrue(any(d['severity'] == 'warning' for d in result['diagnostics']))
        self.assertEqual(result['state']['grid']['hierarchy'], 'not_constructed')

    def test_errors_preserve_confirmed_state_and_never_fallback(self):
        for payload, case, status, stage in [
            (BASE, 'NoSuchCase', 4, 'support'),
            (config(nblockx2=1), 'Sod', 4, 'support'),
            (config(geometry='spherical'), 'Sod', 4, 'support'),
            (config(restart='true', restart_file='missing.h5'), 'Sod', 4, 'support'),
            (config(x_pos=2), 'Sod', 5, 'setup'),
            (config(x_pos='nan'), 'Sod', 3, 'configuration'),
            (config(nblockx1=0), 'Sod', 3, 'configuration'),
            (config(lrefinemax=16), 'Sod', 3, 'configuration'),
            (config(eos_type='unknown'), 'Sod', 5, 'eos'),
            (config(eos_type='helmholtz', eos_table_path='absent.dat'), 'Sod', 5, 'eos'),
            (config(eos_type='tabular', eos_table_path='absent.h5'), 'Sod', 5, 'eos'),
        ]:
            with self.subTest(case=case, payload=payload[-150:]):
                result = self.invoke(payload, case=case, expected=status)
                self.assertEqual(result['stage'], stage)
                self.assertIsNone(result['data'])
                self.assertEqual(result['state']['configuration'], 'parsed')
                self.assertEqual(result['identity']['configRevision'], hashlib.sha256(payload).hexdigest())
        result = self.invoke(config(restart='maybe'), expected=3)
        self.assertEqual(result['state']['configuration'], 'not_loaded')
        self.assertIsNone(result['state']['grid'])

    def test_input_bounds_encoding_and_options(self):
        for payload, options in [
            (b'', []), (b'#' * (1024 * 1024 + 1), []), (b'\xff', []), (BASE + b'\0', []),
            (BASE, ['--samples', '1']), (BASE, ['--samples', '4097']),
            (BASE, ['--samples', '12abc']), (BASE, ['--samples', '-2']),
            (BASE, ['--samples', '12', '--samples', '14']), (BASE, ['--unknown']),
            (BASE, ['--request-id', 'x' * 129]),
        ]:
            with self.subTest(options=options, size=len(payload)):
                self.invoke(payload, *options, expected=2)
        result = self.invoke(BASE, '--samples', '4096')
        self.assertEqual(len(result['data']['axes'][0]['values']), 4096)

    def test_killed_incomplete_input_has_no_outputs(self):
        proc = subprocess.Popen([str(ARCH), '--preview', 'Sod', '--config-stdin'],
                                cwd=self.cwd, env=ENV, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            proc.stdin.write(BASE[:30])
            proc.stdin.flush()
            proc.terminate()
            stdout, _ = proc.communicate(timeout=10)
            self.assertEqual(stdout, b'')
            self.assertEqual(list(self.cwd.iterdir()), [])
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.communicate()

    def test_truncated_unicode_logs_are_still_json(self):
        result = self.invoke(config(restart='true', restart_file='中' * 6000), expected=4)
        self.assertTrue(any(d['code'] == 'LOG_TRUNCATED' for d in result['diagnostics']))

    def test_real_helmholtz_loader_reports_its_source(self):
        table = ROOT / 'EOS_toolkit/tables/helmholtz/helm_table.dat'
        payload = config(eos_type='helmholtz', eos_table_path=str(table),
                         rho_left=1e7, rho_right=1e6, p_left=1e24, p_right=1e23)
        result = self.invoke(payload, '--samples', '2')
        eos = result['state']['eos']
        self.assertEqual(eos['resolved'], 'helmholtz')
        self.assertEqual(eos['sourceFingerprint'], hashlib.sha256(table.read_bytes()).hexdigest())

    @unittest.skipUnless(os.environ.get('ARCH_PREVIEW_SIMULATION_ORACLE') == '1' and shutil.which('h5dump'),
                         'production simulation oracle is opt-in (ARCH_PREVIEW_SIMULATION_ORACLE=1 and h5dump)')
    def test_matches_production_initial_output_at_root_cell_centers(self):
        # This separate oracle invokes ordinary ARCH intentionally. The preview
        # itself never reads plotfiles, invokes a Driver or creates these files.
        payload = config(nblockx1=2, max_blocks=4, tmax=0, max_steps=0,
                         out_dir='oracle', log_dir='oracle',
                         plt_variables='DENS,PRES,TEMP,VELX,ENER', x_pos=.37, u_left=.2)
        preview = self.invoke(payload, '--samples', '32')
        path = self.cwd / 'oracle.par'
        path.write_bytes(payload)
        run = subprocess.run([str(ARCH), 'Sod', str(path)], cwd=self.cwd,
                             env=ENV, capture_output=True, timeout=60)
        self.assertEqual(run.returncode, 0, (run.stdout[-4000:], run.stderr[-2000:]))
        plots = sorted((self.cwd / 'oracle').glob('*plt*.h5'))
        self.assertTrue(plots)
        fields = {f['key']: f['values'] for f in preview['data']['fields']}
        for key in ['DENS', 'PRES', 'TEMP', 'VELX', 'ENER']:
            raw = self.cwd / f'{key}.bin'
            subprocess.run(['h5dump', '-d', f'/Data/{key}', '-b', 'LE', '-o', str(raw),
                            str(plots[0])], check=True, capture_output=True, timeout=15)
            packed = raw.read_bytes()
            actual = struct.unpack('<' + 'd' * (len(packed) // 8), packed)
            self.assertEqual(len(actual), len(fields[key]))
            for left, right in zip(actual, fields[key]):
                self.assertTrue(math.isclose(left, right, rel_tol=2e-13, abs_tol=1e-14), (key, left, right))


if __name__ == '__main__':
    unittest.main()
