"""Linux-only regressions; children sleep and use no meaningful extra RAM."""

import importlib.util
import csv
import errno
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from types import SimpleNamespace
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[3]
TOOL = ROOT / 'tools' / 'run_memory_guarded.py'
spec = importlib.util.spec_from_file_location('run_memory_guarded', TOOL)
guard = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = guard
spec.loader.exec_module(guard)


class SystemPressureTests(unittest.TestCase):
    def observer(self):
        with patch.object(guard, 'pressure_counters', return_value=(0, 0, 0, 0)), \
                patch.object(guard.time, 'monotonic', return_value=0.), \
                patch.object(guard.os, 'sysconf', return_value=4096):
            return guard.SystemPressureObservation(20., 50., 10.)

    def sample(self, observer, seconds, counters):
        with patch.object(guard, 'pressure_counters', return_value=counters), \
                patch.object(guard.time, 'monotonic', return_value=seconds):
            return observer.sample()

    def test_productive_swap_is_recorded_without_stopping(self):
        observer = self.observer()
        self.assertIsNone(self.sample(observer, 20., (0, 0, 1024, 2048)))
        self.assertEqual(observer.peaks, [0., 0., .2, .4])
        self.assertIn('scope=linux_system', observer.summary())

    def test_brief_stall_resets_when_system_recovers(self):
        observer = self.observer()
        self.assertIsNone(self.sample(observer, 5., (2500000, 4000000, 0, 0)))
        self.assertIsNone(self.sample(observer, 10., (2500000, 4000000, 0, 0)))
        self.assertEqual(observer.high_seconds, [0., 0.])
        self.assertIsNone(self.sample(observer, 15., (5000000, 8000000, 0, 0)))

    def test_sustained_memory_or_io_stall_stops_without_waiting_for_full_swap(self):
        for counters, reason in (((2000000, 0, 0, 0), 'memory_pressure'),
                                 ((0, 6000000, 0, 0), 'io_pressure')):
            self.assertEqual(self.sample(self.observer(), 10., counters), reason)

    def test_missing_or_reset_counters_fail_closed(self):
        observer = self.observer()
        with patch.object(guard, 'pressure_counters', side_effect=OSError('unavailable')):
            with self.assertRaises(OSError):
                observer.sample()
        with self.assertRaisesRegex(RuntimeError, 'backwards'):
            self.sample(observer, 1., (-1, 0, 0, 0))
        with self.assertRaisesRegex(RuntimeError, 'nonpositive'):
            self.sample(observer, 0., (0, 0, 0, 0))

    def test_parses_full_stall_and_page_counters_not_avg10_or_some(self):
        def read(path):
            if str(path).endswith('vmstat'):
                return 'nr_free_pages 999\npswpin 3\npswpout 7\n'
            return 'some avg10=90.0 total=999\nfull avg10=1.0 total=400\n'
        with patch.object(guard.Path, 'read_text', read):
            self.assertEqual(guard.pressure_counters(), (400, 400, 3, 7))


class ProcessSamplesObservationTests(unittest.TestCase):
    def observation(self, path, seconds=5.):
        result = guard.ProcessSamplesObservation(path, seconds, 0.)
        self.addCleanup(result.close)
        return result

    def rows(self, path):
        with path.open(newline='') as stream:
            return list(csv.DictReader(stream, delimiter='\t'))

    def test_interval_rows_are_flushed_and_not_resampled_before_due(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'samples.tsv'
            observer = self.observation(path)
            owner = SimpleNamespace(owned={42: (100, 7)})
            identity = guard.ProcessIdentity(42, 9, 100, 'S')
            with patch.object(guard, 'process_identity', return_value=identity), \
                    patch.object(observer, '_process_values', return_value=(12, 3, 'child', [])) as read, \
                    patch.object(guard.time, 'monotonic', side_effect=(0., 4., 5.)):
                observer.sample(owner)
                self.assertEqual(len(self.rows(path)), 1)  # Read before close: flush is observable.
                observer.sample(owner)
                self.assertEqual(read.call_count, 1)
                observer.sample(owner)
            self.assertEqual(len(self.rows(path)), 2)
            observer.close(monitor_complete=True)
            self.assertTrue(observer.complete)

    def test_pid_reuse_before_or_after_field_read_discards_the_row(self):
        old = guard.ProcessIdentity(42, 9, 100, 'S')
        reused = guard.ProcessIdentity(42, 9, 101, 'S')
        with tempfile.TemporaryDirectory() as directory:
            for name, identities, read_count in (('before', [reused], 0),
                                                  ('after', [old, reused], 1)):
                with self.subTest(name=name):
                    path = Path(directory) / f'{name}.tsv'
                    observer = self.observation(path)
                    with patch.object(guard, 'process_identity', side_effect=identities), \
                            patch.object(observer, '_process_values', return_value=(12, 3, 'child', [])) as read:
                        observer.sample(SimpleNamespace(owned={42: (100, 7)}))
                    self.assertEqual(read.call_count, read_count)
                    self.assertEqual(self.rows(path), [])
                    self.assertEqual(observer.skipped, 1)
                    observer.close(monitor_complete=True)
                    self.assertFalse(observer.complete)

    def test_live_missing_identity_or_counter_is_explicit_and_incomplete(self):
        identity = guard.ProcessIdentity(42, 9, 100, 'S')
        with tempfile.TemporaryDirectory() as directory:
            for name, known in (('identity', None), ('rss', identity)):
                with self.subTest(name=name):
                    path = Path(directory) / f'{name}.tsv'
                    observer = self.observation(path)
                    with patch.object(guard, 'process_identity', return_value=known), \
                            patch.object(observer, '_has_exited', return_value=False), \
                            patch.object(observer, '_process_values', return_value=(None, 3, 'child', ['rss'])):
                        observer.sample(SimpleNamespace(owned={42: (100, 7)}))
                    row, = self.rows(path)
                    self.assertEqual(row['rss_kib'], '')
                    self.assertEqual(row['status'], 'missing:' + name)
                    self.assertEqual(observer.missing, 1)
                    observer.close(monitor_complete=True)
                    self.assertFalse(observer.complete)
                    self.assertIn('missing=1', observer.summary())

    def test_verified_exit_is_skipped_without_a_zero_or_missing_row(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'samples.tsv'
            observer = self.observation(path)
            with patch.object(guard, 'process_identity', return_value=None), \
                    patch.object(observer, '_has_exited', return_value=True):
                observer.sample(SimpleNamespace(owned={42: (100, 7)}))
            self.assertEqual(self.rows(path), [])
            self.assertEqual(observer.skipped, 1)
            self.assertEqual(observer.missing, 0)

    def test_unreadable_proc_fields_never_turn_into_zero(self):
        def read(path):
            if str(path).endswith('/status'):
                return 'Name:\tchild\n'  # No VmRSS, not VmRSS=0.
            return 'child\n'
        with patch.object(guard.Path, 'read_text', read), \
                patch.object(guard.os, 'scandir', side_effect=PermissionError('FD_UNAVAILABLE')):
            rss, descriptors, command, missing = guard.ProcessSamplesObservation._process_values(42)
        self.assertIsNone(rss)
        self.assertIsNone(descriptors)
        self.assertEqual(command, 'child')
        self.assertEqual(missing, ['rss', 'fd'])

    def test_invalid_interval_refuses_before_creating_the_file(self):
        with tempfile.TemporaryDirectory() as directory:
            for index, value in enumerate((0., -1., float('nan'), float('inf'))):
                path = Path(directory) / f'{index}.tsv'
                with self.subTest(value=value), self.assertRaises(ValueError):
                    guard.ProcessSamplesObservation(path, value, 0.)
                self.assertFalse(path.exists())


class GpuMemoryObservationTests(unittest.TestCase):
    def observation(self, text):
        with patch.object(guard.subprocess, 'run', return_value=
                          subprocess.CompletedProcess([], 0, text, '')) as run:
            result = guard.GpuMemoryObservation('smi', 'GPU-test')
            self.assertEqual(run.call_args.args[0][1], '--id=GPU-test')
            return result

    def test_device_scope_keeps_display_baseline_and_reserved_memory(self):
        observer = self.observation('GPU-test, 8192, 1700, 6300\n')
        with patch.object(guard.subprocess, 'run', return_value=
                          subprocess.CompletedProcess([], 0, 'GPU-test, 8192, 2400, 5600\n', '')):
            observer.sample()
        self.assertEqual(observer.baseline_used, 1700)
        self.assertEqual(observer.peak_used, 2400)
        self.assertEqual(observer.minimum_free, 5600)
        self.assertEqual(observer.samples, 2)
        self.assertIn('scope=whole_device', observer.summary())

    def test_missing_malformed_or_multiple_counters_never_become_zero(self):
        for text in ('', 'GPU-test, 8192, N/A, 6300', 'GPU-test, 8192, -1, 6300',
                     'GPU-test, 8192, 7000, 2000', 'GPU-test, 0, 0, 0',
                     'GPU-test, 8192, 1000, 7000\nGPU-other, 8192, 1000, 7000'):
            with self.subTest(text=text), self.assertRaises(RuntimeError):
                self.observation(text)

    def test_identity_or_capacity_change_is_rejected(self):
        for text in ('GPU-other, 8192, 1700, 6300', 'GPU-test, 16384, 1700, 14600'):
            observer = self.observation('GPU-test, 8192, 1700, 6300')
            with patch.object(guard.subprocess, 'run', return_value=
                              subprocess.CompletedProcess([], 0, text, '')):
                with self.assertRaises(RuntimeError):
                    observer.sample()

    def test_requested_telemetry_error_or_timeout_is_not_ignored(self):
        with patch.object(guard.subprocess, 'run', return_value=
                          subprocess.CompletedProcess([], 1, '', 'driver unavailable')):
            with self.assertRaisesRegex(RuntimeError, 'driver unavailable'):
                guard.GpuMemoryObservation('smi', '0')
        with patch.object(guard.subprocess, 'run', side_effect=subprocess.TimeoutExpired('smi', 2)):
            with self.assertRaises(subprocess.TimeoutExpired):
                guard.GpuMemoryObservation('smi', '0')


class OutputStorageObservationTests(unittest.TestCase):
    """Logical output tallies stay inside one pinned, non-followed directory."""

    def observation(self, root, budget_mib=1, reserve_mib=0):
        return guard.OutputStorageObservation(root, budget_mib, reserve_mib)

    def test_regular_file_st_size_is_tallied_and_peaked(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'out'
            (root / 'nested').mkdir(parents=True)
            (root / 'a.bin').write_bytes(b'a' * 1024)
            (root / 'nested' / 'b.bin').write_bytes(b'b' * 512)
            observer = self.observation(root, budget_mib=1.)
            self.assertEqual(observer.current_bytes, 1536)
            self.assertEqual(observer.peak_bytes, 1536)
            (root / 'nested' / 'b.bin').unlink()
            observer.sample()
            self.assertEqual(observer.current_bytes, 1024)
            self.assertEqual(observer.peak_bytes, 1536)
            self.assertIn('scope=directory', observer.summary())

    def test_next_write_reservation_shifts_the_effective_budget(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'out'
            root.mkdir()
            (root / 'a.bin').write_bytes(b'a' * 1024)
            self.assertFalse(self.observation(root, budget_mib=.001).over_budget())
            self.assertTrue(self.observation(root, budget_mib=.001,
                                              reserve_mib=.001).over_budget())

    def test_requested_root_must_be_a_preexisting_real_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, 'inaccessible'):
                self.observation(Path(directory) / 'missing')
            real = Path(directory) / 'real'
            real.mkdir()
            link = Path(directory) / 'link'
            link.symlink_to(real)
            with self.assertRaisesRegex(RuntimeError, 'preexisting real directory'):
                self.observation(link)

    def test_symlink_entry_inside_the_tree_fails_closed_without_following(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'out'
            root.mkdir()
            (root / 'link').symlink_to(Path(directory) / 'outside.bin')
            with self.assertRaisesRegex(RuntimeError, 'symlink'):
                self.observation(root)

    def test_unsupported_entry_type_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'out'
            root.mkdir()
            os.mkfifo(root / 'pipe')
            with self.assertRaisesRegex(RuntimeError, 'unsupported entry type'):
                self.observation(root)

    def test_changed_pinned_root_identity_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'out'
            root.mkdir()
            observer = self.observation(root)
            pinned = os.stat(root, follow_symlinks=False)
            with patch.object(guard.os, 'stat', return_value=SimpleNamespace(
                    st_dev=pinned.st_dev, st_ino=pinned.st_ino + 1, st_mode=pinned.st_mode)):
                with self.assertRaisesRegex(RuntimeError, 'identity changed'):
                    observer.sample()

    def test_nonfinite_or_nonpositive_budget_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'out'
            root.mkdir()
            for budget in (float('nan'), float('inf'), 0, -1):
                with self.subTest(budget=budget), self.assertRaises(RuntimeError):
                    self.observation(root, budget_mib=budget)

    def test_root_substituted_by_a_symlink_fails_closed(self):
        # Deterministic substitution, not a raced thread: the pinned path is
        # renamed away and a symlink is put in its place between samples.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'out'
            root.mkdir()
            outside = Path(directory) / 'outside'
            outside.mkdir()
            (outside / 'hidden.bin').write_bytes(b'h' * 4096)
            observer = self.observation(root)
            os.rename(root, Path(directory) / 'moved')
            root.symlink_to(outside, target_is_directory=True)
            with self.assertRaisesRegex(RuntimeError, 'identity changed|symlink|inaccessible'):
                observer.sample()
            self.assertEqual(observer.current_bytes, 0)

    def test_root_substituted_by_another_directory_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'out'
            root.mkdir()
            observer = self.observation(root)
            os.rename(root, Path(directory) / 'moved')
            root.mkdir()
            with self.assertRaisesRegex(RuntimeError, 'identity changed'):
                observer.sample()

    def test_child_directory_substituted_by_a_symlink_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'out'
            child = root / 'child'
            child.mkdir(parents=True)
            outside = Path(directory) / 'outside'
            outside.mkdir()
            (outside / 'hidden.bin').write_bytes(b'h' * 4096)
            observer = self.observation(root)
            os.rename(child, root / 'moved')
            child.symlink_to(outside, target_is_directory=True)
            with self.assertRaisesRegex(RuntimeError, 'symlink'):
                observer.sample()

    def test_mocked_directory_identity_mismatch_fails_closed(self):
        # A mocked fd stat stands in for a directory swapped under the walk, so
        # the identity check is exercised without any probabilistic race.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'out'
            root.mkdir()
            observer = self.observation(root)
            pinned = os.stat(root, follow_symlinks=False)
            with patch.object(guard.os, 'fstat', return_value=SimpleNamespace(
                    st_dev=pinned.st_dev, st_ino=pinned.st_ino + 1, st_mode=pinned.st_mode)):
                with self.assertRaisesRegex(RuntimeError, 'identity changed'):
                    observer.sample()


class HostStorageObservationTests(unittest.TestCase):
    """Actual free space comes from the Linux mount, not a Windows drive letter."""

    def test_minimum_free_space_is_tracked_from_disk_usage(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with patch.object(guard.shutil, 'disk_usage',
                              return_value=SimpleNamespace(free=9 * 1024 * 1024)):
                observer = guard.HostStorageObservation(root, 1)
            self.assertEqual(observer.initial_free, 9 * 1024 * 1024)
            with patch.object(guard.shutil, 'disk_usage',
                              return_value=SimpleNamespace(free=4 * 1024 * 1024)):
                observer.sample()
            self.assertEqual(observer.minimum_free, 4 * 1024 * 1024)
            self.assertIn('scope=linux_mount', observer.summary())

    def test_minimum_threshold_and_reservation_bound_the_stop(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            free = 3 * 1024 * 1024
            with patch.object(guard.shutil, 'disk_usage',
                              return_value=SimpleNamespace(free=free)):
                self.assertFalse(guard.HostStorageObservation(root, 2).under_minimum())
                self.assertTrue(guard.HostStorageObservation(root, 4).under_minimum())
                self.assertTrue(guard.HostStorageObservation(
                    root, 2, reserve_mib=2).under_minimum())

    def test_requested_root_and_free_space_reading_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, 'inaccessible'):
                guard.HostStorageObservation(Path(directory) / 'missing', 1)
            real = Path(directory) / 'real'
            real.mkdir()
            link = Path(directory) / 'link'
            link.symlink_to(real)
            with self.assertRaisesRegex(RuntimeError, 'preexisting real directory'):
                guard.HostStorageObservation(link, 1)
            with patch.object(guard.shutil, 'disk_usage',
                              side_effect=OSError(5, 'FREE_SPACE_CONTROL')):
                with self.assertRaisesRegex(RuntimeError, 'FREE_SPACE_CONTROL'):
                    guard.HostStorageObservation(real, 1)
            with patch.object(guard.shutil, 'disk_usage',
                              return_value=SimpleNamespace(free=float('nan'))):
                with self.assertRaisesRegex(RuntimeError, 'finite'):
                    guard.HostStorageObservation(real, 1)

    def test_changed_pinned_root_identity_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            real = Path(directory) / 'real'
            real.mkdir()
            observer = guard.HostStorageObservation(real, 1)
            pinned = os.stat(real, follow_symlinks=False)
            with patch.object(guard.os, 'stat', return_value=SimpleNamespace(
                    st_dev=pinned.st_dev, st_ino=pinned.st_ino + 1, st_mode=pinned.st_mode)):
                with self.assertRaisesRegex(RuntimeError, 'identity changed'):
                    observer.sample()

    def test_host_root_substituted_by_another_directory_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'host'
            root.mkdir()
            observer = guard.HostStorageObservation(root, 1)
            os.rename(root, Path(directory) / 'moved')
            root.mkdir()
            with self.assertRaisesRegex(RuntimeError, 'identity changed'):
                observer.sample()

# This child escapes the command's session and ignores TERM to exercise KILL.
LEAF = r'''
import json, os, pathlib, signal, sys, time
signal.signal(signal.SIGTERM, signal.SIG_IGN)
record = {'pid': os.getpid(), 'pgid': os.getpgid(0),
          'start': pathlib.Path('/proc/self/stat').read_text().rsplit(')', 1)[1].split()[19]}
pathlib.Path(sys.argv[1]).write_text(json.dumps(record))
while True:
    time.sleep(1)
'''

# The intermediate process exits immediately: the guard must retain an
# otherwise-unobserved descendant via subreaping, not only a polling snapshot.
MIDDLE = r'''
import subprocess, sys
subprocess.Popen([sys.executable, '-c', sys.argv[1], sys.argv[2]], start_new_session=True)
'''

COMMAND = r'''
import os, pathlib, subprocess, sys, time
subprocess.run([sys.executable, '-c', sys.argv[1], sys.argv[2], sys.argv[3]], check=True)
while not pathlib.Path(sys.argv[3]).exists():
    time.sleep(.005)
print('COMMAND_LOG_MARKER', flush=True)
if sys.argv[4] == 'grow':
    root = pathlib.Path(os.environ['OUTPUT_GROWTH_ROOT'])
    chunk = b'x' * 65536
    for index in range(4096):
        (root / f'part-{index:04d}.bin').write_bytes(chunk)
    while True:
        time.sleep(1)
if sys.argv[4] == 'fastgrow':
    root = pathlib.Path(os.environ['OUTPUT_GROWTH_ROOT'])
    chunk = b'x' * 65536
    for index in range(64):
        (root / f'fast-{index:04d}.bin').write_bytes(chunk)
    raise SystemExit(0)
if sys.argv[4] == 'wait':
    while True:
        time.sleep(1)
raise SystemExit(int(sys.argv[4]))
'''

# Reused by the storage preflight tests: the marker exists only if the guard
# launched the child, which a refused preflight must never do.
MARKER_CHILD = r'''
import pathlib, sys
pathlib.Path(sys.argv[1]).write_text('ran')
'''

RUNNER = r'''
import importlib.util, pathlib, sys
spec = importlib.util.spec_from_file_location('guard_child', sys.argv[1])
guard = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = guard
spec.loader.exec_module(guard)
ready = pathlib.Path(sys.argv[2])
mode = sys.argv[3]
guard.TERMINATE_GRACE_SECONDS = .05
guard.KILL_REAP_TIMEOUT_SECONDS = 2
guard.CLEANUP_POLL_SECONDS = .01
# Deterministic simulated headroom; do not allocate RAM or depend on host load.
guard.memory_kib = lambda: ((0 if mode == 'breach' and ready.exists() else 1024 * 1024), 0)
if mode in ('swap_below', 'swap_above'):
    guard.memory_kib = lambda: (1024 * 1024,
        (128 if mode == 'swap_below' else 300) * 1024 if ready.exists() else 0)
if mode in ('pressure_breach', 'pressure_error'):
    class PressureControl:
        def __init__(self, *_):
            self.complete = False
        def sample(self):
            if ready.exists():
                if mode == 'pressure_error':
                    raise RuntimeError('PRESSURE_FAILURE_CONTROL')
                return 'io_pressure'
        def summary(self):
            return f'SYSTEM_PRESSURE_OBSERVATION complete={self.complete}'
    guard.SystemPressureObservation = PressureControl
if mode == 'gpu_error':
    class FailingTelemetry:
        def __init__(self, *_):
            self.complete = False
        def sample(self):
            if ready.exists():
                raise RuntimeError('GPU_TELEMETRY_FAILURE_CONTROL')
        def summary(self):
            return f'GPU_MEMORY_OBSERVATION complete={self.complete}'
    guard.GpuMemoryObservation = FailingTelemetry
if mode == 'launch_interrupt':
    import signal, time
    original = guard.subprocess.Popen
    def interrupted_launch(*args, **kwargs):
        process = original(*args, **kwargs)
        deadline = time.monotonic() + 5
        while not ready.exists() and process.poll() is None and time.monotonic() < deadline:
            time.sleep(.005)
        # Simulate interruption after fork but before main's Popen assignment.
        raise guard.GuardInterrupted(signal.SIGTERM)
    guard.subprocess.Popen = interrupted_launch
if mode in ('host_low', 'host_error'):
    # Real free space is read at preflight; the live mount is then simulated so
    # no test depends on the actual host filling up.
    real_disk_usage = guard.shutil.disk_usage
    class _Usage:
        def __init__(self, free):
            self.free = free
    def fake_disk_usage(path):
        if ready.exists():
            if mode == 'host_error':
                raise OSError(5, 'HOST_FREE_FAILURE_CONTROL')
            return _Usage(1)
        return real_disk_usage(path)
    guard.shutil.disk_usage = fake_disk_usage
sys.argv = ([sys.argv[1]] + (['--gpu-memory-device', '0'] if mode == 'gpu_error' else [])
            + (['--pressure-guard'] if mode.startswith('pressure_') else []) + sys.argv[4:])
raise SystemExit(guard.main())
'''


@unittest.skipUnless(sys.platform == 'linux' and hasattr(os, 'pidfd_open')
                     and hasattr(signal, 'pidfd_send_signal'), 'Linux pidfds required')
class MemoryGuardTests(unittest.TestCase):
    def test_pidfd_einval_ignores_only_verified_stale_snapshot(self):
        current = guard.ProcessIdentity(42, 9, 100, 'S')
        for error_number, verified, expected_error in (
                (errno.EINVAL, None, False),
                (errno.EINVAL, guard.ProcessIdentity(42, 1, 101, 'S'), False),
                (errno.EINVAL, current, True),
                (errno.EINVAL, guard.ProcessIdentity(42, 9, 100, 'Z'), True),
                (errno.EMFILE, None, True),
                (errno.EPERM, current, True)):
            with self.subTest(error_number=error_number, verified=verified):
                tracker = object.__new__(guard.OwnedDescendants)
                tracker.guard_pid = 9
                tracker.preexisting = set()
                tracker.owned = {}
                with patch.object(guard, 'process_snapshot', return_value={42: current}), \
                        patch.object(guard, 'process_identity', return_value=verified), \
                        patch.object(os, 'pidfd_open', side_effect=
                                     OSError(error_number, 'PIDFD_FAILURE_CONTROL')):
                    if expected_error:
                        with self.assertRaises(OSError):
                            tracker.refresh()
                    else:
                        tracker.refresh()
                self.assertEqual(tracker.owned, {})

    def test_identity_mismatch_never_signals_reused_pid(self):
        tracker = object.__new__(guard.OwnedDescendants)
        tracker.owned = {42: (100, 7)}
        with patch.object(guard, 'process_identity', return_value=
                          guard.ProcessIdentity(42, 1, 101, 'S')), \
                patch.object(signal, 'pidfd_send_signal') as send:
            tracker.send(signal.SIGKILL)
        send.assert_not_called()

    def test_stale_parent_identity_never_claims_unrelated_child(self):
        tracker = object.__new__(guard.OwnedDescendants)
        tracker.guard_pid = 9
        tracker.preexisting = set()
        tracker.owned = {42: (100, 7)}
        snapshot = {42: guard.ProcessIdentity(42, 9, 100, 'S'),
                    43: guard.ProcessIdentity(43, 42, 201, 'S')}
        with patch.object(guard, 'process_snapshot', return_value=snapshot), \
                patch.object(guard, 'process_identity', return_value=
                             guard.ProcessIdentity(42, 1, 101, 'S')), \
                patch.object(os, 'pidfd_open') as open_pid:
            tracker.refresh()
        open_pid.assert_not_called()
        self.assertEqual(set(tracker.owned), {42})

    def run_tree(self, mode, exit_code='wait', interrupt=False, extra_args=(), env=None,
                 poll_seconds='.01'):
        with tempfile.TemporaryDirectory(prefix='arch-memory-guard-test-') as directory:
            ready = Path(directory) / 'leaf.json'
            log = Path(directory) / 'command.log'
            # A sibling is deliberately outside the guard's descendant tree.
            unrelated = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'])
            process = None
            try:
                command = [sys.executable, '-c', RUNNER, str(TOOL), str(ready), mode,
                           '--min-available-mib', '1', '--max-swap-growth-mib',
                           '256' if mode.startswith('swap_') else '0',
                           '--poll-seconds', poll_seconds, '--log', str(log), *extra_args, '--',
                           sys.executable, '-c', COMMAND, MIDDLE, LEAF, str(ready), exit_code]
                process = subprocess.Popen(command, stdout=subprocess.PIPE,
                                           stderr=subprocess.STDOUT, text=True,
                                           env={**os.environ, **env} if env else None)
                if interrupt:
                    deadline = time.monotonic() + 5
                    while not ready.exists() and process.poll() is None and time.monotonic() < deadline:
                        time.sleep(.01)
                    self.assertTrue(ready.exists())
                    process.send_signal(signal.SIGTERM)
                output, _ = process.communicate(timeout=8)
                self.assertTrue(ready.exists(), output)
                record = json.loads(ready.read_text())
                self.assertEqual(record['pid'], record['pgid'])
                # Reaping matters: a zombie still has /proc/PID/stat.
                self.assertIsNone(guard.process_identity(record['pid']), output)
                self.assertIsNone(unrelated.poll(), 'guard killed an unrelated sibling')
                text = log.read_text()
                self.assertIn('MEMORY_GUARD_RESULT', text)
                if mode == 'normal' and not interrupt:
                    self.assertIn('COMMAND_LOG_MARKER', text)
                return process.returncode, output
            finally:
                if process is not None and process.poll() is None:
                    process.terminate()
                    process.communicate(timeout=8)
                unrelated.terminate()
                unrelated.wait(timeout=3)

    def test_memory_breach_reaps_setsid_adopted_descendant(self):
        code, output = self.run_tree('breach')
        self.assertEqual(code, 125, output)
        self.assertIn('guard_stopped=True', output)

    def test_command_failure_reaps_setsid_adopted_descendant(self):
        code, output = self.run_tree('normal', '17')
        self.assertEqual(code, 17, output)
        self.assertIn('guard_stopped=False', output)

    def test_command_success_does_not_leave_daemon(self):
        code, output = self.run_tree('normal', '0')
        self.assertEqual(code, 0, output)

    def test_external_term_cleans_owned_tree(self):
        code, output = self.run_tree('normal', interrupt=True)
        self.assertEqual(code, 128 + signal.SIGTERM, output)

    def test_interrupted_launch_without_popen_handle_cleans_owned_tree(self):
        code, output = self.run_tree('launch_interrupt')
        self.assertEqual(code, 128 + signal.SIGTERM, output)

    def test_telemetry_failure_reaps_owned_tree_and_reports_incomplete(self):
        code, output = self.run_tree('gpu_error')
        self.assertNotEqual(code, 0, output)
        self.assertIn('GPU_TELEMETRY_FAILURE_CONTROL', output)
        self.assertIn('GPU_MEMORY_OBSERVATION complete=False', output)

    def test_pressure_stop_reaps_owned_tree_and_leaves_sibling(self):
        code, output = self.run_tree('pressure_breach')
        self.assertEqual(code, 125, output)
        self.assertIn('stop_reason=io_pressure', output)

    def test_pressure_counter_failure_reaps_owned_tree(self):
        code, output = self.run_tree('pressure_error')
        self.assertNotEqual(code, 0, output)
        self.assertIn('PRESSURE_FAILURE_CONTROL', output)
        self.assertIn('stop_reason=monitor_error', output)
        self.assertIn('MEMORY_GUARD_ERROR RuntimeError:', output)
        self.assertIn('SYSTEM_PRESSURE_OBSERVATION complete=False', output)

    def test_bounded_swap_growth_allows_command_to_finish(self):
        code, output = self.run_tree('swap_below', '0')
        self.assertEqual(code, 0, output)
        self.assertIn('guard_stopped=False', output)

    def test_swap_growth_ceiling_stops_only_owned_tree(self):
        code, output = self.run_tree('swap_above')
        self.assertEqual(code, 125, output)
        self.assertIn('stop_reason=swap_growth', output)

    def test_output_budget_stop_reaps_owned_tree_and_leaves_sibling(self):
        with tempfile.TemporaryDirectory(prefix='arch-output-guard-test-') as workspace:
            growth = Path(workspace) / 'out'
            growth.mkdir()
            code, output = self.run_tree(
                'grow', exit_code='grow',
                extra_args=('--output-root', str(growth), '--max-output-mib', '1',
                            '--next-write-reserve-mib', '0'),
                env={'OUTPUT_GROWTH_ROOT': str(growth)})
            self.assertEqual(code, 125, output)
            self.assertIn('stop_reason=output_bytes', output)
            self.assertIn('OUTPUT_STORAGE_OBSERVATION', output)
            self.assertIn('complete=False', output)
            written = sum(item.stat().st_size for item in growth.iterdir())
            self.assertGreater(written, 1024 * 1024)
            self.assertFalse(growth.is_symlink())

    def test_fast_child_output_breach_is_caught_on_the_final_sample(self):
        # A child that finishes inside one poll interval is never seen by the
        # monitor loop; the end-of-run sample must still stop the run (125)
        # rather than report the child's exit code as success. A deliberately
        # long poll interval makes the final sample the only observation.
        with tempfile.TemporaryDirectory(prefix='arch-output-final-test-') as workspace:
            growth = Path(workspace) / 'out'
            growth.mkdir()
            code, output = self.run_tree(
                'normal', exit_code='fastgrow', poll_seconds='2',
                extra_args=('--output-root', str(growth), '--max-output-mib', '1'),
                env={'OUTPUT_GROWTH_ROOT': str(growth)})
            self.assertEqual(code, 125, output)
            self.assertIn('stop_reason=output_bytes', output)
            self.assertIn('MEMORY_GUARD_STOP', output)
            self.assertIn('OUTPUT_STORAGE_OBSERVATION', output)
            self.assertIn('complete=False', output)
            written = sum(item.stat().st_size for item in growth.iterdir())
            self.assertGreater(written, 1024 * 1024)

    def test_low_host_free_space_stop_reaps_owned_tree(self):
        code, output = self.run_tree(
            'host_low', extra_args=('--host-storage-root', tempfile.gettempdir(),
                                    '--min-host-free-mib', '2'))
        self.assertEqual(code, 125, output)
        self.assertIn('stop_reason=host_free', output)
        self.assertIn('HOST_STORAGE_OBSERVATION', output)

    def test_host_free_monitor_error_keeps_fail_closed_semantics(self):
        code, output = self.run_tree(
            'host_error', extra_args=('--host-storage-root', tempfile.gettempdir(),
                                      '--min-host-free-mib', '2'))
        self.assertNotEqual(code, 0, output)
        self.assertIn('stop_reason=monitor_error', output)
        self.assertIn('HOST_FREE_FAILURE_CONTROL', output)

    def test_run_without_disk_arguments_keeps_legacy_output(self):
        code, output = self.run_tree('normal', '0')
        self.assertEqual(code, 0, output)
        self.assertIn('MEMORY_GUARD_RESULT', output)
        self.assertNotIn('OUTPUT_STORAGE_OBSERVATION', output)
        self.assertNotIn('HOST_STORAGE_OBSERVATION', output)


    def test_real_child_rss_fd_rows_survive_command_failure(self):
        # The child waits for two flushed rows, avoiding scheduler-speed assumptions.
        child = r"""
import csv, json, os, pathlib, sys, time
files = [open(os.devnull) for _ in range(4)]
pathlib.Path(sys.argv[1]).write_text(json.dumps({
    'pid': os.getpid(),
    'start': int(pathlib.Path('/proc/self/stat').read_text().rsplit(')', 1)[1].split()[19])}))
deadline = time.monotonic() + 4
while time.monotonic() < deadline:
    with pathlib.Path(sys.argv[2]).open(newline='') as stream:
        rows = list(csv.DictReader(stream, delimiter='\t'))
    ready = [row for row in rows if row.get('pid') == str(os.getpid())
             and row.get('status') == 'ok' and (row.get('fd_count') or '').isdigit()
             and int(row['fd_count']) >= 7]
    if len(ready) >= 2:
        raise SystemExit(17)
    time.sleep(.01)
raise SystemExit(18)
"""
        with tempfile.TemporaryDirectory(prefix='arch-process-samples-') as directory:
            root = Path(directory)
            marker, samples = root / 'child.json', root / 'samples.tsv'
            result = subprocess.run(
                [sys.executable, '-c', RUNNER, str(TOOL), str(marker), 'normal',
                 '--min-available-mib', '1', '--max-swap-growth-mib', '0',
                 '--poll-seconds', '.01', '--process-samples', str(samples),
                 '--process-sample-seconds', '.02', '--',
                 sys.executable, '-c', child, str(marker), str(samples)],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=8)
            self.assertEqual(result.returncode, 17, result.stdout + result.stderr)
            record = json.loads(marker.read_text())
            with samples.open(newline='') as stream:
                rows = list(csv.DictReader(stream, delimiter='\t'))
            actual = [row for row in rows if row['pid'] == str(record['pid']) and row['status'] == 'ok']
            self.assertGreaterEqual(sum(int(row['fd_count']) >= 7 for row in actual), 2)
            for row in actual:
                self.assertEqual(int(row['start_time']), record['start'])
                self.assertGreater(int(row['rss_kib']), 0)
                self.assertTrue(row['comm'])
            self.assertEqual(sorted(float(row['elapsed_seconds']) for row in actual),
                             [float(row['elapsed_seconds']) for row in actual])
            self.assertIn('PROCESS_SAMPLES_OBSERVATION scope=owned_descendants', result.stdout)
            self.assertIn('leak_freedom_proven=False', result.stdout)
            self.assertIsNone(guard.process_identity(record['pid']))

    def test_process_samples_survive_guard_stop_without_changing_tree_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'samples.tsv'
            code, output = self.run_tree('breach', extra_args=(
                '--process-samples', str(path), '--process-sample-seconds', '.01'))
            self.assertEqual(code, 125, output)
            self.assertTrue(path.read_text().startswith('elapsed_seconds\tpid\t'))
            summaries = [line for line in output.splitlines() if line.startswith('PROCESS_SAMPLES_OBSERVATION')]
            self.assertEqual(len(summaries), 1)
            self.assertIn('complete=False', summaries[0])

    def test_default_run_has_no_process_sample_summary(self):
        code, output = self.run_tree('normal', '0')
        self.assertEqual(code, 0, output)
        self.assertNotIn('PROCESS_SAMPLES_OBSERVATION', output)


@unittest.skipUnless(sys.platform == 'linux', 'Linux guard required')
class StoragePreflightTests(unittest.TestCase):
    """A refused storage preflight must never launch the child process."""

    def run_preflight(self, extra_args, command):
        with tempfile.TemporaryDirectory(prefix='arch-storage-preflight-') as directory:
            return subprocess.run(
                [sys.executable, '-c', RUNNER, str(TOOL),
                 str(Path(directory) / 'ready.json'), 'preflight',
                 '--min-available-mib', '1', '--max-swap-growth-mib', '0',
                 '--poll-seconds', '.01', *extra_args, '--', *command],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=30)

    def assert_refused_without_child(self, extra_args, expected_message):
        with tempfile.TemporaryDirectory(prefix='arch-storage-marker-') as directory:
            marker = Path(directory) / 'child-marker'
            result = self.run_preflight(extra_args, [sys.executable, '-c', MARKER_CHILD, str(marker)])
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn(expected_message, result.stderr)
            self.assertFalse(marker.exists(), 'child ran despite refused preflight')

    def test_missing_output_root_refuses_before_launching_child(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assert_refused_without_child(
                ('--output-root', str(Path(directory) / 'absent'), '--max-output-mib', '1'),
                'cannot establish requested output/host storage observation')

    def test_symlinked_output_root_refuses_before_launching_child(self):
        with tempfile.TemporaryDirectory() as directory:
            real = Path(directory) / 'real'
            real.mkdir()
            link = Path(directory) / 'link'
            link.symlink_to(real)
            self.assert_refused_without_child(
                ('--output-root', str(link), '--max-output-mib', '1'),
                'cannot establish requested output/host storage observation')

    def test_missing_host_root_refuses_before_launching_child(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assert_refused_without_child(
                ('--host-storage-root', str(Path(directory) / 'absent'), '--min-host-free-mib', '1'),
                'cannot establish requested output/host storage observation')

    def test_storage_arguments_are_validated_as_pairs(self):
        for extra in (('--output-root', tempfile.gettempdir()), ('--max-output-mib', '1'),
                      ('--host-storage-root', tempfile.gettempdir()), ('--min-host-free-mib', '1')):
            with self.subTest(extra=extra):
                result = self.run_preflight(extra, [sys.executable, '-c', 'pass'])
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)

    def test_nonpositive_budget_and_orphaned_reservation_are_refused(self):
        for extra in (('--output-root', tempfile.gettempdir(), '--max-output-mib', '0'),
                      ('--host-storage-root', tempfile.gettempdir(), '--min-host-free-mib', '0'),
                      ('--next-write-reserve-mib', '5'),
                      ('--next-write-reserve-mib=-1',)):
            with self.subTest(extra=extra):
                result = self.run_preflight(extra, [sys.executable, '-c', 'pass'])
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)

    def test_output_already_over_budget_refuses_before_launching_child(self):
        # A tree that already exceeds its budget must be refused up front, not
        # launch the child and only then notice at the first poll.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'out'
            root.mkdir()
            (root / 'existing.bin').write_bytes(b'z' * (2 * 1024 * 1024))
            self.assert_refused_without_child(
                ('--output-root', str(root), '--max-output-mib', '1'),
                'already exceeds the configured budget before launch')

    def test_host_already_below_minimum_refuses_before_launching_child(self):
        # An impossible minimum is guaranteed to be under any real free space,
        # so this refusal does not depend on the live host's occupancy.
        self.assert_refused_without_child(
            ('--host-storage-root', tempfile.gettempdir(), '--min-host-free-mib', '1000000000'),
            'already below the configured minimum before launch')


    def test_existing_process_samples_file_refuses_without_overwrite_or_child(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'samples.tsv'
            path.write_text('KEEP_EXISTING_SAMPLES\n')
            self.assert_refused_without_child(('--process-samples', str(path)),
                                               'cannot establish requested process sampling')
            self.assertEqual(path.read_text(), 'KEEP_EXISTING_SAMPLES\n')

    def test_invalid_process_sample_interval_refuses_without_child(self):
        for value in ('0', '-1', 'nan', 'inf'):
            with self.subTest(value=value):
                self.assert_refused_without_child((f'--process-sample-seconds={value}',),
                                                   'must be finite and positive')


if __name__ == '__main__':
    unittest.main()
