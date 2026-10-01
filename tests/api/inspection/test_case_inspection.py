"""Every built-in model uses the same CPU read/Init inspection boundary."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ARCH, ROOT = map(lambda p: Path(p).resolve(), sys.argv[1:3])
del sys.argv[1:3]
def edit(text, **values):
    lines = [line for line in text.splitlines()
             if line.split('#', 1)[0].split('=', 1)[0].strip() not in values]
    return '\n'.join(lines) + '\n' + ''.join(
        f'{key}={value}\n' for key, value in values.items() if value is not None)


def load(path):
    return (ROOT/path).read_text().replace('EOS_toolkit/tables/helmholtz/helm_table.dat',
                                          str(ROOT/'EOS_toolkit/tables/helmholtz/helm_table.dat'))


BASE = load('simulation/Sod/Sod.par')
# Complete common controls; case-specific Sod inputs do not leak into another model.
COMMON = edit(BASE, **{key: None for key in
              ['x_pos', 'rho_left', 'p_left', 'u_left', 'rho_right', 'p_right', 'u_right']})
BURN = load('validation/burn/inputs/bd-config-v3.par')
CASES = {
    'Sod': ('simulation/Sod/Sod.cpp', BASE),
    'CellularDet': ('simulation/Cellular/Cellular.cpp', load('simulation/Cellular/CellularPreview2D.par')),
    'Gaussian': ('simulation/GaussianPulse/Gaussian.cpp', load('simulation/GaussianPulse/Gaussian.par')),
    'Sedov': ('simulation/Sedov/Sedov.cpp', load('simulation/Sedov/Sedov.par')),
    'RT': ('simulation/RTinstability/RT_instab.cpp', load('simulation/RTinstability/RT_instab.par')),
    'GravityBox': ('simulation/GravityBox/GravityBox.cpp', load('simulation/GravityBox/GravityBox.par')),
    'JeansWave': ('simulation/JeansWave/JeansWave.cpp', load('simulation/JeansWave/JeansWave.par')),
    'CooperativeHotspots': ('simulation/CooperativeHotspots/CooperativeHotspots.cpp',
                            load('simulation/CooperativeHotspots/CooperativeHotspots.par')),
    'SNIaCoupled': ('simulation/SNIaCoupled/SNIaCoupled.cpp',
                    load('simulation/SNIaCoupled/SNIaCoupled_2d_cartesian.par')),
    # Explicit values formerly implicit in these inspection fixtures' Setup reads.
    'SmoothAdvection': ('simulation/SmoothAdvection/SmoothAdvection.cpp',
                        edit(COMMON, rho_mean=1, rho_amplitude=.2, pressure0=1, velocity0=1, mode=1)),
    'ExternalGravity': ('simulation/ExternalGravity/ExternalGravity.cpp',
                        edit(COMMON, gravity_type='external', gravity_g_x=0, gravity_g_y=0,
                             gravity_g_z=0, rho0=1, pressure0=1, velocity_x0=0)),
    'DiffusionMode': ('simulation/DiffusionMode/DiffusionMode.cpp',
                      edit(COMMON, use_diffusion='true', use_thermal_diff='false',
                           use_viscous_diff='false', use_species_diff='true',
                           diff_integrator='RKL2', diff_cfl=.8, D_spec=0,
                           rho0=1, pressure0=1, tracer_mean=.5, tracer_amplitude=.25, mode=1)),
    'BurnOneZone': ('simulation/BurnOneZone/BurnOneZone.cpp', BURN),
    'BurnGradient': ('simulation/BurnGradient/BurnGradient.cpp',
                     edit(BURN, temperature0=None, x1_max=1, background_temperature=5e7,
                          peak_temperature=3e9, center_x=.5, width=.08)),
}

class Inspection(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='arch-inspection-')
        self.addCleanup(self.tmp.cleanup)
        self.cwd = Path(self.tmp.name)

    def call(self, case, text, code=0, *opts):
        before = sorted(self.cwd.rglob('*'))
        p = subprocess.run([str(ARCH), '--inspect-case', case, '--config-stdin', '--request-id', 'inspection', *opts],
                           input=text, text=True, capture_output=True, timeout=360, cwd=self.cwd,
                           env=dict(os.environ, OMP_NUM_THREADS='1', CUDA_VISIBLE_DEVICES=''))
        self.assertEqual(p.returncode, code, (p.stdout[-6000:], p.stderr))
        self.assertEqual(before, sorted(self.cwd.rglob('*')), 'inspection must not create scientific output')
        obj = json.loads(p.stdout)
        self.assertEqual(obj['kind'], 'case-inspection')
        if obj.get('identity'):
            self.assertEqual(obj['identity']['configRevision'], hashlib.sha256(text.encode()).hexdigest())
        return obj

    def test_all_registered_models(self):
        registry = json.loads(subprocess.check_output([str(ARCH), '--list-cases'], text=True, cwd=self.cwd))
        self.assertEqual({x['caseId'] for x in registry['cases']}, set(CASES), 'new built-in cases need an explicit coverage fixture')
        for case, (source, text) in CASES.items():
            with self.subTest(case=case):
                obj = self.call(case, edit(text, compute_backend='cuda'))
                self.assertEqual(obj['status'], 'ok')
                self.assertEqual(obj['execution']['cuda'], 'not_initialized')
                self.assertEqual(obj['state']['setup'], 'ready')
                self.assertEqual(obj['state']['grid']['hierarchy'], 'not_constructed')
                self.assertEqual(obj['capability']['compiledSourceSha256'], hashlib.sha256((ROOT/source).read_bytes()).hexdigest())
                self.assertEqual(obj['capability']['reviewedUnitEvidence'], 'current')
                self.assertFalse(obj['capability']['automaticExpressionInference'])
                metadata = obj['parameterMetadata']
                self.assertNotIn('future_knob', metadata['unobservedInputKeys'])
                self.assertFalse(metadata['complete'])
                self.assertGreater(len(metadata['parameters']), 0)
                for p in metadata['parameters']:
                    if p['type'] in ('float', 'int'):
                        self.assertIn(p['unitEvidence']['status'], ('known', 'dimensionless'), p)
                        self.assertIsNotNone(p['unit'])
                    self.assertNotEqual(p['valueSource'], 'unknown', p)
                self.assertEqual(obj['data']['sampleCount'], 3**obj['state']['grid']['dimension'])
                for sample in obj['data']['samples']:
                    fields = {f['key']:f for f in sample['fields']}
                    self.assertGreater(fields['DENS']['value'], 0)
                    self.assertEqual(fields['PRES']['unit'], 'erg/cm^3')
                if case == 'CellularDet':
                    by_key = {p['key']:p for p in metadata['parameters']}
                    self.assertEqual(by_key['xc12']['effectiveValue'], .6)
                    self.assertEqual(by_key['xc12']['unitEvidence']['basis'], 'core-composition-input-before-normalization')

    def test_dimension_dependent_units_and_curvilinear_points(self):
        for dim, unit in [(1, 'erg/cm^2'), (2, 'erg/cm'), (3, 'erg')]:
            obj = self.call('Sedov', edit(CASES['Sedov'][1], nblockx2=int(dim>=2), nblockx3=int(dim==3)))
            p = next(p for p in obj['parameterMetadata']['parameters'] if p['key']=='explosion_energy')
            self.assertEqual(p['unit'], unit)
        obj = self.call('Gaussian', edit(CASES['Gaussian'][1], geometry='spherical', nblockx2=0, x1_min=.1, x1_max=1))
        self.assertEqual(obj['data']['sampleCount'], 3)
        self.assertAlmostEqual(obj['data']['samples'][0]['cartesianPosition'][0], .325)

    def test_explicit_defaults_and_string_metadata(self):
        obj = self.call('Sod', edit(BASE, x_pos=.3))
        p = next(p for p in obj['parameterMetadata']['parameters'] if p['key']=='x_pos')
        self.assertEqual((p['effectiveValue'], p['defaultValue'], p['valueSource'], p['unit']), (.3,.5,'explicit','cm'))
        obj = self.call('CooperativeHotspots', CASES['CooperativeHotspots'][1])
        p = next(p for p in obj['parameterMetadata']['parameters'] if p['key']=='hotspot_mode')
        self.assertEqual(p['unitEvidence']['status'], 'not-applicable')
        self.assertIsNone(p['unit'])

    def test_rejections_and_error_metadata(self):
        self.call('absent', BASE, 3)
        obj = self.call('Sod', edit(BASE, x_pos=2), 5)
        self.assertEqual(obj['state']['setup'], 'error')
        self.assertIn('x_pos', {p['key'] for p in obj['parameterMetadata']['parameters']})
        self.call('SmoothAdvection', edit(CASES['SmoothAdvection'][1], mode='1.2'), 3)
        self.call('SmoothAdvection', edit(CASES['SmoothAdvection'][1], mode='1.0'), 3)
        self.call('SmoothAdvection', edit(CASES['SmoothAdvection'][1], mode='1e30'), 3)
        self.call('Sod', BASE+'future_knob=7\n', 3)
        self.call('Sod', BASE, 2, '--samples', '10')
        self.call('Sod', edit(BASE, restart='true', restart_file='/absent'), 4)
        self.call('Sod', edit(BASE, nblockx1='1.2'), 3)

    def test_command_error_envelopes_and_direction_integer(self):
        kinds = {
            '--config-schema': ('configuration-schema', '3'),
            '--inspect-config': ('configuration-inspection', '3'),
            '--list-cases': ('registered-cases', '1'),
            '--preview-capabilities': ('preview-capabilities', '1'),
            '--amr-resources': ('amr-resource-estimate', '1'),
            '--preview-amr': ('initial-amr-preview', '1'),
            '--inspect-case': ('case-inspection', '1'),
        }
        for flag, (kind, version) in kinds.items():
            p = subprocess.run([str(ARCH), flag, 'unexpected'], capture_output=True, text=True, cwd=self.cwd)
            self.assertEqual(p.returncode, 2)
            obj = json.loads(p.stdout)
            self.assertEqual((obj['kind'], obj['version']), (kind, version))
        p = subprocess.run([str(ARCH), '--preview', 'CellularDet', '--config-stdin'],
                           input=edit(CASES['CellularDet'][1], shock_dir='1.2'), capture_output=True, text=True, cwd=self.cwd)
        self.assertEqual(p.returncode, 3)
        self.assertEqual(json.loads(p.stdout)['stage'], 'configuration')

    def test_log_domain_keeps_zero_data(self):
        p = subprocess.run([str(ARCH), '--preview', 'Sod', '--config-stdin', '--samples', '8'],
                           input=BASE, text=True, capture_output=True, cwd=self.cwd, timeout=45)
        self.assertEqual(p.returncode, 0, p.stdout)
        fields = {f['key']:f for f in json.loads(p.stdout)['data']['fields']}
        velocity = fields['VELX']
        self.assertEqual(velocity['values'], [0]*8)
        self.assertEqual(velocity['logDomain']['zeroCount'], 8)
        self.assertEqual(velocity['logDomain']['negativeCount'], 0)
        self.assertFalse(velocity['logDomain']['canLog'])
        self.assertTrue(fields['DENS']['logDomain']['canLog'])

if __name__ == '__main__':
    unittest.main()
