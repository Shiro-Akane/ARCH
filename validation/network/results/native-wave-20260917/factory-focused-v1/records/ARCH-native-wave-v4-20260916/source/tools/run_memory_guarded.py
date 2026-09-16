#!/usr/bin/env python3
"""Run one build/test descendant tree with explicit system-memory headroom.

This is a safety guard, not a capacity qualification. It samples system-wide
MemAvailable and swap usage; it cannot prove a workload never used swap between
samples. Linux subreaping retains children that escape into a new session or
outlive their parent. Only descendants owned by this invocation are signalled,
using PID/start-time checks and pidfds, never a process-group-wide kill.
"""

import argparse
import csv
import ctypes
from dataclasses import dataclass
import errno
import os
import math
from pathlib import Path
import signal
import subprocess
import time


TERMINATE_GRACE_SECONDS = 2.0
KILL_REAP_TIMEOUT_SECONDS = 5.0
CLEANUP_POLL_SECONDS = 0.05


def pressure_counters():
    """Linux-wide PSI full-stall microseconds and swapped pages, not disk load.

    https://www.kernel.org/doc/html/latest/accounting/psi.html defines `full`
    as simultaneous stalls of all non-idle tasks. Windows disk utilization is
    outside these WSL counters; high throughput alone is not a stall signal.
    """
    values = []
    for resource in ('memory', 'io'):
        rows = [line.split() for line in Path(f'/proc/pressure/{resource}').read_text().splitlines()
                if line.startswith('full ')]
        if len(rows) != 1:
            raise RuntimeError(f'{resource} full pressure counter is unavailable')
        fields = dict(item.split('=', 1) for item in rows[0][1:])
        values.append(int(fields['total']))
    vmstat = dict(line.split() for line in Path('/proc/vmstat').read_text().splitlines())
    values.extend(int(vmstat[key]) for key in ('pswpin', 'pswpout'))
    if min(values) < 0:
        raise RuntimeError('negative system pressure counter')
    return tuple(values)


class SystemPressureObservation:
    """Stop sustained stalls, while allowing bounded, productive swap use."""

    def __init__(self, memory_limit, io_limit, duration):
        self.limits = (memory_limit, io_limit)
        self.duration = duration
        self.previous = pressure_counters()
        self.previous_time = time.monotonic()
        self.page_mib = os.sysconf('SC_PAGE_SIZE') / (1024 * 1024)
        self.high_seconds = [0., 0.]
        self.peaks = [0., 0., 0., 0.]
        self.samples = 0
        self.complete = False

    def sample(self):
        counters = pressure_counters()
        now = time.monotonic()
        elapsed = now - self.previous_time
        if elapsed <= 0:
            raise RuntimeError('nonpositive system pressure sampling interval')
        delta = [value - previous for value, previous in zip(counters, self.previous)]
        if min(delta) < 0:
            raise RuntimeError('system pressure counters moved backwards')
        # PSI totals are microseconds: 100 * delta_us / (elapsed_s * 1e6)
        # gives the percentage of the sampling interval spent fully stalled.
        # Swap counters are pages and are converted separately to MiB/s.
        rates = [min(100., value / (elapsed * 10000.)) for value in delta[:2]]
        rates.extend(value * self.page_mib / elapsed for value in delta[2:])
        self.peaks = [max(peak, rate) for peak, rate in zip(self.peaks, rates)]
        self.previous, self.previous_time = counters, now
        self.samples += 1
        for index, limit in enumerate(self.limits):
            self.high_seconds[index] = (self.high_seconds[index] + elapsed
                                        if rates[index] >= limit else 0.)
        for index, name in enumerate(('memory_pressure', 'io_pressure')):
            if self.high_seconds[index] >= self.duration:
                return name
        return None

    def summary(self):
        return ('SYSTEM_PRESSURE_OBSERVATION scope=linux_system '
                f'peak_memory_full_percent={self.peaks[0]:.3f} '
                f'peak_io_full_percent={self.peaks[1]:.3f} '
                f'peak_swap_in_mib_s={self.peaks[2]:.3f} '
                f'peak_swap_out_mib_s={self.peaks[3]:.3f} '
                f'memory_limit_percent={self.limits[0]} io_limit_percent={self.limits[1]} '
                f'sustain_seconds={self.duration} samples={self.samples} complete={self.complete}')


class GpuMemoryObservation:
    """Optional whole-device telemetry, never attributed solely to the child.

    Display/other applications and driver reservations can contribute. Sampling
    can miss transient allocations; this is not an allocation-peak guarantee.
    A requested but unavailable counter fails closed rather than reporting zero.
    """

    def __init__(self, executable, selector):
        self.executable, self.selector = executable, selector
        self.uuid = None
        self.samples = 0
        self.complete = False
        self.sample()

    def sample(self):
        result = subprocess.run(
            [self.executable, f'--id={self.selector}',
             '--query-gpu=uuid,memory.total,memory.used,memory.free',
             '--format=csv,noheader,nounits'],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=2, check=False)
        if result.returncode:
            raise RuntimeError('requested GPU memory query failed: ' + result.stderr.strip())
        rows = list(csv.reader(result.stdout.strip().splitlines(), skipinitialspace=True))
        if len(rows) != 1 or len(rows[0]) != 4:
            raise RuntimeError('GPU memory query must identify exactly one device')
        uuid = rows[0][0]
        try:
            total, used, free = map(int, rows[0][1:])
        except ValueError as error:
            raise RuntimeError('GPU memory counters are unavailable or malformed') from error
        if not uuid.startswith('GPU-') or total <= 0 or min(used, free) < 0 \
                or max(used, free) > total or used + free > total:
            raise RuntimeError('GPU memory counters are inconsistent')
        if self.uuid is None:
            self.uuid, self.total, self.baseline_used = uuid, total, used
            self.peak_used, self.minimum_free = used, free
        elif self.uuid != uuid or self.total != total:
            raise RuntimeError('observed GPU identity or memory capacity changed')
        self.peak_used = max(self.peak_used, used)
        self.minimum_free = min(self.minimum_free, free)
        self.samples += 1

    def summary(self):
        return (f'GPU_MEMORY_OBSERVATION scope=whole_device uuid={self.uuid} '
                f'total_mib={self.total} baseline_used_mib={self.baseline_used} '
                f'peak_used_mib={self.peak_used} min_free_mib={self.minimum_free} '
                f'samples={self.samples} complete={self.complete}')


@dataclass(frozen=True)
class ProcessIdentity:
    pid: int
    parent: int
    start_time: int
    state: str


def process_identity(pid):
    """Read Linux stat without assuming the parenthesized comm has no spaces."""
    try:
        text = Path(f'/proc/{pid}/stat').read_text()
        fields = text[text.rindex(')') + 2:].split()
        return ProcessIdentity(pid, int(fields[1]), int(fields[19]), fields[0])
    except (OSError, ValueError, IndexError):
        return None


def process_snapshot():
    result = {}
    for entry in Path('/proc').iterdir():
        if entry.name.isdecimal():
            identity = process_identity(int(entry.name))
            if identity is not None:
                result[identity.pid] = identity
    return result


def enable_subreaper():
    """Fail before launching if safe ownership primitives are unavailable."""
    if not hasattr(os, 'pidfd_open') or not hasattr(signal, 'pidfd_send_signal'):
        raise RuntimeError('this Linux memory guard requires Python pidfd support')
    descriptor = os.pidfd_open(os.getpid())
    os.close(descriptor)
    # PR_SET_CHILD_SUBREAPER: orphaned descendants reparent to this guard,
    # rather than init, even when an intermediate compiler called setsid().
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(36, 1, 0, 0, 0) != 0:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error))


class OwnedDescendants:
    def __init__(self):
        enable_subreaper()
        self.guard_pid = os.getpid()
        self.preexisting = {
            (item.pid, item.start_time) for item in process_snapshot().values()
            if item.parent == self.guard_pid
        }
        self.owned = {}  # PID -> (start time, pidfd); descriptors pin identity.

    def refresh(self):
        snapshot = process_snapshot()
        for pid, (start_time, descriptor) in list(self.owned.items()):
            current = snapshot.get(pid)
            if current is None or current.start_time != start_time:
                os.close(descriptor)
                del self.owned[pid]
        pending = True
        while pending:
            pending = False
            for current in snapshot.values():
                if current.pid in self.owned:
                    continue
                adopted = (current.parent == self.guard_pid
                           and (current.pid, current.start_time) not in self.preexisting)
                if not adopted:
                    parent = process_identity(current.parent)
                    expected = self.owned.get(current.parent)
                    if (expected is None or parent is None
                            or parent.start_time != expected[0]):
                        continue
                try:
                    descriptor = os.pidfd_open(current.pid)
                except ProcessLookupError:
                    continue
                except OSError as error:
                    # Older kernels can return EINVAL, not ESRCH, when a
                    # process is reaped between PID lookup and the TGID check.
                    # Ignore only a verified stale snapshot; a live identity
                    # that cannot be pinned must still fail closed (including EINVAL).
                    verified = process_identity(current.pid)
                    if error.errno == errno.EINVAL and (verified is None
                            or verified.start_time != current.start_time):
                        continue
                    raise
                verified = process_identity(current.pid)
                if verified is None or verified.start_time != current.start_time:
                    os.close(descriptor)
                    continue
                self.owned[current.pid] = (current.start_time, descriptor)
                pending = True

    def send(self, signum):
        for pid, (start_time, descriptor) in self.owned.items():
            current = process_identity(pid)
            if (current is not None and current.start_time == start_time
                    and current.state not in ('Z', 'X')):
                try:
                    signal.pidfd_send_signal(descriptor, signum)
                except ProcessLookupError:
                    pass

    def reap(self, process):
        # Popen owns its direct child's exit status. Reap only other known,
        # adopted descendants here, never unrelated preexisting children.
        if process is not None:
            process.poll()
        for pid in list(self.owned):
            if process is None or pid != process.pid:
                try:
                    os.waitpid(pid, os.WNOHANG)
                except ChildProcessError:
                    pass

    def cleanup(self, process):
        """Also clean escaped children after command failure or normal exit."""
        terminate_until = time.monotonic() + TERMINATE_GRACE_SECONDS
        kill_until = terminate_until + KILL_REAP_TIMEOUT_SECONDS
        try:
            while True:
                self.refresh()
                self.reap(process)
                self.refresh()
                if not self.owned:
                    return
                now = time.monotonic()
                self.send(signal.SIGTERM if now < terminate_until else signal.SIGKILL)
                if now >= kill_until:
                    raise RuntimeError(
                        f'owned descendants did not exit/reap after SIGKILL: {sorted(self.owned)}')
                time.sleep(CLEANUP_POLL_SECONDS)
        finally:
            for _, descriptor in self.owned.values():
                os.close(descriptor)
            self.owned.clear()


class GuardInterrupted(BaseException):
    def __init__(self, signum):
        self.signum = signum


def interrupted(signum, _frame):
    raise GuardInterrupted(signum)


def memory_kib():
    values = {}
    for line in Path('/proc/meminfo').read_text().splitlines():
        key, value = line.split(':', 1)
        values[key] = int(value.split()[0])
    return values['MemAvailable'], values['SwapTotal'] - values['SwapFree']


def owned_rss_kib(descendants):
    """Sample owned processes only. RSS sums can double-count shared pages;
    system MemAvailable remains the safety authority, not this diagnostic."""
    total = 0
    for pid, (start_time, _) in descendants.owned.items():
        identity = process_identity(pid)
        if identity is None or identity.start_time != start_time:
            continue
        try:
            for line in Path(f'/proc/{pid}/status').read_text().splitlines():
                if line.startswith('VmRSS:'):
                    total += int(line.split()[1])
                    break
        except (OSError, ValueError):
            pass  # Exited between identity and RSS sampling.
    return total


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--min-available-mib', type=int, required=True)
    parser.add_argument('--max-swap-growth-mib', type=int, required=True)
    parser.add_argument('--poll-seconds', type=float, default=1.0)
    parser.add_argument('--log', type=Path,
                        help='save child output and the memory summary in a new log file')
    parser.add_argument('--gpu-memory-device',
                        help='optional nvidia-smi device index or UUID to observe; not a CUDA_VISIBLE_DEVICES ordinal')
    parser.add_argument('--nvidia-smi', default='nvidia-smi',
                        help='telemetry executable; only used with --gpu-memory-device')
    parser.add_argument('--pressure-guard', action='store_true',
                        help='also stop sustained Linux memory/I/O stalls; requires PSI counters')
    parser.add_argument('--max-memory-stall-percent', type=float, default=20.)
    parser.add_argument('--max-io-stall-percent', type=float, default=50.)
    parser.add_argument('--pressure-seconds', type=float, default=10.,
                        help='consecutive high-pressure time before stopping the owned command')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command or args.min_available_mib <= 0 or args.max_swap_growth_mib < 0 \
            or not math.isfinite(args.poll_seconds) or args.poll_seconds <= 0:
        parser.error('a command, positive headroom/poll interval and nonnegative swap allowance are required')
    if (not all(math.isfinite(value) and 0 < value <= 100 for value in
                (args.max_memory_stall_percent, args.max_io_stall_percent))
            or not math.isfinite(args.pressure_seconds) or args.pressure_seconds <= 0):
        parser.error('stall limits must be in (0,100] and pressure duration must be positive')
    available, baseline_swap = memory_kib()
    if available < args.min_available_mib * 1024:
        parser.error('insufficient MemAvailable to start safely')
    try:
        gpu = (GpuMemoryObservation(args.nvidia_smi, args.gpu_memory_device)
               if args.gpu_memory_device is not None else None)
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        parser.error(f'cannot establish requested GPU telemetry: {error}')
    try:
        pressure = (SystemPressureObservation(args.max_memory_stall_percent,
                    args.max_io_stall_percent, args.pressure_seconds)
                    if args.pressure_guard else None)
    except (OSError, RuntimeError, ValueError, KeyError) as error:
        parser.error(f'cannot establish requested system pressure telemetry: {error}')
    minimum, peak_swap = available, baseline_swap
    peak_owned_rss = 0
    started = time.monotonic()
    try:
        descendants = OwnedDescendants()
    except (OSError, RuntimeError) as error:
        parser.error(f'cannot establish safe child ownership: {error}')
    log = args.log.open('x', encoding='utf-8') if args.log else None
    process = None
    breached = False
    stop_reason = 'none'
    code = 1
    handlers = {signum: signal.signal(signum, interrupted)
                for signum in (signal.SIGTERM, signal.SIGINT)}
    try:
        process = subprocess.Popen(command, start_new_session=True,
                                   stdout=log, stderr=subprocess.STDOUT if log else None)
        while process.poll() is None:
            descendants.refresh()
            peak_owned_rss = max(peak_owned_rss, owned_rss_kib(descendants))
            available, swap = memory_kib()
            minimum, peak_swap = min(minimum, available), max(peak_swap, swap)
            pressure_reason = pressure.sample() if pressure else None
            if available < args.min_available_mib * 1024:
                stop_reason = 'available_memory'
            elif swap - baseline_swap > args.max_swap_growth_mib * 1024:
                stop_reason = 'swap_growth'
            elif pressure_reason:
                stop_reason = pressure_reason
            if stop_reason != 'none':
                breached = True
                print(f'MEMORY_GUARD_STOP reason={stop_reason}: terminating owned descendants', flush=True)
                break
            if gpu:
                gpu.sample()
            time.sleep(args.poll_seconds)
        if not breached:
            code = process.returncode
            if pressure:
                pressure.sample()
                pressure.complete = True
            if gpu:
                gpu.sample()
                gpu.complete = True
    except GuardInterrupted as error:
        code = 128 + error.signum
        stop_reason = f'signal_{error.signum}'
    except Exception as error:
        stop_reason = 'monitor_error'
        message = f'MEMORY_GUARD_ERROR {type(error).__name__}: {error}'
        print(message, flush=True)
        if log:
            log.write(message + '\n')
        raise
    finally:
        # A second TERM/INT must not interrupt descendant cleanup. SIGKILL
        # cannot be intercepted; external users should stop the guard via TERM.
        for signum in handlers:
            signal.signal(signum, signal.SIG_IGN)
        try:
            # Popen can be interrupted after fork but before assignment. The
            # subreaper still owns that child even without a Popen handle.
            descendants.cleanup(process)
        finally:
            for signum, handler in handlers.items():
                signal.signal(signum, handler)
            summary = (f'MEMORY_GUARD_RESULT min_available_kib={minimum} '
                       f'baseline_swap_kib={baseline_swap} peak_swap_kib={peak_swap} '
                       f'guard_stopped={breached} peak_owned_rss_kib={peak_owned_rss} '
                       f'elapsed_seconds={time.monotonic() - started:.3f} stop_reason={stop_reason}')
            print(summary, flush=True)
            if gpu:
                print(gpu.summary(), flush=True)
            if pressure:
                print(pressure.summary(), flush=True)
            if log:
                log.write(summary + '\n')
                if gpu:
                    log.write(gpu.summary() + '\n')
                if pressure:
                    log.write(pressure.summary() + '\n')
                log.close()
    return 125 if breached else code


if __name__ == '__main__':
    raise SystemExit(main())
