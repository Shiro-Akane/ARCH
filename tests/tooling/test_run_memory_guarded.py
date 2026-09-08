"""Linux-only regressions; children sleep and use no meaningful extra RAM."""

import importlib.util
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
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
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
import pathlib, subprocess, sys, time
subprocess.run([sys.executable, '-c', sys.argv[1], sys.argv[2], sys.argv[3]], check=True)
while not pathlib.Path(sys.argv[3]).exists():
    time.sleep(.005)
print('COMMAND_LOG_MARKER', flush=True)
if sys.argv[4] == 'wait':
    while True:
        time.sleep(1)
raise SystemExit(int(sys.argv[4]))
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

    def run_tree(self, mode, exit_code='wait', interrupt=False):
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
                           '--poll-seconds', '.01', '--log', str(log), '--',
                           sys.executable, '-c', COMMAND, MIDDLE, LEAF, str(ready), exit_code]
                process = subprocess.Popen(command, stdout=subprocess.PIPE,
                                           stderr=subprocess.STDOUT, text=True)
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


if __name__ == '__main__':
    unittest.main()
