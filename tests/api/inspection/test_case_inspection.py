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
BASE = 'geometry=cartesian\nnblockx1=1\nnblockx2=0\nnblockx3=0\nnetwork_name=none\nx1_min=0\nx1_max=1\n'
EOS = f'eos_type=helmholtz\neos_table_path={ROOT}/EOS_toolkit/tables/helmholtz/helm_table.dat\nnetwork_name=aprox19\nxhe4=.4\nxc12=.6\nuse_burn=true\n'
CASES = {
    'Sod': ('simulation/Sod/Sod.cpp', ''),
    'CellularDet': ('simulation/Cellular/Cellular.cpp', EOS+'nblockx2=1\n'),
    'Gaussian': ('simulation/GaussianPulse/Gaussian.cpp', ''),
    'Sedov': ('simulation/Sedov/Sedov.cpp', ''),
    'RT': ('simulation/RTinstability/RT_instab.cpp', 'nblockx2=1\n'),
    'SmoothAdvection': ('simulation/SmoothAdvection/SmoothAdvection.cpp', ''),
    'ExternalGravity': ('simulation/ExternalGravity/ExternalGravity.cpp', 'gravity_type=external\n'),
    'DiffusionMode': ('simulation/DiffusionMode/DiffusionMode.cpp', 'use_diffusion=true\nuse_species_diff=true\n'),
    'BurnOneZone': ('simulation/BurnOneZone/BurnOneZone.cpp', EOS),
    'BurnGradient': ('simulation/BurnGradient/BurnGradient.cpp', EOS),
    'CooperativeHotspots': ('simulation/CooperativeHotspots/CooperativeHotspots.cpp', EOS+'nblockx2=1\nx1_max=128\nx2_max=128\n'),
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
        for case, (source, extra) in CASES.items():
            with self.subTest(case=case):
                obj = self.call(case, BASE+extra+'future_knob=7\ncompute_backend=cuda\n')
                self.assertEqual(obj['status'], 'ok')
                self.assertEqual(obj['execution']['cuda'], 'not_initialized')
                self.assertEqual(obj['state']['setup'], 'ready')
                self.assertEqual(obj['state']['grid']['hierarchy'], 'not_constructed')
                self.assertEqual(obj['capability']['compiledSourceSha256'], hashlib.sha256((ROOT/source).read_bytes()).hexdigest())
                self.assertEqual(obj['capability']['reviewedUnitEvidence'], 'current')
                self.assertFalse(obj['capability']['automaticExpressionInference'])
                metadata = obj['parameterMetadata']
                self.assertIn('future_knob', metadata['unobservedInputKeys'])
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
            obj = self.call('Sedov', BASE+f'nblockx2={int(dim>=2)}\nnblockx3={int(dim==3)}\n')
            p = next(p for p in obj['parameterMetadata']['parameters'] if p['key']=='explosion_energy')
            self.assertEqual(p['unit'], unit)
        obj = self.call('Gaussian', BASE+'geometry=spherical\nx1_min=.1\nx1_max=1\n')
        self.assertEqual(obj['data']['sampleCount'], 3)
        self.assertAlmostEqual(obj['data']['samples'][0]['cartesianPosition'][0], .325)

    def test_explicit_defaults_and_string_metadata(self):
        obj = self.call('Sod', BASE+'x_pos=.3\n')
        p = next(p for p in obj['parameterMetadata']['parameters'] if p['key']=='x_pos')
        self.assertEqual((p['effectiveValue'], p['defaultValue'], p['valueSource'], p['unit']), (.3,.5,'explicit','cm'))
        obj = self.call('Gaussian', BASE)
        p = next(p for p in obj['parameterMetadata']['parameters'] if p['key']=='network_name')
        self.assertEqual(p['unitEvidence']['status'], 'not-applicable')
        self.assertIsNone(p['unit'])

    def test_rejections_and_error_metadata(self):
        self.call('absent', BASE, 4)
        obj = self.call('Sod', BASE+'x_pos=2\n', 5)
        self.assertEqual(obj['state']['setup'], 'error')
        self.assertIn('x_pos', {p['key'] for p in obj['parameterMetadata']['parameters']})
        self.call('SmoothAdvection', BASE+'mode=1.2\n', 5)
        self.call('SmoothAdvection', BASE+'mode=1.0\n', 5)
        self.call('SmoothAdvection', BASE+'mode=1e30\n', 5)
        self.call('Sod', BASE, 2, '--samples', '10')
        self.call('Sod', BASE+'restart=true\nrestart_file=/absent\n', 4)
        self.call('Sod', BASE+'nblockx1=1.2\n', 3)

    def test_command_error_envelopes_and_direction_integer(self):
        kinds = {
            '--config-schema': ('configuration-schema', '2'),
            '--inspect-config': ('configuration-inspection', '2'),
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
                           input=BASE+'nblockx2=1\nshock_dir=1.2\n', capture_output=True, text=True, cwd=self.cwd)
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
