"""Core A contract through the real CPU preview executable; no simulation."""
import hashlib
import json
import subprocess
import unittest

import test_preview as base


def without_position():
    return b'\n'.join(line for line in base.BASE.splitlines()
                      if line.split(b'#', 1)[0].split(b'=', 1)[0].strip() != b'x_pos') + b'\n'


class ParameterMetadataContract(unittest.TestCase):
    setUp = base.PreviewContract.setUp
    invoke = base.PreviewContract.invoke

    def parameter(self, result):
        extension = result['parameterMetadata']
        self.assertEqual(extension['version'], '1')
        self.assertEqual(extension['coverage'], 'observed-case-setup-reads')
        self.assertFalse(extension['complete'])
        self.assertEqual(len(extension['parameters']), 1)
        parameter = extension['parameters'][0]
        self.assertEqual(parameter['key'], 'x_pos')
        self.assertEqual(parameter['type'], 'float')
        self.assertEqual(parameter['defaultValue'], .5)
        self.assertEqual(parameter['unit'], 'cm')
        self.assertFalse(parameter['unitEvidence']['automaticInference'])
        self.assertIsNone(parameter['description'])
        return parameter

    def test_capabilities_publish_independent_partial_extensions(self):
        run = subprocess.run([str(base.ARCH), '--preview-capabilities'],
                             check=True, capture_output=True, cwd=self.cwd, timeout=15)
        caps = json.loads(run.stdout)
        self.assertEqual(caps['schemaVersion'], '1.0')
        self.assertEqual(caps['cases'], ['Sod'])
        self.assertFalse(caps['parameterTracing'])
        self.assertTrue(caps['markers'])
        metadata = caps['extensions']['parameterMetadata']
        self.assertEqual(metadata['version'], '1')
        self.assertFalse(metadata['complete'])
        self.assertEqual(metadata['cases'], [{'caseId': 'Sod', 'keys': ['x_pos']}])
        binding = caps['extensions']['graphicalBindings']
        self.assertEqual(binding['version'], '1')
        self.assertEqual(binding['cases'][0]['ids'], ['Sod.x_pos'])

    def test_actual_read_sources_and_init_agree(self):
        for payload, explicit, effective, source, reason in [
            (base.config(x_pos=.35), .35, .35, 'explicit', None),
            (without_position(), None, .5, 'default', 'missing-key'),
            (base.config(x_pos='bad'), None, .5, 'default', 'parse-failure'),
            (base.config(x_pos='1e9999'), None, .5, 'default', 'parse-failure'),
        ]:
            with self.subTest(source=source, reason=reason):
                result = self.invoke(payload, '--samples', '20', '--request-id', 'metadata-a')
                p = self.parameter(result)
                self.assertEqual(p['explicitValue'], explicit)
                self.assertEqual(p['effectiveValue'], effective)
                self.assertEqual(p['valueSource'], source)
                self.assertEqual(p['sourceReason'], reason)
                self.assertEqual(result['identity']['configRevision'], hashlib.sha256(payload).hexdigest())
                self.assertEqual(result['identity']['requestId'], 'metadata-a')
                extension = result['graphicalBindings']
                self.assertEqual(extension['version'], '1')
                self.assertEqual(len(extension['items']), 1)
                marker = extension['items'][0]
                self.assertEqual(marker['coordinate'], effective)
                self.assertEqual(marker['axis'], 'x1')
                self.assertEqual(marker['parameterKey'], 'x_pos')
                self.assertTrue(marker['editable'])
                density = next(f['values'] for f in result['data']['fields'] if f['key'] == 'DENS')
                for x, rho in zip(result['data']['axes'][0]['values'], density):
                    self.assertEqual(rho, 1 if x < marker['coordinate'] else .125)
                if reason == 'parse-failure':
                    self.assertEqual(p['diagnostics'][0]['code'], 'PARAMETER_DEFAULT_FALLBACK')

    def test_current_domain_open_bounds_and_literal_default(self):
        result = self.invoke(base.config(x1_min=-3, x1_max=5, x_pos=1), '--samples', '16')
        bounds = self.parameter(result)['constraints']
        self.assertEqual(bounds, {'min': -3, 'max': 5, 'minInclusive': False, 'maxInclusive': False})
        marker = result['graphicalBindings']['items'][0]
        self.assertEqual({key: marker[key] for key in bounds}, bounds)
        self.assertEqual(marker['clamping'], 'none')
        self.assertEqual(marker['invalidBehavior'], 'retain-input-and-report')
        payload = without_position() + b'x1_min = -3\nx1_max = 5\n'
        self.assertEqual(self.parameter(self.invoke(payload))['effectiveValue'], .5)

    def test_boundary_and_outside_values_remain_visible_on_setup_failure(self):
        for x in [-3, 5, 6]:
            with self.subTest(x=x):
                result = self.invoke(base.config(x1_min=-3, x1_max=5, x_pos=x), expected=5)
                p = self.parameter(result)
                self.assertEqual(p['effectiveValue'], x)
                self.assertEqual(p['explicitValue'], x)
                self.assertEqual(p['valueSource'], 'explicit')
                self.assertEqual(p['constraints']['min'], -3)
                self.assertIsNone(result['data'])
                self.assertEqual(result['graphicalBindings']['items'], [])
                self.assertEqual(result['stage'], 'setup')
        payload = without_position() + b'x1_min = 10\nx1_max = 20\n'
        result = self.invoke(payload, expected=5)
        self.assertEqual(self.parameter(result)['effectiveValue'], .5)
        self.assertEqual(self.parameter(result)['sourceReason'], 'missing-key')

    def test_last_duplicate_and_numeric_prefix_keep_parser_semantics(self):
        payload = without_position() + b'x_pos=.2\r\nx_pos = 0.65suffix # preserved input\r\n'
        result = self.invoke(payload)
        p = self.parameter(result)
        self.assertEqual(p['effectiveValue'], .65)
        self.assertEqual(p['explicitValue'], .65)
        self.assertEqual(p['rawValue'], '0.65suffix')
        self.assertEqual(p['valueSource'], 'explicit')
        self.assertEqual(result['identity']['configRevision'], hashlib.sha256(payload).hexdigest())

    def test_later_failures_keep_reads_without_a_success_binding(self):
        result = self.invoke(base.config(x_pos=.4, eos_type='helmholtz', eos_table_path='absent.dat'), expected=5)
        self.assertEqual(self.parameter(result)['effectiveValue'], .4)
        self.assertEqual(result['state']['setup'], 'ready')
        self.assertEqual(result['state']['eos']['status'], 'error')
        self.assertIsNone(result['data'])
        self.assertEqual(result['graphicalBindings']['items'], [])
        result = self.invoke(base.config(x_pos=.4, p_left='1e308'), expected=6)
        self.assertEqual(self.parameter(result)['effectiveValue'], .4)
        self.assertEqual(result['stage'], 'sampling')
        self.assertIsNone(result['data'])
        self.assertEqual(result['graphicalBindings']['items'], [])

    def test_before_setup_failure_does_not_invent_a_read(self):
        for payload, status in [(base.config(x_pos='nan'), 3),
                                (base.config(nblockx2=1), 4),
                                (base.config(restart='maybe'), 3)]:
            with self.subTest(status=status):
                result = self.invoke(payload, expected=status)
                self.assertNotIn('parameterMetadata', result)
                self.assertNotIn('graphicalBindings', result)
                self.assertIsNone(result['data'])


if __name__ == '__main__':
    unittest.main()
