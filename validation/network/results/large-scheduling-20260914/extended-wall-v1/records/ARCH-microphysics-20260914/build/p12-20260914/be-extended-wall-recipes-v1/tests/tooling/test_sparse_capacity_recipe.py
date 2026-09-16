import argparse
import json
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'validation/network'))
import run_sparse_capacity as capacity


class SparseCapacityRecipeTests(unittest.TestCase):
    def test_original_gate_command_preserved(self):
        self.assertEqual(capacity.trajectory_command('probe','be_nr',2,[2,3],4),
            ['probe','1e7','3e9','1e-10','1e8','1e-7','4','--ode','be_nr',
             '--storage-cells','2','3','--pool-cells','2','c12=0.5','o16=0.5'])

    def test_longer_trajectory_does_not_change_accuracy_or_initial_state(self):
        original=capacity.trajectory_command('probe','bd',8,[32,33],4)
        extended=capacity.trajectory_command('probe','bd',8,[32,33],16,1e-9)
        self.assertEqual([i for i,(a,b) in enumerate(zip(original,extended)) if a!=b],[3,6])
        for bad in ('0','-1','nan','inf'):
            with self.assertRaises(argparse.ArgumentTypeError): capacity.positive_duration(bad)

    def test_wall_budget_is_separate_from_physical_trajectory(self):
        required=['--build-dir','build','--source','test.cpp','--provider','provider.a',
                  '--output-dir','new-output','--methods','be_nr','--pools','8','32',
                  '--storage','32','33','--steps','16','--duration','1e-9']
        before=capacity.argument_parser().parse_args(required)
        after=capacity.argument_parser().parse_args(required+['--run-timeout','21600'])
        self.assertEqual(before.run_timeout,1800)
        self.assertEqual(after.run_timeout,21600)
        left,right=vars(before).copy(),vars(after).copy()
        left.pop('run_timeout'); right.pop('run_timeout')
        self.assertEqual(left,right)
        for pool in after.pools:
            self.assertEqual(
                capacity.trajectory_command('probe','be_nr',pool,before.storage,before.steps,before.duration),
                capacity.trajectory_command('probe','be_nr',pool,after.storage,after.steps,after.duration))

    def test_wall_budget_is_finite_and_bounded(self):
        for text in ('1','1800','21600','86400'):
            self.assertEqual(capacity.positive_wall_timeout(text),float(text))
        for text in ('0','-1','nan','inf','-inf','86401','invalid'):
            with self.subTest(text=text), self.assertRaises(argparse.ArgumentTypeError):
                capacity.positive_wall_timeout(text)

    def test_timeout_preserves_pre_run_artifacts_and_native_build_limits(self):
        # Test-only fake compiler/runtime: this validates orchestration records,
        # not the CUDA trajectory or compiler. No real build is launched.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / 'build'
            build.mkdir()
            source, provider = root / 'test.cpp', root / 'provider.a'
            source.write_text('test fixture')
            provider.write_bytes(b'test provider')
            target = 'arch_cuda_generated_sparse_burn_audit150'
            old_source = '/old/tests/cuda/test_generated_sparse_burn.cpp'
            old_obj = f'CMakeFiles/{target}.dir/tests/cuda/test_generated_sparse_burn.cpp.o'
            factory = build / f'CMakeFiles/{target}.dir/tests/cuda/test_generated_sparse_burn_factory.cu.o'
            factory.parent.mkdir(parents=True)
            factory.write_bytes(b'frozen test factory')
            entries = [dict(file=old_source, command=shlex.join(
                ['test-cxx', '-c', old_source, '-o', old_obj]))]
            (build / 'compile_commands.json').write_text(json.dumps(entries))
            out = root / 'new-run'
            limits = []

            def fake_run(command, **kwargs):
                if command[0] == 'ninja':
                    native = [':', '&&', 'test-link', old_obj,
                              factory.relative_to(build).as_posix(), 'libarch_cuda_sparse_provider.a',
                              '-o', target, '&&', ':']
                    return subprocess.CompletedProcess(command, 0, shlex.join(native))
                limits.append(kwargs['timeout'])
                if command[0] in ('test-cxx', 'test-link'):
                    Path(command[command.index('-o') + 1]).write_bytes(b'test artifact')
                    return subprocess.CompletedProcess(command, 0)
                raise subprocess.TimeoutExpired(command, kwargs['timeout'])

            argv = ['run_sparse_capacity.py', '--build-dir', str(build), '--source', str(source),
                    '--provider', str(provider), '--output-dir', str(out), '--methods', 'be_nr',
                    '--pools', '8', '32', '--storage', '32', '33', '--steps', '16',
                    '--duration', '1e-9', '--run-timeout', '21600']
            with patch.object(sys, 'argv', argv), patch.object(capacity.signal, 'signal'), \
                    patch.object(capacity.subprocess, 'run', side_effect=fake_run):
                with self.assertRaises(subprocess.TimeoutExpired):
                    capacity.main()
            record = json.loads((out / 'record.json').read_text())
            self.assertEqual(limits, [1800, 1800, 21600])
            self.assertEqual(record['status'], 'failed')
            self.assertEqual(record['runs'], [])
            self.assertEqual(record['artifacts'][0]['factory_sha256'], capacity.digest(factory))
            self.assertEqual(record['artifacts'][0]['executable_sha256'], capacity.digest(out / target))
            self.assertTrue(record['commands'][-1]['timed_out'])
            self.assertIsNone(record['commands'][-1]['returncode'])
            self.assertEqual(record['commands'][-1]['timeout_seconds'], 21600)


if __name__ == '__main__': unittest.main()
