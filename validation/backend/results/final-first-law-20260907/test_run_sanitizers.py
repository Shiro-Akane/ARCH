"""CPU-only command/profile controls; no executable, GPU or build is launched."""
import ast
from contextlib import redirect_stderr, redirect_stdout
from copy import deepcopy
import csv
import hashlib
import io
import json
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import run_sanitizers as recipe

HERE = Path(__file__).resolve().parent
BUILD = Path('/nonexistent/sanitizer-fixture-build')


class SanitizerProfileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        archived = HERE / 'profile-history/full-interval/run_sanitizers.py'
        source = archived.read_bytes()
        if hashlib.sha256(source).hexdigest() != 'f52f31ac9fe366f6f1cd3dc58564269a24e28a1dee3323e6cccb080a5c0a71f2':
            raise AssertionError('archived pre-profile recipe changed')
        tree = ast.parse(source)
        cases = next(node for node in tree.body if isinstance(node, ast.Assign)
                     and any(isinstance(t, ast.Name) and t.id == 'CASES' for t in node.targets))
        cls.old_cases = ast.literal_eval(cases.value)
        cls.configured = {name: [str(BUILD / ('arch_' + name)), '--fixture', name]
                          for name in cls.old_cases}
        main = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'main')
        start = next(i for i, node in enumerate(main.body) if isinstance(node, ast.Assign)
                     and any(isinstance(t, ast.Name) and t.id == 'commands' for t in node.targets))
        # Evaluate only the archived three-statement command construction, not main.
        block = ast.Module(body=main.body[start:start + 3], type_ignores=[])
        namespace = dict(build=BUILD, configured=cls.configured, CASES=cls.old_cases)
        exec(compile(block, str(archived), 'exec'), namespace)
        cls.original_commands = namespace['commands']
        path = recipe.ROOT / 'validation/network/results/sparse-native-20260907/release-900/evidence.json'
        cls.record = json.loads(path.read_text())
        cls.metadata = next(item['manifest'] for item in cls.record['identity']['build']['registered_networks']
                            if item['manifest']['network_id'] == 'audit31')
        transcript = Path(cls.record['transcript']['path'])
        if recipe.provenance.sha256(transcript) != cls.record['transcript']['sha256']:
            raise AssertionError('existing sparse transcript identity changed')
        cls.transcript = transcript.read_text()

    def test_default_commands_are_byte_for_byte_original_for_both_tools(self):
        self.assertEqual(recipe.CASES, self.old_cases)
        for tool in ('memcheck', 'racecheck'):
            profile = recipe.sparse_profile(tool)
            commands = recipe.sanitizer_commands(BUILD, self.configured, profile)
            self.assertEqual(commands, self.original_commands)
            self.assertEqual(list(commands), list(self.original_commands))
            self.assertEqual(len(commands), 23)
            self.assertEqual(profile['controls']['interval'], 1e-10)
            self.assertEqual(profile['interval_selection'], 'original-default')

    def test_explicit_race_profile_changes_only_sparse_interval_token(self):
        profile = recipe.sparse_profile('racecheck', '0.000000000001')
        commands = recipe.sanitizer_commands(BUILD, self.configured, profile)
        expected = deepcopy(self.original_commands)
        expected['audit31_sparse_cells'][3] = '1e-12'
        self.assertEqual(commands, expected)
        self.assertEqual(profile['arguments'][2], '1e-12')
        self.assertEqual(profile['steps'], 4)
        self.assertEqual(profile['methods'], [1, 2, 3])
        self.assertEqual(profile['storage_sizes'], [2, 3])
        self.assertEqual(profile['composition'], ['c12=0.5', 'o16=0.5'])
        self.assertEqual(list(profile['controls']), ['rho', 'temperature', 'interval', 'cv', 'rtol'])
        self.assertEqual(profile['interval_selection'], 'explicit-racecheck')

    def test_invalid_intervals_and_every_memcheck_override_are_rejected(self):
        for value in ('', 'invalid', 'nan', 'NaN', 'inf', '-inf', '1e1000',
                      '0', '-1', '1e-1000', 0, -1, True, False, [], 10 ** 10000):
            with self.subTest(value_type=type(value).__name__):
                with self.assertRaises(ValueError):
                    recipe.sparse_profile('racecheck', value)
        for value in ('1e-12', '1e-10', 1e-10, 'nan'):
            with self.assertRaisesRegex(ValueError, 'only with racecheck'):
                recipe.sparse_profile('memcheck', value)
        with self.assertRaises(ValueError):
            recipe.sparse_profile('unknown')

    def test_invalid_cli_stops_before_outputs_or_processes(self):
        for tool, value in (('memcheck', '1e-10'), ('racecheck', 'NaN'),
                            ('racecheck', 'inf'), ('racecheck', '0'), ('racecheck', 'bad')):
            argv = ['run_sanitizers.py', '--build-dir', str(BUILD), '--output-dir', '/nonexistent/output',
                    '--sanitizer', '/nonexistent/tool', '--tool', tool, '--sparse-race-interval', value]
            with patch.object(recipe.sys, 'argv', argv), patch.object(recipe, 'run_arch_with_logs') as run, \
                    patch.object(recipe, 'require_empty_output_root') as empty, redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    recipe.main()
                self.assertEqual(error.exception.code, 2)
                run.assert_not_called()
                empty.assert_not_called()

    def test_shared_parser_rechecks_full_existing_transcript(self):
        profile = recipe.sparse_profile('memcheck')
        summary = recipe.sparse_validation.parse_transcript(
            self.transcript, self.metadata, profile['controls'], profile['steps'])
        self.assertEqual(set(summary['methods']), {1, 2, 3})
        self.assertEqual(summary['storage_sizes'], (2, 3))
        self.assertEqual(summary['steps'], 4)
        self.assertTrue(all(method['kernels'] > 0 for method in summary['methods'].values()))
        with self.assertRaises(ValueError):
            recipe.sparse_validation.parse_transcript(self.transcript, self.metadata,
                recipe.sparse_profile('racecheck', '1e-12')['controls'], 4)

    def test_shared_parser_rejects_missing_coverage_and_false_success(self):
        original = list(csv.reader(io.StringIO(self.transcript)))
        corruptions = []
        missing = deepcopy(original)
        missing.pop(next(i for i, row in enumerate(missing) if row[0] == 'gpu_step'))
        corruptions.append(missing)
        corruptions.append([row for row in original if not (row[0] == 'cpu_step' and row[1] == '3')])
        for kind, field, value in (('gpu_step', 4, '0'), ('metrics', 6, '0'),
                                   ('metrics', 7, '0'), ('metrics', 4, '1')):
            rows = deepcopy(original)
            next(row for row in rows if row[0] == kind)[field] = value
            corruptions.append(rows)
        corruptions.append([row for row in original if row != ['GENERATED_SPARSE_BURN_PARITY_PASS']])
        for rows in corruptions:
            stream = io.StringIO()
            csv.writer(stream).writerows(rows)
            with self.assertRaises(ValueError):
                recipe.sparse_validation.parse_transcript(stream.getvalue(), self.metadata,
                    recipe.sparse_profile('memcheck')['controls'], 4)

    def test_main_uses_shared_parser_and_archives_profile_and_summary(self):
        inventory = json.dumps(dict(tests=[dict(name=name, command=command)
            for name, command in self.configured.items()]))
        identity = dict(build=dict(registered_networks=[dict(manifest=self.metadata)]))
        argv = ['run_sanitizers.py', '--build-dir', str(BUILD), '--output-dir', '/nonexistent/output',
                '--sanitizer', '/nonexistent/tool', '--tool', 'memcheck']

        def fake_run(command, **kwargs):
            stdout = inventory if command[0] == 'ctest' else self.transcript
            return SimpleNamespace(returncode=0, stdout=stdout)

        with patch.object(recipe.sys, 'argv', argv), redirect_stdout(io.StringIO()), \
                patch.object(recipe, 'run_arch_with_logs', side_effect=fake_run) as run, \
                patch.object(recipe, 'require_empty_output_root'), patch.object(Path, 'mkdir'), \
                patch.object(Path, 'write_text') as write, patch.object(recipe, 'CudaSanitizer') as instrument, \
                patch.object(recipe.provenance, 'capture_focused', return_value=identity), \
                patch.object(recipe.provenance, 'file_identity', side_effect=lambda path:
                    dict(path=str(path), sha256='a' * 64)), \
                patch.object(recipe.sparse_validation, 'parse_transcript',
                    wraps=recipe.sparse_validation.parse_transcript) as parse:
            instrument.return_value.evidence.return_value = dict(tool='memcheck')
            recipe.main()
            self.assertEqual(run.call_count, 24)  # One read-only inventory plus 23 fixture routes.
            parse.assert_called_once_with(self.transcript, self.metadata,
                recipe.sparse_profile('memcheck')['controls'], 4)
            evidence = json.loads(write.call_args.args[0])
        self.assertFalse(evidence['release_qualified'])
        self.assertEqual(evidence['timeout_seconds'], 1200)
        self.assertEqual(evidence['sparse_profile'], recipe.sparse_profile('memcheck'))
        sparse = next(item for item in evidence['cases'] if item['name'] == 'audit31_sparse_cells')
        self.assertEqual(set(sparse['sparse_summary']['methods']), {'1', '2', '3'})
        self.assertTrue(all('sparse_summary' not in item for item in evidence['cases']
                            if item['name'] != 'audit31_sparse_cells'))


if __name__ == '__main__':
    unittest.main(verbosity=2)
