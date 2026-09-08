"""Bounded process-identity controls; no CUDA or profiler is launched."""
from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('capacity_replay', Path(__file__).with_name('replay.py'))
replay = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(replay)


def fixture():
    pid = 123
    artifact = {'path': '/build/arch_cuda_generated_sparse_burn_audit31', 'sha256': 'a' * 64}
    stat = [1, 2, 300, 400, 500]
    summary = {'processes': [{'pid': pid, 'global_pid': 987654321,
                              'device': 0, 'executable': 'arch_cuda_gener'}]}
    capture = {'schema': 1, 'complete': True, 'poll_interval_seconds': 0.05,
               'observations': [{'pid': pid, 'start_time_ticks': 42,
                    'executable_path': artifact['path'], 'artifact_observation': stat,
                    'first_observed_monotonic_ns': 100, 'last_observed_monotonic_ns': 200,
                    'samples': 3}]}
    return summary, capture, {'sparse': artifact}, {'sparse': stat}


def handoff_fixture():
    args = fixture()
    launcher = {'artifact': {'path': '/tools/profiler-bootstrap', 'sha256': 'b' * 64},
                'artifact_observation': [1, 8, 900, 1000, 1100]}
    bootstrap = deepcopy(args[1]['observations'][0])
    bootstrap.update(executable_path=launcher['artifact']['path'],
                     artifact_observation=list(launcher['artifact_observation']),
                     first_observed_monotonic_ns=10, last_observed_monotonic_ns=90)
    args[1]['observations'].insert(0, bootstrap)
    return args, launcher


class ProcessIdentityTest(unittest.TestCase):
    def test_exact_image_resolves_short_name_without_replacing_it(self):
        result = replay.check_process_identities(*fixture())
        process, = result['processes']
        self.assertEqual(process['nsight_process_name'], 'arch_cuda_gener')
        self.assertEqual(process['executable']['path'], '/build/arch_cuda_generated_sparse_burn_audit31')
        self.assertEqual(process['executable']['sha256'], 'a' * 64)

    def test_missing_selected_pid_rejected(self):
        args = fixture()
        args[1]['observations'].clear()
        with self.assertRaises(ValueError):
            replay.check_process_identities(*args)

    def test_same_prefix_wrong_binary_rejected(self):
        args = fixture()
        args[1]['observations'][0]['executable_path'] = '/build/arch_cuda_generated_sparse_burn_weak_urca'
        with self.assertRaises(ValueError):
            replay.check_process_identities(*args)

    def test_same_basename_wrong_directory_rejected(self):
        args = fixture()
        args[1]['observations'][0]['executable_path'] = '/other/arch_cuda_generated_sparse_burn_audit31'
        with self.assertRaises(ValueError):
            replay.check_process_identities(*args)

    def test_changed_executable_stat_rejected(self):
        for index in range(5):
            args = fixture()
            args[1]['observations'][0]['artifact_observation'] = list(args[3]['sparse'])
            args[1]['observations'][0]['artifact_observation'][index] += 1
            with self.subTest(index=index), self.assertRaises(ValueError):
                replay.check_process_identities(*args)

    def test_deleted_image_rejected(self):
        args = fixture()
        args[1]['observations'][0]['executable_path'] += ' (deleted)'
        with self.assertRaises(ValueError):
            replay.check_process_identities(*args)

    def test_pid_reuse_rejected(self):
        args = fixture()
        other = deepcopy(args[1]['observations'][0])
        other['start_time_ticks'] += 1
        args[1]['observations'].append(other)
        with self.assertRaises(ValueError):
            replay.check_process_identities(*args)

    def test_multiple_executable_images_rejected(self):
        args = fixture()
        other = deepcopy(args[1]['observations'][0])
        other['executable_path'] = '/build/ARCH'
        args[1]['observations'].append(other)
        with self.assertRaises(ValueError):
            replay.check_process_identities(*args)

    def test_ambiguous_artifact_registry_rejected(self):
        args = fixture()
        args[2]['duplicate'] = deepcopy(args[2]['sparse'])
        args[3]['duplicate'] = list(args[3]['sparse'])
        with self.assertRaises(ValueError):
            replay.check_process_identities(*args)

    def test_workload_specific_artifact_rejected(self):
        args = fixture()
        args[2]['sparse']['path'] = '/build/arch_cuda_regrid_transaction'
        with self.assertRaises(ValueError):
            replay.check_process_identities(*args)

    def test_selected_pid_without_observation_rejected(self):
        args = fixture()
        args[0]['processes'][0]['pid'] += 1
        with self.assertRaises(ValueError):
            replay.check_process_identities(*args)

    def test_global_identity_ambiguity_rejected(self):
        args = fixture()
        other = deepcopy(args[0]['processes'][0])
        other['global_pid'] += 1
        args[0]['processes'].append(other)
        with self.assertRaises(ValueError):
            replay.check_process_identities(*args)

    def test_invalid_process_identifiers_rejected(self):
        for key in ('pid', 'global_pid'):
            for invalid in (0, -1, True, '1'):
                args = fixture()
                args[0]['processes'][0][key] = invalid
                with self.subTest(key=key, invalid=invalid), self.assertRaises(ValueError):
                    replay.check_process_identities(*args)

    def test_incomplete_observer_rejected(self):
        args = fixture()
        args[1]['complete'] = False
        with self.assertRaises(ValueError):
            replay.check_process_identities(*args)

    def test_invalid_sampling_interval_rejected(self):
        for interval in (0, -1, True, float('inf'), float('nan')):
            args = fixture()
            args[1]['poll_interval_seconds'] = interval
            with self.subTest(interval=interval), self.assertRaises(ValueError):
                replay.check_process_identities(*args)

    def test_invalid_observation_fields_rejected(self):
        for key in ('start_time_ticks', 'samples', 'first_observed_monotonic_ns',
                    'last_observed_monotonic_ns'):
            args = fixture()
            args[1]['observations'][0][key] = -1
            with self.subTest(key=key), self.assertRaises(ValueError):
                replay.check_process_identities(*args)

    def test_invalid_frozen_hash_rejected(self):
        args = fixture()
        args[2]['sparse']['sha256'] = 'missing'
        with self.assertRaises(ValueError):
            replay.check_process_identities(*args)

    def test_non_cuda_descendants_not_exported(self):
        args = fixture()
        other = deepcopy(args[1]['observations'][0])
        other.update(pid=456, executable_path='/profiler/helper')
        args[1]['observations'].append(other)
        result = replay.check_process_identities(*args)
        self.assertEqual(len(result['observations']), 1)
        self.assertNotIn('/profiler/helper', json.dumps(result))

    def test_same_process_multiple_devices(self):
        args = fixture()
        other = deepcopy(args[0]['processes'][0])
        other['device'] = 1
        args[0]['processes'].append(other)
        result = replay.check_process_identities(*args)
        self.assertEqual(len(result['processes']), 1)

    def test_index_replay_and_input_immutability(self):
        args = fixture()
        before = deepcopy(args)
        result = replay.check_process_identities(*args)
        self.assertEqual(args, before)
        self.assertEqual(result, replay.check_process_identities(args[0],
                         json.loads(json.dumps(result)), args[2], args[3]))

    def test_selected_failure_capture_distinguishes_zero_from_multiple(self):
        for count in (0, 2):
            args = fixture()
            image = deepcopy(args[1]['observations'][0])
            args[1]['observations'] = [deepcopy(image) for _ in range(count)]
            capture = replay.selected_process_capture(args[0], args[1])
            self.assertEqual(capture['selected_counts'][0]['image_count'], count)
            self.assertEqual(capture['selected_counts'][0]['sample_count'], 3 * count)
            with self.subTest(count=count), self.assertRaises(ValueError):
                replay.check_process_identities(args[0], capture, args[2], args[3])

    def test_selected_capture_does_not_export_unrelated_or_extra_fields(self):
        args = fixture()
        other = deepcopy(args[1]['observations'][0])
        other.update(pid=456, executable_path='/profiler/helper')
        args[1]['observations'].append(other)
        args[1]['observations'][0]['unrelated_metadata'] = 'must-not-be-exported'
        capture = replay.selected_process_capture(args[0], args[1])
        self.assertEqual(len(capture['observations']), 1)
        self.assertNotIn('/profiler/helper', json.dumps(capture))
        self.assertNotIn('must-not-be-exported', json.dumps(capture))
        replay.check_process_identities(args[0], capture, args[2], args[3])

    def test_declared_launcher_handoff(self):
        args, launcher = handoff_fixture()
        result = replay.check_process_identities(*args, launcher=launcher)
        self.assertEqual(result['processes'][0]['launch_mode'], 'pinned-launcher-exec')
        self.assertEqual(result['processes'][0]['instrumentation_launcher'], launcher)
        self.assertEqual(len(result['observations']), 2)

    def test_undeclared_launcher_rejected(self):
        args, _ = handoff_fixture()
        with self.assertRaises(ValueError):
            replay.check_process_identities(*args)

    def test_unknown_same_prefix_launcher_rejected(self):
        args, launcher = handoff_fixture()
        args[1]['observations'][0]['executable_path'] += '-other'
        with self.assertRaises(ValueError):
            replay.check_process_identities(*args, launcher=launcher)

    def test_same_basename_launcher_in_wrong_directory_rejected(self):
        args, launcher = handoff_fixture()
        args[1]['observations'][0]['executable_path'] = '/other/profiler-bootstrap'
        with self.assertRaises(ValueError):
            replay.check_process_identities(*args, launcher=launcher)

    def test_changed_launcher_object_rejected(self):
        for index in range(5):
            args, launcher = handoff_fixture()
            args[1]['observations'][0]['artifact_observation'][index] += 1
            with self.subTest(index=index), self.assertRaises(ValueError):
                replay.check_process_identities(*args, launcher=launcher)

    def test_launcher_after_workload_rejected(self):
        args, launcher = handoff_fixture()
        args[1]['observations'][0].update(first_observed_monotonic_ns=210,
                                          last_observed_monotonic_ns=300)
        with self.assertRaises(ValueError):
            replay.check_process_identities(*args, launcher=launcher)

    def test_overlapping_launcher_rejected(self):
        for last in (100, 150, 250):
            args, launcher = handoff_fixture()
            args[1]['observations'][0]['last_observed_monotonic_ns'] = last
            with self.subTest(last=last), self.assertRaises(ValueError):
                replay.check_process_identities(*args, launcher=launcher)

    def test_return_to_launcher_rejected(self):
        args, launcher = handoff_fixture()
        # The observer aggregates the same object's samples: a return extends
        # the launcher's last timestamp beyond the first workload observation.
        args[1]['observations'][0]['last_observed_monotonic_ns'] = 300
        with self.assertRaises(ValueError):
            replay.check_process_identities(*args, launcher=launcher)

    def test_launcher_different_start_time_rejected(self):
        args, launcher = handoff_fixture()
        args[1]['observations'][0]['start_time_ticks'] += 1
        with self.assertRaises(ValueError):
            replay.check_process_identities(*args, launcher=launcher)

    def test_third_image_rejected(self):
        args, launcher = handoff_fixture()
        extra = deepcopy(args[1]['observations'][0])
        extra['executable_path'] = '/other/program'
        args[1]['observations'].append(extra)
        with self.assertRaises(ValueError):
            replay.check_process_identities(*args, launcher=launcher)

    def test_launcher_cannot_replace_missing_workload(self):
        args, launcher = handoff_fixture()
        args[1]['observations'].pop()
        with self.assertRaises(ValueError):
            replay.check_process_identities(*args, launcher=launcher)

    def test_launcher_cannot_be_workload_artifact(self):
        args = fixture()
        launcher = {'artifact': args[2]['sparse'], 'artifact_observation': args[3]['sparse']}
        with self.assertRaises(ValueError):
            replay.check_process_identities(*args, launcher=launcher)

    def test_direct_workload_with_configured_launcher(self):
        args, launcher = handoff_fixture()
        args[1]['observations'].pop(0)
        result = replay.check_process_identities(*args, launcher=launcher)
        self.assertEqual(result['processes'][0]['launch_mode'], 'direct')
        self.assertIsNone(result['processes'][0]['instrumentation_launcher'])

    def test_launcher_handoff_index_replay(self):
        args, launcher = handoff_fixture()
        before = deepcopy((args, launcher))
        result = replay.check_process_identities(*args, launcher=launcher)
        self.assertEqual((args, launcher), before)
        self.assertEqual(result, replay.check_process_identities(args[0],
            json.loads(json.dumps(result)), args[2], args[3], launcher=launcher))

    def test_invalid_launcher_identity_rejected(self):
        for kind in ('hash', 'path', 'stat'):
            args, launcher = handoff_fixture()
            if kind == 'hash':
                launcher['artifact']['sha256'] = 'invalid'
            elif kind == 'path':
                launcher['artifact']['path'] = 'relative-launcher'
            else:
                launcher['artifact_observation'][0] = -1
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                replay.check_process_identities(*args, launcher=launcher)

    def test_tool_changed_during_identity_capture_rejected(self):
        first = {'profiler': [1, 2, 3, 4, 5]}
        changed = {'profiler': [1, 2, 3, 4, 6]}
        artifact = {'path': '/tools/profiler', 'sha256': 'b' * 64}
        with patch.object(replay.provenance, '_artifact_observations', side_effect=[first, changed]), \
                patch.object(replay.provenance, 'file_identity', return_value=artifact), \
                self.assertRaises(ValueError):
            replay.capture_instrumentation(Path('/tools/profiler'))

    def test_real_owned_cpu_process_observation(self):
        executable = Path(shutil.which('sleep')).resolve()
        frozen = {'cpu_fixture': replay.provenance.file_identity(executable)}
        stats = replay.provenance._artifact_observations({'cpu_fixture': executable})
        with tempfile.TemporaryDirectory(prefix='identity-selftest-') as directory:
            with replay.ProcessImageObserver(0.01) as observer:
                completed = replay.run_arch_with_logs([str(executable), '0.2'],
                    source_root=replay.ROOT, lane_root=Path(directory), timeout=5)
        self.assertEqual(completed.returncode, 0)
        capture = observer.capture()
        matches = [row for row in capture['observations'] if row['executable_path'] == str(executable)]
        self.assertEqual(len(matches), 1)
        pid = matches[0]['pid']
        summary = {'processes': [{'pid': pid, 'global_pid': 987654321,
                                  'executable': 'not-an-identity'}]}
        result = replay.check_process_identities(summary, capture, frozen, stats)
        self.assertEqual(result['processes'][0]['executable'], frozen['cpu_fixture'])
        self.assertGreater(result['observations'][0]['samples'], 0)


if __name__ == '__main__':
    unittest.main()
