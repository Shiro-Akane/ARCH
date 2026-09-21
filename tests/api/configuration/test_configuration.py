"""Real CPU configuration contract: no Setup, EOS I/O, simulation, or file writes."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ARCH = Path(sys.argv.pop(1)).resolve()
ROOT = Path(sys.argv.pop(1)).resolve()
ENV = dict(os.environ, OMP_NUM_THREADS='1', CUDA_VISIBLE_DEVICES='')

class ConfigurationContract(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='arch-config-contract-')
        self.addCleanup(self.tmp.cleanup)
        self.cwd = Path(self.tmp.name)

    def run_api(self, args, payload=b'', code=0):
        before = list(self.cwd.rglob('*'))
        run = subprocess.run([str(ARCH), *args], input=payload, capture_output=True,
                             cwd=self.cwd, env=ENV, timeout=30)
        self.assertEqual(run.returncode, code, (run.stdout[:5000], run.stderr[:1000]))
        self.assertEqual(before, list(self.cwd.rglob('*')), 'Configuration API wrote files')
        self.assertLessEqual(len(run.stdout), 8*1024*1024)
        return json.loads(run.stdout)

    def inspect(self, text='', code=0, case='Sod'):
        payload = text.encode()
        out = self.run_api(['--inspect-config', case, '--config-stdin', '--request-id', '配置-A'], payload, code)
        self.assertEqual(out['kind'], 'configuration-inspection')
        self.assertEqual(out['identity']['configRevision'], hashlib.sha256(payload).hexdigest())
        self.assertEqual(out['execution']['setup'], 'not_executed')
        self.assertEqual(out['execution']['eos'], 'not_loaded')
        self.assertEqual(out['execution']['cuda'], 'not_initialized')
        return out

    def test_catalog_is_available_before_any_configuration(self):
        schema = self.run_api(['--config-schema'])
        self.assertEqual(schema['kind'], 'configuration-schema')
        self.assertTrue(schema['standardParametersComplete'])
        self.assertFalse(schema['customParametersComplete'])
        specs = {p['key']: p for p in schema['parameters']}
        keys = set(re.findall(r'parser\.Get(?:Int|Double|String|Bool)\(\s*"([^"]+)"',
                              (ROOT/'src/core/config/RuntimeParams.h').read_text()))
        self.assertEqual(set(specs), keys)
        self.assertEqual(len(specs), 90)
        self.assertEqual({p['group'] for p in specs.values()}, {'Grid','EOS','Network','Gravity','Diffusion','Runtime'})
        self.assertEqual(specs['ode_rtol']['defaultValue'], 1e-4)
        self.assertEqual(specs['ode_atol']['defaultValue'], 1e-8)
        self.assertEqual(specs['ode_max_substeps']['defaultValue'], 10000)
        self.assertEqual(specs['ode_initial_dt_frac']['defaultValue'], .001)
        self.assertEqual(specs['timeintegrator']['aliasOf'], 'time_integrator')
        self.assertEqual(specs['use_nse']['defaultValue'], 'true')
        self.assertEqual(specs['gravity_G']['defaultValue'], 6.67430e-8)
        self.assertTrue(any(p['value']=='auto' for p in specs['linear_solver']['options']['choices']))
        self.assertTrue(any(p['value']=='aprox19' for p in specs['network_name']['options']['choices']))
        self.assertEqual(specs['gravity_type']['options']['unavailableValues'], ['self'])
        caps = self.run_api(['--preview-capabilities'])
        self.assertEqual(caps['extensions']['configuration']['standardParameterCount'], 90)
        self.assertEqual(caps['cases'], ['Sod'])

    def test_defaults_and_explicit_values_with_no_eos_or_device_access(self):
        result = self.inspect('eos_type=helmholtz\neos_table_path=/not present/absent.dat\ncompute_backend=cuda\ncuda_device=999\node_rtol=2e-5\nx_pos=0.2suffix\n')
        self.assertEqual(result['status'], 'ok')
        parameters = {p['key']: p for p in result['parameters']}
        self.assertEqual(parameters['ode_rtol']['valueSource'], 'explicit')
        self.assertEqual(parameters['ode_rtol']['parsedValue'], 2e-5)
        self.assertEqual(parameters['ode_max_substeps']['parsedValue'], 10000)
        self.assertEqual(parameters['ode_max_substeps']['valueSource'], 'default')
        self.assertFalse(parameters['ode_rtol']['applicable'])
        self.assertEqual(parameters['eos_table_path']['path']['relativeTo'], 'process-working-directory')
        self.assertFalse(parameters['eos_table_path']['path']['existenceChecked'])
        self.assertTrue(parameters['out_dir']['path']['targetMayBeNew'])
        self.assertIsNone(parameters['network_name']['path'])
        self.assertEqual(result['customParameters'], [{'key':'x_pos','rawValue':'0.2suffix','type':None,'unit':None,'status':'uninspected-model-parameter'}])
        self.assertEqual(result['unitSystem'], 'cgs')
        empty = self.inspect()
        self.assertEqual(empty['resolved']['dimension'], 3)
        self.assertEqual(len(empty['parameters']), 90)

    def test_integer_tokens_reject_fractions_suffixes_and_overflow(self):
        for token in ['1.5','1.0','1e2','12suffix','2147483648','-2147483649','+-1','', 'nan']:
            with self.subTest(token=token):
                result = self.inspect(f'nblockx2={token}\n', 3)
                self.assertTrue(any(d['parameterKey']=='nblockx2' and d['code']=='INVALID_INTEGER' for d in result['diagnostics']))
        result = self.inspect('nblockx1=+1\nnblockx2=1\nnblockx3=0\n')
        self.assertEqual(result['resolved']['dimension'], 2)
        self.assertEqual(result['coordinates']['axes'][1]['blocks'], 1)
        result = self.inspect('nblockx2=0\nnblockx3=1', 3)
        self.assertEqual(result['diagnostics'][-1]['parameterKey'], 'nblockx3')

    def test_float_and_expression_validation(self):
        for token in ['NaN','inf','-Infinity','1e999','1e-9999','1.2suffix','+-2','']:
            with self.subTest(token=token):
                out = self.inspect(f'ode_rtol={token}', 3)
                self.assertTrue(any(d['parameterKey']=='ode_rtol' for d in out['diagnostics']))
        for token in ['pi/0','2*pi*garbage','garbage','1.0suffix','pi*1e999']:
            with self.subTest(token=token):
                out = self.inspect(f'x1_max={token}', 3)
                self.assertTrue(any(d['parameterKey']=='x1_max' for d in out['diagnostics']))
        result = self.inspect('x1_max=2 * pi\nuse_diffusion=FaLsE\n')
        p = {p['key']:p for p in result['parameters']}
        self.assertAlmostEqual(p['x1_max']['parsedValue'], 6.283185307179586)
        self.assertFalse(p['use_diffusion']['parsedValue'])

    def test_alias_and_canonical_precedence(self):
        r = self.inspect('timeintegrator=RK3')
        p = {p['key']:p for p in r['parameters']}
        self.assertEqual(p['time_integrator']['valueSource'], 'alias')
        self.assertEqual(p['time_integrator']['sourceKey'], 'timeintegrator')
        self.assertEqual(r['resolved']['timeIntegrator'], 'RK3')
        r = self.inspect('timeintegrator=RK3\ntime_integrator=RK2')
        p = {p['key']:p for p in r['parameters']}
        self.assertEqual(r['resolved']['timeIntegrator'], 'RK2')
        self.assertFalse(p['timeintegrator']['applicable'])

    def test_geometry_and_units_in_all_dimensions(self):
        names = {'cartesian': {1:['x'],2:['x','y'],3:['x','y','z']},
                 'cylindrical': {1:['r'],2:['r','phi'],3:['r','z','phi']},
                 'spherical': {1:['r'],2:['r','phi'],3:['r','theta','phi']}}
        for geometry, dims in names.items():
            for dim, labels in dims.items():
                with self.subTest(geometry=geometry, dim=dim):
                    r = self.inspect(f'geometry={geometry}\nnblockx2={int(dim>=2)}\nnblockx3={int(dim==3)}\neos_type=helmholtz')
                    axes=r['coordinates']['axes']
                    self.assertEqual([a['displayName'] for a in axes if a['active']], labels)
                    self.assertEqual([a['key'] for a in axes], ['x1','x2','x3'])
                    self.assertEqual([a['unit'] for a in axes if a['active']], ['rad' if x in ['phi','theta'] else 'cm' for x in labels])
                    self.assertTrue(all(a['unit'] is None for a in axes if not a['active']))
        r=self.inspect('nblockx2=0\nnblockx3=0')
        self.assertEqual(r['unitSystem'], 'cgs')
        self.assertEqual(r['coordinates']['axes'][0]['unit'], 'cm')

    def test_constraints_and_forbidden_diffusion_are_structured(self):
        for text,key in [('nblockx2=-1\nnblockx3=0','nblockx2'),
                         ('x1_min=2\nx1_max=1','x1_max'),
                         ('lrefinemax=16','lrefinemax'),
                         ('eos_type=helmholtz\nuse_diffusion=true\nalpha_therm=0','alpha_therm'),
                         ('eos_type=unknown','eos_type'),
                         ('gravity_type=invalid','gravity_type'),
                         ('x1l_boundary_type=invalid','x1l_boundary_type')]:
            with self.subTest(text=text):
                out=self.inspect(text,3)
                self.assertTrue(any(d['severity']=='error' and d['parameterKey']==key for d in out['diagnostics']), out['diagnostics'])
        out=self.inspect('solver=unknown')
        self.assertTrue(any(d['code']=='POLICY_FALLBACK' for d in out['diagnostics']))
        out=self.inspect('gravity_type=self')
        self.assertTrue(any(d['code']=='UNAVAILABLE_MODULE' for d in out['diagnostics']))

    def test_request_encoding_limits_and_modes(self):
        for payload in [b'\xff',b'\0',b'#'*(1024*1024+1)]:
            out=self.run_api(['--inspect-config','Sod','--config-stdin'],payload,2)
            self.assertEqual(out['kind'],'configuration-inspection')
        out=self.run_api(['--inspect-config','Sod','--config-stdin','--samples','4'],b'',2)
        self.assertEqual(out['diagnostics'][0]['code'],'INVALID_REQUEST')
        self.assertEqual(self.run_api(['--config-schema','unexpected'],code=2)['kind'],'configuration-schema')

    def test_preview_uses_same_strict_standard_parser(self):
        base=(ROOT/'simulation/Sod/Sod.par').read_bytes()
        for key,value in [('nblockx1','1.5'),('ode_rtol','NaN'),('gravity_g_x','2junk')]:
            out=self.run_api(['--preview','Sod','--config-stdin'],base+f'\n{key}={value}\n'.encode(),3)
            self.assertEqual(out['diagnostics'][0]['parameterKey'],key)
            self.assertIsNone(out['data'])

    def test_large_custom_metadata_has_a_bounded_error_response(self):
        payload = ''.join(f'p{i}=1\n' for i in range(90000)).encode()
        self.assertLess(len(payload), 1024*1024)
        out = self.run_api(['--inspect-config','Sod','--config-stdin'], payload, 7)
        self.assertEqual(out['kind'], 'configuration-inspection')
        self.assertEqual(out['diagnostics'][0]['code'], 'RESPONSE_TOO_LARGE')
        self.assertNotIn('customParameters', out)

if __name__ == '__main__':
    unittest.main()
