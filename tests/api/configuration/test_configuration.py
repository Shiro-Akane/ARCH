"""Real CPU configuration contract: no Setup, EOS I/O, simulation, or file writes."""
import hashlib
import json
import math
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
BASE = (ROOT/'simulation/Sod/Sod.par').read_text()

def overlay(text):
    # Tests edit a complete explicit fixture, never rely on runtime defaults.
    keys = {line.split('#', 1)[0].split('=', 1)[0].strip()
            for line in text.splitlines() if '=' in line}
    lines = [line for line in BASE.splitlines()
             if line.split('#', 1)[0].split('=', 1)[0].strip() not in keys]
    return '\n'.join(lines) + '\n' + text


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

    def inspect(self, text='', code=0, case='Sod', partial=False):
        payload = (text if partial else overlay(text)).encode()
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
        self.assertTrue(schema['caseDeclarationsComplete'])
        specs = {p['key']: p for p in schema['parameters']}
        keys = set(re.findall(r'ARCH_STANDARD_PARAMETER\("([^"]+)"',
                              (ROOT/'src/core/config/StandardParameterEntries.inc').read_text())) - {'gravity_G'}
        self.assertEqual(set(specs), keys)
        self.assertEqual(len(specs), len(keys))
        self.assertEqual({p['group'] for p in specs.values()}, {'Grid','EOS','Network','Gravity','Diffusion','Runtime'})
        jeans=specs['jeans_cells']
        self.assertIsNone(jeans['allowedDefault'])
        self.assertEqual(jeans['requirement']['kind'],'conditional')
        self.assertEqual(jeans['requirement']['condition']['dependencies'],['refine_var'])
        self.assertEqual(jeans['constraints']['min'],4)
        self.assertTrue(jeans['constraints']['minInclusive'])
        self.assertEqual(jeans['units']['unit'],'1')
        self.assertEqual(jeans['presentation']['subgroup'],'AMR')
        self.assertIsNone(specs['ode_rtol']['allowedDefault'])
        self.assertIsNone(specs['ode_atol']['allowedDefault'])
        self.assertEqual(specs['ode_max_substeps']['allowedDefault']['value'], 10000)
        self.assertEqual(specs['ode_initial_dt_frac']['allowedDefault']['value'], 1.0)
        self.assertNotIn('timeintegrator', specs)
        self.assertIsNone(specs['use_nse']['allowedDefault'])
        self.assertNotIn('gravity_G', specs)
        self.assertIn('exp(number)', specs['x1_max']['constraints']['syntax'])
        self.assertTrue(any(p['value']=='auto' for p in specs['linear_solver']['options']['choices']))
        self.assertTrue(any(p['value']=='aprox19' for p in specs['network_name']['options']['choices']))
        self.assertIn('self', [choice['value'] for choice in specs['gravity_type']['options']['choices']])
        self.assertIsNone(specs['gravity_rtol']['allowedDefault'])
        self.assertIsNone(specs['gravity_atol']['allowedDefault'])
        caps = self.run_api(['--preview-capabilities'])
        self.assertEqual(caps['extensions']['configuration']['standardParameterCount'], len(keys))
        self.assertEqual(caps['cases'], ['Sod'])

    def test_defaults_and_explicit_values_with_no_eos_or_device_access(self):
        result = self.inspect('eos_type=helmholtz\neos_table_path=/not present/absent.dat\ncompute_backend=cuda\ncuda_device=999\node_rtol=2e-5\neos_coulomb_mult=1\nx_pos=0.2\n')
        self.assertEqual(result['status'], 'ok')
        parameters = {p['key']: p for p in result['parameters']}
        self.assertEqual(parameters['ode_rtol']['valueSource'], 'input')
        self.assertEqual(parameters['ode_rtol']['parsedValue'], 2e-5)
        self.assertEqual(parameters['ode_max_substeps']['resolvedValue'], 10000)
        self.assertEqual(parameters['ode_max_substeps']['valueSource'], 'documented-default')
        self.assertEqual(parameters['ode_rtol']['applicability']['state'], 'not-applicable')
        self.assertEqual(parameters['eos_table_path']['path']['relativeTo'], 'process-working-directory')
        self.assertFalse(parameters['eos_table_path']['path']['existenceChecked'])
        self.assertTrue(parameters['out_dir']['path']['targetMayBeNew'])
        self.assertIsNone(parameters['network_name']['path'])
        self.assertEqual(parameters['x_pos']['parsedValue'], .2)
        self.assertEqual(result['unitSystem'], 'cgs')
        empty = self.inspect('', code=3, partial=True)
        self.assertIsNone(empty['coordinates'])
        self.assertIsNone(next(p for p in empty['parameters'] if p['key']=='cfl')['resolvedValue'])

    def test_explicit_eos_face_and_time_controls(self):
        result = self.inspect('solver=hLlC\neos_type=HeLmHoLtZ\neos_coulomb_mult=0.5\neos_table_path=/absent\n'
                              'hll_wave_speed=DaViS\ndt_max=1e-9\n')
        values = {p['key']: p for p in result['parameters']}
        for key, expected in [('eos_coulomb_mult', .5), ('dt_max', 1e-9)]:
            self.assertEqual(values[key]['parsedValue'], expected)
            self.assertEqual(values[key]['applicability']['state'], 'satisfied')
        schema = {p['key']: p for p in self.run_api(['--config-schema'])['parameters']}
        self.assertEqual(schema['eos_coulomb_mult']['group'], 'EOS')
        self.assertEqual(values['dt_max']['units']['unit'], 's')
        for text, key in [
            ('eos_coulomb_mult=-0.1', 'eos_coulomb_mult'),
            ('eos_coulomb_mult=1.1', 'eos_coulomb_mult'),
            ('eos_coulomb_mult=nan', 'eos_coulomb_mult'),
            ('eos_type=ideal\neos_coulomb_mult=0', 'eos_coulomb_mult'),
            ('solver=Roe\nhll_wave_speed=davis', 'hll_wave_speed'),
            ('dt_max=0', 'dt_max'), ('dt_max=-2', 'dt_max'),
            ('dt_max=1e-30', 'dt_max'),
            ('hll_wave_speed=guess', 'hll_wave_speed'),
]:
            with self.subTest(text=text):
                bad = self.inspect(text+'\n', 3)
                self.assertTrue(any(d.get('parameterKey') == key for d in bad['diagnostics']))

    def test_inactive_explicit_controls_are_retained_not_silently_removed(self):
        for text, key, value in [
            ('use_burn=false\node_rtol=2e-5', 'ode_rtol', 2e-5),
            ('use_diffusion=false\nD_spec=0.1', 'D_spec', .1)]:
            with self.subTest(key=key):
                out = self.inspect(text)
                parameter = next(p for p in out['parameters'] if p['key']==key)
                self.assertEqual(parameter['parsedValue'], value)
                self.assertEqual(parameter['valueSource'], 'input')
                self.assertEqual(parameter['applicability']['state'], 'not-applicable')

    def test_integer_tokens_reject_fractions_suffixes_and_overflow(self):
        for token in ['1.5','1.0','1e2','12suffix','2147483648','-2147483649','+-1','', 'nan']:
            with self.subTest(token=token):
                result = self.inspect(f'nblockx2={token}\n', 3)
                self.assertTrue(any(d['parameterKey']=='nblockx2' and d['code']=='INVALID_INTEGER' for d in result['diagnostics']))
        result = self.inspect('nblockx1=+1\nnblockx2=1\nnblockx3=0\n')
        self.assertEqual(result['coordinates']['dimension'], 2)
        self.assertEqual(result['coordinates']['axes'][1]['blocks'], 1)
        result = self.inspect('nblockx2=0\nnblockx3=1', 3)
        self.assertTrue(any(d['parameterKey']=='nblockx3' for d in result['diagnostics']))

    def test_float_and_expression_validation(self):
        for token in ['NaN','inf','-Infinity','1e999','1e-9999','1.2suffix','+-2','']:
            with self.subTest(token=token):
                out = self.inspect(f'ode_rtol={token}', 3)
                self.assertTrue(any(d['parameterKey']=='ode_rtol' for d in out['diagnostics']))
        for token in ['pi/0','2*pi*garbage','garbage','1.0suffix','pi*1e999',
                      'E','e','exp()','exp(pi)','exp(1000)','exp(1)junk',
                      '2*exp(1)','sin(0)','cos(0)','log(10)']:
            with self.subTest(token=token):
                out = self.inspect(f'x1_max={token}', 3)
                self.assertTrue(any(d['parameterKey']=='x1_max' for d in out['diagnostics']))
        result = self.inspect('x1_max=2 * pi\nuse_diffusion=FaLsE\n')
        p = {p['key']:p for p in result['parameters']}
        self.assertAlmostEqual(p['x1_max']['parsedValue'], 6.283185307179586)
        self.assertFalse(p['use_diffusion']['parsedValue'])
        result = self.inspect('x1_max=exp(1)\nx2_max=exp(-2)\n')
        p = {p['key']:p for p in result['parameters']}
        self.assertAlmostEqual(p['x1_max']['parsedValue'], math.e)
        self.assertAlmostEqual(p['x2_max']['parsedValue'], math.exp(-2))
        bad = self.inspect('dt_max=exp(1)', code=3)
        self.assertTrue(any(d['parameterKey']=='dt_max' for d in bad['diagnostics']))

    def test_physical_ranges_and_cross_parameter_bounds(self):
        invalid = {
            'sml_rho': '0', 'min_eint': '-1', 'max_eint': '1e-12',
            'cfl': '1.01', 'gamma': '1', 'smallt': '0', 'smallx': '1',
            'nuclearTempMin': '-1', 'nuclearDensMin': '-1', 'enucDtFactor': '0',
            'nseTempThreshold': '0', 'nseDensThreshold': '-1',
            'ode_rtol': '1', 'ode_atol': '0', 'ode_max_newton_iter': '0',
            'ode_max_substeps': '0', 'ode_dt_safe_fac': '1.01',
            'ode_dt_fac_min': '1.01', 'ode_dt_fac_max': '.99',
            'ode_initial_dt_frac': '0', 'dt_init': '1e-30', 'dt_min': '-1',
            'tstep_change_factor': '.99', 'diff_cfl': '1.01', 'diff_max_stages': '1',
            'nu_visc': '-1', 'alpha_therm': '-1', 'D_spec': '-1',
            'gravity_rtol': '1', 'gravity_atol': '-1', 'gravity_max_cycles': '0',
            'gravity_boundary': 'unknown',
            'tmax': '-1', 'max_steps': '-2', 'cuda_device': '-1',
        }
        for key, value in invalid.items():
            with self.subTest(key=key):
                result = self.inspect(f'{key}={value}', code=3)
                self.assertTrue(any(d['code'] in ('INVALID_RANGE','INVALID_OPTION') and d['parameterKey']==key
                                    for d in result['diagnostics']), result['diagnostics'])
        result = self.inspect('sml_rho=1e-110\nmin_eint=1e-100\nsmallx=1e-100\ndt_min=1e-100\ndt_init=1e-50')
        self.assertEqual(result['status'], 'ok')

    def test_retired_parameters_rejected_including_alias(self):
        for key in ['gravity_G', 'timeintegrator', 'enforce_mass_conservation', 'burn_verbose_level',
                    'ode_use_numerical_jac', 'ode_freeze_jacobian']:
            result = self.inspect(f'{key}=1', code=3)
            self.assertEqual(result['diagnostics'][0]['code'], 'RETIRED_PARAMETER')
            self.assertEqual(result['diagnostics'][0]['parameterKey'], key)
        result = self.inspect('time_integrator=RK3')
        self.assertEqual(next(p for p in result['parameters'] if p['key']=='time_integrator')['resolvedValue'], 'RK3')

    def test_geometry_and_units_in_all_dimensions(self):
        names = {'cartesian': {1:['x'],2:['x','y'],3:['x','y','z']},
                 'cylindrical': {1:['r'],2:['r','phi'],3:['r','z','phi']},
                 'spherical': {1:['r'],2:['r','phi'],3:['r','theta','phi']}}
        for geometry, dims in names.items():
            for dim, labels in dims.items():
                with self.subTest(geometry=geometry, dim=dim):
                    r = self.inspect(f'geometry={geometry}\nnblockx2={int(dim>=2)}\nnblockx3={int(dim==3)}\neos_type=helmholtz\neos_coulomb_mult=1\neos_table_path=/absent')
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
        for key in ['solver', 'reconstruct', 'limiter', 'time_integrator']:
            with self.subTest(unknown_policy=key):
                out=self.inspect(f'{key}=unknown',3)
                self.assertTrue(any(d['code']=='INVALID_OPTION' and d['parameterKey']==key
                                    for d in out['diagnostics']), out['diagnostics'])
        out=self.inspect('gravity_type=self',3)
        self.assertTrue(any(d['parameterKey']=='gravity_boundary' for d in out['diagnostics']))
        out=self.inspect('gravity_type=self\ngravity_boundary=periodic\ngravity_rtol=1e-10\ngravity_atol=0\nx1l_boundary_type=periodic\nx1r_boundary_type=periodic\nx2l_boundary_type=periodic\nx2r_boundary_type=periodic\nx3l_boundary_type=periodic\nx3r_boundary_type=periodic')
        self.assertFalse(any(d['severity']=='error' for d in out['diagnostics']))

    def test_request_encoding_limits_and_modes(self):
        for payload in [b'\xff',b'\0',b'#'*(1024*1024+1)]:
            out=self.run_api(['--inspect-config','Sod','--config-stdin'],payload,2)
            self.assertEqual(out['kind'],'configuration-inspection')
        out=self.run_api(['--inspect-config','Sod','--config-stdin','--samples','4'],b'',2)
        self.assertEqual(out['diagnostics'][0]['code'],'INVALID_REQUEST')
        self.assertEqual(self.run_api(['--config-schema','unexpected'],code=2)['kind'],'configuration-schema')

    def test_preview_uses_same_strict_standard_parser(self):
        for key,value in [('nblockx1','1.5'),('ode_rtol','NaN'),('gravity_g_x','2junk')]:
            out=self.run_api(['--preview','Sod','--config-stdin'],overlay(f'{key}={value}\n').encode(),3)
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
