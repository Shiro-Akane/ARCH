#!/usr/bin/env python3
"""Run one build/test descendant tree with explicit system-memory headroom.

This is a safety guard, not a capacity qualification. It samples system-wide
MemAvailable and swap usage; it cannot prove a workload never used swap between
samples. Linux subreaping retains children that escape into a new session or
outlive their parent. Only descendants owned by this invocation are signalled,
using PID/start-time checks and pidfds, never a process-group-wide kill.

Optional output-directory and host-mount observation extend the same guard
without changing memory or ownership behaviour. Both are sampled between polls,
so neither can bound what a writer allocates between two samples; they are
detectors with the stated sampling limitation, not allocation guarantees.
"""

import argparse
import csv
import ctypes
from dataclasses import dataclass
import errno
import os
import select
import math
from pathlib import Path
import signal
import shutil
import stat
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


class ProcessSamplesObservation:
    """Optional per-process rows from the existing pinned descendant owner.

    Reads are bracketed by PID/start-time checks, not an atomic process snapshot.
    Exited/reused identities may be skipped. Missing live data remains explicit;
    neither a complete finite record nor stable samples prove leak freedom.
    """

    def __init__(self, path, seconds, started):
        if not math.isfinite(seconds) or seconds <= 0:
            raise ValueError('process sampling interval must be finite and positive')
        self.seconds, self.started, self.next_due = seconds, started, started
        self.samples = self.rows = self.valid_rows = self.missing = self.skipped = 0
        self.peak_rss_kib = self.peak_fd_count = None
        self.complete = False
        self.write_error = None
        self.stream = Path(path).open('x', encoding='utf-8', newline='')
        self.writer = csv.writer(self.stream, delimiter='\t', lineterminator='\n')
        try:
            self.writer.writerow(('elapsed_seconds', 'pid', 'start_time', 'parent',
                                  'rss_kib', 'fd_count', 'comm', 'status'))
            self.stream.flush()
        except BaseException:
            self.stream.close()
            raise

    @staticmethod
    def _has_exited(descriptor):
        # The existing pidfd pins the original identity even after PID reuse.
        try:
            poller = select.poll()
            poller.register(descriptor, select.POLLIN)
            return any(event & (select.POLLIN | select.POLLHUP)
                       for _, event in poller.poll(0))
        except (OSError, ValueError):
            return False  # Unknown liveness is missing evidence, not an exit.

    @staticmethod
    def _process_values(pid):
        rss = descriptors = command = None
        missing = []
        try:
            lines = [line.split() for line in Path(f'/proc/{pid}/status').read_text().splitlines()
                     if line.startswith('VmRSS:')]
            if len(lines) != 1 or len(lines[0]) != 3 or lines[0][2] != 'kB' \
                    or not lines[0][1].isdecimal():
                raise ValueError('VmRSS unavailable')
            rss = int(lines[0][1])
        except (OSError, ValueError):
            missing.append('rss')
        try:
            with os.scandir(f'/proc/{pid}/fd') as entries:
                descriptors = sum(1 for _ in entries)
        except OSError:
            missing.append('fd')
        try:
            command = Path(f'/proc/{pid}/comm').read_text().rstrip('\n')
            command = command.replace('\t', ' ').replace('\r', ' ').replace('\n', ' ')[:256]
            if not command:
                raise ValueError('comm unavailable')
        except (OSError, ValueError):
            command = None
            missing.append('comm')
        return rss, descriptors, command, missing

    def _row(self, elapsed, pid, start_time, parent, rss, descriptors, command, missing):
        if missing:
            self.missing += 1
        try:
            self.writer.writerow((f'{elapsed:.6f}', pid, start_time, parent,
                                  '' if rss is None else rss,
                                  '' if descriptors is None else descriptors,
                                  '' if command is None else command,
                                  'missing:' + ','.join(missing) if missing else 'ok'))
        except (OSError, ValueError) as error:
            self.write_error = type(error).__name__
            return
        self.rows += 1
        if not missing:
            self.valid_rows += 1
            self.peak_rss_kib = rss if self.peak_rss_kib is None else max(self.peak_rss_kib, rss)
            self.peak_fd_count = (descriptors if self.peak_fd_count is None
                                  else max(self.peak_fd_count, descriptors))

    def sample(self, descendants):
        now = time.monotonic()
        if now < self.next_due or self.write_error is not None:
            return
        self.next_due = now + self.seconds
        self.samples += 1
        elapsed = now - self.started
        for pid, (start_time, descriptor) in descendants.owned.items():
            before = process_identity(pid)
            if before is None:
                if self._has_exited(descriptor):
                    self.skipped += 1
                else:
                    self._row(elapsed, pid, start_time, '', None, None, None, ['identity'])
                if self.write_error is not None:
                    break
                continue
            if before.pid != pid or before.start_time != start_time or before.state in ('Z', 'X'):
                self.skipped += 1
                continue
            rss, descriptors, command, missing = self._process_values(pid)
            after = process_identity(pid)
            if after is None:
                if self._has_exited(descriptor):
                    self.skipped += 1
                    continue
                self._row(elapsed, pid, start_time, '', None, None, None, ['identity'])
            elif after.pid != pid or after.start_time != start_time or after.state in ('Z', 'X'):
                self.skipped += 1
                continue
            else:
                self._row(elapsed, pid, start_time, after.parent, rss, descriptors, command, missing)
            if self.write_error is not None:
                break
        try:
            self.stream.flush()
        except (OSError, ValueError) as error:
            self.write_error = type(error).__name__

    def close(self, monitor_complete=False):
        if self.stream.closed:
            return
        self.complete = (monitor_complete and self.valid_rows > 0
                         and self.missing == 0 and self.write_error is None)
        try:
            self.stream.close()
        except (OSError, ValueError) as error:
            self.write_error = type(error).__name__
            self.complete = False

    def summary(self):
        return ('PROCESS_SAMPLES_OBSERVATION scope=owned_descendants '
                'phase=monitor_loop per_process=true rss_unit=KiB leak_freedom_proven=False '
                f'interval_seconds={self.seconds} samples={self.samples} rows={self.rows} '
                f'valid_rows={self.valid_rows} missing={self.missing} skipped={self.skipped} '
                f'peak_process_rss_kib={self.peak_rss_kib if self.peak_rss_kib is not None else "missing"} '
                f'peak_process_fd_count={self.peak_fd_count if self.peak_fd_count is not None else "missing"} '
                f'write_error={self.write_error or "none"} complete={self.complete}')


def pinned_storage_status(root, label):
    """Stat a requested storage root without following a final symlink.

    Symlinks and non-directories fail closed so that later walks can neither
    follow a link out of the tree nor treat a replaced path as the pinned one.
    """
    try:
        status = os.stat(root, follow_symlinks=False)
    except OSError as error:
        raise RuntimeError(f'requested {label} is inaccessible: {error}') from error
    if stat.S_ISLNK(status.st_mode) or not stat.S_ISDIR(status.st_mode):
        raise RuntimeError(f'requested {label} must be a preexisting real directory')
    return status


def observed_storage_status(root, device, inode, label):
    """Re-verify a pinned root still resolves to the same real directory."""
    try:
        status = os.stat(root, follow_symlinks=False)
    except OSError as error:
        raise RuntimeError(f'observed {label} is inaccessible: {error}') from error
    if (stat.S_ISLNK(status.st_mode) or not stat.S_ISDIR(status.st_mode)
            or (status.st_dev, status.st_ino) != (device, inode)):
        raise RuntimeError(f'observed {label} identity changed')
    return status


def _open_directory_nofollow(path, dir_fd=None):
    """Open one real directory by fd with O_DIRECTORY|O_NOFOLLOW.

    The final component is never resolved through a symlink, so a tree entry
    swapped for a link between listing and open fails closed instead of being
    followed out of the requested tree. The caller owns the returned fd.
    """
    flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC
    if dir_fd is None:
        return os.open(path, flags)
    return os.open(path, flags, dir_fd=dir_fd)


def _open_pinned_directory(path, device, inode, label, dir_fd=None):
    """Open a directory by fd and pin it to an expected device/inode.

    The fd is fstat-checked against the identity observed on the path before it
    is used, so a root replaced after the pre-stat, or by a symlink, is rejected
    rather than silently re-baselined. The caller owns the returned fd.
    """
    try:
        fd = _open_directory_nofollow(path, dir_fd)
    except OSError as error:
        raise RuntimeError(f'observed {label} is inaccessible or a symlink: {error}') from error
    try:
        status = os.fstat(fd)
    except OSError as error:
        os.close(fd)
        raise RuntimeError(f'observed {label} is unreadable: {error}') from error
    if (status.st_dev, status.st_ino) != (device, inode):
        os.close(fd)
        raise RuntimeError(f'observed {label} identity changed')
    return fd


def _tally_entry(dir_fd, name, device, pending):
    """Tally one listed entry relative to an open directory fd, never following.

    Subdirectories are opened with O_NOFOLLOW and fstat-checked against the
    entry stat before being queued, so a directory replaced by a symlink or a
    cross-device directory is refused instead of walked. Returns the logical
    st_size contributed by the entry; queued subdirectories contribute on a
    later pass.
    """
    try:
        status = os.stat(name, dir_fd=dir_fd, follow_symlinks=False)
    except OSError as error:
        if error.errno == errno.ENOENT:
            return 0  # Documented transient-missing limitation; never delete data.
        raise RuntimeError(f'output entry is unreadable: {error}') from error
    if stat.S_ISLNK(status.st_mode):
        raise RuntimeError(f'output tree contains a symlink: {name}')
    if stat.S_ISDIR(status.st_mode):
        if status.st_dev != device:
            raise RuntimeError(f'output directory crossed to another device: {name}')
        try:
            child_fd = _open_directory_nofollow(name, dir_fd)
        except OSError as error:
            if error.errno == errno.ENOENT:
                return 0  # Documented transient-missing limitation; never delete data.
            raise RuntimeError(
                f'output directory changed to a symlink or non-directory: {name}') from error
        try:
            child = os.fstat(child_fd)
        except OSError as error:
            os.close(child_fd)
            raise RuntimeError(f'output directory is unreadable: {name}: {error}') from error
        if (child.st_dev, child.st_ino) != (status.st_dev, status.st_ino):
            os.close(child_fd)
            raise RuntimeError(f'output directory identity changed during scan: {name}')
        pending.append(child_fd)
        return 0
    if stat.S_ISREG(status.st_mode):
        if status.st_dev != device:
            raise RuntimeError(f'output entry crossed to another device: {name}')
        return status.st_size
    raise RuntimeError(f'output tree contains an unsupported entry type: {name}')


def scan_regular_file_bytes(root, device, inode):
    """Sum logical st_size of regular files strictly below a pinned directory.

    Traversal is fd-relative: the root and every listed subdirectory are opened
    with O_DIRECTORY|O_NOFOLLOW and fstat-checked against the identity observed
    on the path, so a path swapped for a symlink between the pre-stat and the
    walk fails closed instead of escaping the tree. Symlinks, other non-regular
    entry types and cross-device entries are refused rather than skipped; only
    an entry that genuinely vanishes while the tree is walked is ignored, which
    is a stated limitation of sampling a live writer. Every queued directory fd
    is closed deterministically, allocated blocks are never claimed and nothing
    is deleted.
    """
    total = 0
    root_fd = _open_pinned_directory(root, device, inode, 'output root')
    pending = [root_fd]
    try:
        while pending:
            fd = pending.pop()
            try:
                with os.scandir(fd) as iterator:
                    names = [entry.name for entry in iterator]
                for name in names:
                    total += _tally_entry(fd, name, device, pending)
            except OSError as error:
                if error.errno != errno.ENOENT:
                    raise RuntimeError(f'output tree is unreadable: {error}') from error
            finally:
                os.close(fd)
        # Re-verify the path after the walk so a root replaced mid-scan is
        # reported instead of being treated as the pinned directory.
        observed_storage_status(root, device, inode, 'output root')
    finally:
        for fd in pending:
            os.close(fd)
    return total


class OutputStorageObservation:
    """Logical byte growth inside one caller-pinned output directory tree.

    Regular-file st_size is tallied; allocated blocks are not claimed. The
    root's device/inode is pinned before the child starts, re-verified on the
    path before and after every sample, and re-pinned on the O_NOFOLLOW fd the
    walk actually traverses; the walk is fd-relative, so it never follows a
    symlink and never leaves the requested tree. A writer can allocate far more
    between two samples than this reports, so the observation is a sampled
    detector, not an allocation guarantee.
    """

    def __init__(self, root, budget_mib, reserve_mib=0):
        if not math.isfinite(budget_mib) or budget_mib <= 0:
            raise RuntimeError('output budget must be a positive finite MiB count')
        if not math.isfinite(reserve_mib) or reserve_mib < 0:
            raise RuntimeError('next-write reservation must be finite and nonnegative')
        root = Path(root)
        status = pinned_storage_status(root, 'output root')
        self.root, self.device, self.inode = root, status.st_dev, status.st_ino
        self.budget_bytes = int(budget_mib * 1024 * 1024)
        self.reserve_bytes = int(reserve_mib * 1024 * 1024)
        self.current_bytes = 0
        self.peak_bytes = 0
        self.samples = 0
        self.complete = False
        self.sample()

    def sample(self):
        observed_storage_status(self.root, self.device, self.inode, 'output root')
        self.current_bytes = scan_regular_file_bytes(self.root, self.device, self.inode)
        self.peak_bytes = max(self.peak_bytes, self.current_bytes)
        self.samples += 1
        return self.current_bytes

    def over_budget(self):
        """True when sampled logical bytes plus the reservation exceed budget."""
        return self.current_bytes + self.reserve_bytes > self.budget_bytes

    def summary(self):
        return ('OUTPUT_STORAGE_OBSERVATION scope=directory '
                f'root={self.root} device={self.device} inode={self.inode} '
                f'budget_bytes={self.budget_bytes} reserve_bytes={self.reserve_bytes} '
                f'current_bytes={self.current_bytes} peak_bytes={self.peak_bytes} '
                f'samples={self.samples} complete={self.complete}')


class HostStorageObservation:
    """Lowest actual free space seen on the Linux mount backing a path.

    The argument is a real mounted path visible to this Linux process (for
    example /mnt/e), measured with shutil.disk_usage. No Windows drive-letter
    adaptation or credential is used, and the WSL virtual filesystem must not
    be substituted for the host mount. The path's device/inode is pinned and
    re-verified on both sides of every read, and free space is read from the
    O_NOFOLLOW fd so a root replaced by a symlink or another directory fails
    closed. Sampling cannot bound a writer between two reads.
    """

    def __init__(self, root, minimum_mib, reserve_mib=0):
        if not math.isfinite(minimum_mib) or minimum_mib <= 0:
            raise RuntimeError('minimum host free space must be a positive finite MiB count')
        if not math.isfinite(reserve_mib) or reserve_mib < 0:
            raise RuntimeError('next-write reservation must be finite and nonnegative')
        root = Path(root)
        status = pinned_storage_status(root, 'host storage root')
        self.root, self.device, self.inode = root, status.st_dev, status.st_ino
        self.minimum_threshold_bytes = int(minimum_mib * 1024 * 1024)
        self.reserve_bytes = int(reserve_mib * 1024 * 1024)
        self.initial_free = None
        self.minimum_free = None
        self.samples = 0
        self.complete = False
        self.sample()

    def sample(self):
        observed_storage_status(self.root, self.device, self.inode, 'host storage root')
        # Free space is read from the pinned O_NOFOLLOW fd, not re-resolved from
        # the path, and the path identity is re-verified on both sides of the
        # read so a root replaced by a symlink or another directory is refused.
        fd = _open_pinned_directory(self.root, self.device, self.inode, 'host storage root')
        try:
            free = shutil.disk_usage(fd).free
        except OSError as error:
            raise RuntimeError(f'host storage free space is unavailable: {error}') from error
        finally:
            os.close(fd)
        observed_storage_status(self.root, self.device, self.inode, 'host storage root')
        if not math.isfinite(free) or free < 0:
            raise RuntimeError('host storage free space is not a finite nonnegative byte count')
        if self.initial_free is None:
            self.initial_free = free
        self.minimum_free = free if self.minimum_free is None else min(self.minimum_free, free)
        self.samples += 1
        return free

    def under_minimum(self):
        """True when sampled free bytes plus the reservation fall below minimum."""
        return self.minimum_free < self.minimum_threshold_bytes + self.reserve_bytes

    def summary(self):
        return ('HOST_STORAGE_OBSERVATION scope=linux_mount '
                f'root={self.root} minimum_threshold_bytes={self.minimum_threshold_bytes} '
                f'reserve_bytes={self.reserve_bytes} initial_free_bytes={self.initial_free} '
                f'minimum_free_bytes={self.minimum_free} samples={self.samples} '
                f'complete={self.complete}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--min-available-mib', type=int, required=True)
    parser.add_argument('--max-swap-growth-mib', type=int, required=True)
    parser.add_argument('--poll-seconds', type=float, default=1.0)
    parser.add_argument('--log', type=Path,
                        help='save child output and the memory summary in a new log file')
    parser.add_argument('--process-samples', type=Path,
                        help='optional new TSV of owned process RSS/fd samples; never overwrite')
    parser.add_argument('--process-sample-seconds', type=float, default=5.,
                        help='positive finite process sampling interval; observed at guard polls')
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
    parser.add_argument('--output-root', type=Path,
                        help='optional preexisting output directory whose logical size is watched')
    parser.add_argument('--max-output-mib', type=int,
                        help='logical output budget in MiB; required with --output-root')
    parser.add_argument('--host-storage-root', type=Path,
                        help='optional Linux-visible host mount whose actual free space is watched')
    parser.add_argument('--min-host-free-mib', type=int,
                        help='minimum actual host free space in MiB; required with --host-storage-root')
    parser.add_argument('--next-write-reserve-mib', type=int, default=None,
                        help='caller-provided conservative next-write reservation in MiB; only '
                             'valid together with an output or host storage guard. It is a '
                             'configuration choice, not a measured allocation guarantee')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command or args.min_available_mib <= 0 or args.max_swap_growth_mib < 0 \
            or not math.isfinite(args.poll_seconds) or args.poll_seconds <= 0:
        parser.error('a command, positive headroom/poll interval and nonnegative swap allowance are required')
    if not math.isfinite(args.process_sample_seconds) or args.process_sample_seconds <= 0:
        parser.error('--process-sample-seconds must be finite and positive')
    if (not all(math.isfinite(value) and 0 < value <= 100 for value in
                (args.max_memory_stall_percent, args.max_io_stall_percent))
            or not math.isfinite(args.pressure_seconds) or args.pressure_seconds <= 0):
        parser.error('stall limits must be in (0,100] and pressure duration must be positive')
    if (args.output_root is None) != (args.max_output_mib is None):
        parser.error('--output-root and --max-output-mib must be given together')
    if (args.host_storage_root is None) != (args.min_host_free_mib is None):
        parser.error('--host-storage-root and --min-host-free-mib must be given together')
    if args.max_output_mib is not None and args.max_output_mib <= 0:
        parser.error('--max-output-mib must be a positive integer')
    if args.min_host_free_mib is not None and args.min_host_free_mib <= 0:
        parser.error('--min-host-free-mib must be a positive integer')
    if args.next_write_reserve_mib is not None:
        if args.next_write_reserve_mib < 0:
            parser.error('--next-write-reserve-mib must be a nonnegative integer')
        if args.output_root is None and args.host_storage_root is None:
            parser.error('--next-write-reserve-mib requires --output-root or --host-storage-root')
    reserve_mib = 0 if args.next_write_reserve_mib is None else args.next_write_reserve_mib
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
    try:
        # Preflight before launching the child: requested but inaccessible,
        # symlinked, replaced-identity or nonfinite-budget storage fails closed.
        output_storage = (OutputStorageObservation(args.output_root, args.max_output_mib, reserve_mib)
                          if args.output_root is not None else None)
        host_storage = (HostStorageObservation(args.host_storage_root, args.min_host_free_mib, reserve_mib)
                        if args.host_storage_root is not None else None)
    except (OSError, RuntimeError, ValueError) as error:
        parser.error(f'cannot establish requested output/host storage observation: {error}')
    # A requested budget that is already breached must refuse to launch: the
    # child is never started, so a pre-existing over-budget tree or an
    # already-too-small host mount cannot be smuggled past the first poll.
    if output_storage is not None and output_storage.over_budget():
        parser.error('sampled output already exceeds the configured budget before launch')
    if host_storage is not None and host_storage.under_minimum():
        parser.error('actual host free space is already below the configured minimum before launch')
    minimum, peak_swap = available, baseline_swap
    peak_owned_rss = 0
    started = time.monotonic()
    try:
        descendants = OwnedDescendants()
    except (OSError, RuntimeError) as error:
        parser.error(f'cannot establish safe child ownership: {error}')
    log = args.log.open('x', encoding='utf-8') if args.log else None
    try:
        process_samples = (ProcessSamplesObservation(args.process_samples,
                           args.process_sample_seconds, started)
                           if args.process_samples is not None else None)
    except (OSError, ValueError) as error:
        if log:
            log.close()
        parser.error(f'cannot establish requested process sampling: {error}')
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
            if process_samples:
                process_samples.sample(descendants)
            available, swap = memory_kib()
            minimum, peak_swap = min(minimum, available), max(peak_swap, swap)
            pressure_reason = pressure.sample() if pressure else None
            if output_storage:
                output_storage.sample()
            if host_storage:
                host_storage.sample()
            if available < args.min_available_mib * 1024:
                stop_reason = 'available_memory'
            elif swap - baseline_swap > args.max_swap_growth_mib * 1024:
                stop_reason = 'swap_growth'
            elif pressure_reason:
                stop_reason = pressure_reason
            elif output_storage and output_storage.over_budget():
                stop_reason = 'output_bytes'
            elif host_storage and host_storage.under_minimum():
                stop_reason = 'host_free'
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
            # The final sample is a real verdict, not just bookkeeping: a child
            # that breaches a requested budget and exits before the next poll
            # must still stop the run instead of reporting success. Cleanup and
            # the summary path are unchanged, so owned descendants are reaped
            # exactly as they are on a monitor-loop stop.
            if output_storage:
                output_storage.sample()
            if host_storage:
                host_storage.sample()
            if output_storage is not None and output_storage.over_budget():
                breached, stop_reason = True, 'output_bytes'
            elif host_storage is not None and host_storage.under_minimum():
                breached, stop_reason = True, 'host_free'
            if breached:
                print(f'MEMORY_GUARD_STOP reason={stop_reason}: '
                      'breach found on the final sample', flush=True)
            else:
                if output_storage:
                    output_storage.complete = True
                if host_storage:
                    host_storage.complete = True
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
        cleanup_complete = False
        try:
            # Popen can be interrupted after fork but before assignment. The
            # subreaper still owns that child even without a Popen handle.
            descendants.cleanup(process)
            cleanup_complete = True
        finally:
            for signum, handler in handlers.items():
                signal.signal(signum, handler)
            if process_samples:
                process_samples.close(monitor_complete=(stop_reason == 'none' and cleanup_complete))
            summary = (f'MEMORY_GUARD_RESULT min_available_kib={minimum} '
                       f'baseline_swap_kib={baseline_swap} peak_swap_kib={peak_swap} '
                       f'guard_stopped={breached} peak_owned_rss_kib={peak_owned_rss} '
                       f'elapsed_seconds={time.monotonic() - started:.3f} stop_reason={stop_reason}')
            print(summary, flush=True)
            if process_samples:
                print(process_samples.summary(), flush=True)
            if gpu:
                print(gpu.summary(), flush=True)
            if pressure:
                print(pressure.summary(), flush=True)
            if output_storage:
                print(output_storage.summary(), flush=True)
            if host_storage:
                print(host_storage.summary(), flush=True)
            if log:
                log.write(summary + '\n')
                if process_samples:
                    log.write(process_samples.summary() + '\n')
                if gpu:
                    log.write(gpu.summary() + '\n')
                if pressure:
                    log.write(pressure.summary() + '\n')
                if output_storage:
                    log.write(output_storage.summary() + '\n')
                if host_storage:
                    log.write(host_storage.summary() + '\n')
                log.close()
    return 125 if breached else code


if __name__ == '__main__':
    raise SystemExit(main())
