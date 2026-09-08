"""Collect allocator peaks without adding another production memory counter.

Run under tools/run_memory_guarded.py with GPU observation. Nsight instruments
the normal tests/validators; their mathematical and checkpoint gates remain in
force. Raw profiler databases stay in the ignored build tree because they may
contain unrelated environment metadata. Only selected-process summaries are
written into the validation archive. Instrumented times are not speed benchmarks.
"""
import argparse
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import shutil
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools'))
import validation_provenance as provenance
from run_memory_guarded import OwnedDescendants, process_identity
from validation_device_memory import read_profile
from validate_backend_results import require_empty_output_root, run_arch_with_logs


WORKLOAD_ARTIFACTS = {
    'regrid_transaction': 'regrid',
    'sparse_capacity': 'provider',
    'audit31_trajectory': 'sparse',
    'amr_application': 'arch',
}


class ProcessImageObserver:
    """Observe only this invocation's pinned descendants; never use comm as identity.

    The shared memory guard owns PID/start-time discovery and cleanup. The
    existing runner still launches and checks the workload. Only executable
    links and stat identities are sampled, never arguments or environment.
    """
    def __init__(self, poll_interval_seconds):
        if not math.isfinite(poll_interval_seconds) or poll_interval_seconds <= 0:
            raise ValueError('process identity sampling interval must be positive')
        self.interval = poll_interval_seconds
        self.descendants = OwnedDescendants()
        self.stop = threading.Event()
        self.images, self.error, self.finished = {}, None, False
        self.thread = threading.Thread(target=self._observe, name='capacity-process-identity')

    def _sample(self):
        self.descendants.refresh()
        for pid, (start_time, _) in list(self.descendants.owned.items()):
            before = process_identity(pid)
            if before is None or before.start_time != start_time:
                continue
            link = Path(f'/proc/{pid}/exe')
            try:
                executable = os.readlink(link)
                image = provenance._artifact_observations({'image': link})['image']
                after_path = os.readlink(link)
            except (FileNotFoundError, ProcessLookupError):
                continue  # A missed or departed CUDA PID cannot pass the later join.
            after = process_identity(pid)
            if after is None or after.start_time != start_time or executable != after_path:
                continue
            key = (pid, start_time, executable, *image)
            now = time.monotonic_ns()
            row = self.images.setdefault(key, dict(pid=pid, start_time_ticks=start_time,
                executable_path=executable, artifact_observation=image,
                first_observed_monotonic_ns=now, last_observed_monotonic_ns=now, samples=0))
            row['samples'] += 1
            row['last_observed_monotonic_ns'] = now

    def _observe(self):
        try:
            while True:
                self._sample()
                if self.stop.wait(self.interval):
                    break
        except Exception as error:
            self.error = error

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.stop.set()
        self.thread.join()
        # Reuse the guard's bounded cleanup, including orphaned descendants on
        # timeout; the original subprocess runner has already reaped its child.
        self.descendants.cleanup(None)
        if exc_type is None and self.error is not None:
            raise RuntimeError('CUDA process identity observer failed') from self.error
        self.finished = True

    def capture(self):
        if not self.finished or self.thread.is_alive() or self.error is not None:
            raise ValueError('CUDA process identity observation is incomplete')
        return dict(schema=1, complete=True, poll_interval_seconds=self.interval,
                    observations=list(self.images.values()))


def check_process_identities(summary, capture, artifacts, artifact_observations):
    """Join actual CUDA PIDs to observed executable objects and frozen hashes.

    The return value is also accepted as capture for deterministic index replay.
    Non-CUDA descendants are discarded from the public evidence, not guessed
    into the allowed set. Missing observations and multiple images fail closed.
    """
    interval = capture.get('poll_interval_seconds')
    if capture.get('schema') != 1 or capture.get('complete') is not True \
            or type(interval) not in (int, float) or not math.isfinite(interval) or interval <= 0 \
            or not isinstance(capture.get('observations'), list):
        raise ValueError('missing or incomplete CUDA process identity capture')
    if not artifacts or artifacts.keys() != artifact_observations.keys():
        raise ValueError('missing frozen workload artifacts')
    for artifact in artifacts.values():
        digest = artifact.get('sha256')
        if not isinstance(digest, str) or len(digest) != 64 \
                or any(value not in '0123456789abcdef' for value in digest):
            raise ValueError('invalid frozen executable SHA-256')
    rows = summary.get('processes')
    if not isinstance(rows, list) or not rows:
        raise ValueError('missing actual CUDA process coverage')
    selected, verified, seen, pid_owners = [], [], {}, {}
    for row in rows:
        pid, global_pid = row.get('pid'), row.get('global_pid')
        # The shared SQLite reader already joins PROCESSES on exact globalPid.
        # Preserve that opaque identity instead of decoding profiler-version bits.
        if type(pid) is not int or pid <= 0 or type(global_pid) is not int or global_pid <= 0:
            raise ValueError('invalid selected CUDA process identifier')
        if global_pid in seen:
            if seen[global_pid] != (pid, row.get('executable')):
                raise ValueError('conflicting CUDA process names or identities')
            continue  # The same process may own allocations on multiple devices.
        if pid in pid_owners:
            raise ValueError('multiple CUDA global identities map to one observed Linux PID')
        pid_owners[pid] = global_pid
        seen[global_pid] = (pid, row.get('executable'))
        observations = [item for item in capture['observations'] if item.get('pid') == pid]
        if len(observations) != 1:
            raise ValueError(f'CUDA PID {pid} lacks a unique observed executable image')
        observation = observations[0]
        for key in ('start_time_ticks', 'samples', 'first_observed_monotonic_ns',
                    'last_observed_monotonic_ns'):
            if type(observation.get(key)) is not int or observation[key] <= 0:
                raise ValueError('invalid CUDA process observation counters')
        if observation['last_observed_monotonic_ns'] < observation['first_observed_monotonic_ns']:
            raise ValueError('unordered CUDA process observation times')
        image = observation.get('artifact_observation')
        if not isinstance(image, list) or len(image) != 5 \
                or any(type(value) is not int or value < 0 for value in image):
            raise ValueError('invalid CUDA executable object identity')
        matches = [name for name, artifact in artifacts.items()
                   if artifact['path'] == observation.get('executable_path')
                   and artifact_observations[name] == image]
        if len(matches) != 1:
            raise ValueError(f'CUDA PID {pid} did not execute a unique frozen workload artifact')
        name = matches[0]
        selected.append(observation)
        verified.append(dict(global_pid=global_pid, pid=pid,
            nsight_process_name=row.get('executable'), artifact_key=name,
            executable=artifacts[name], start_time_ticks=observation['start_time_ticks']))
    return dict(schema=1, complete=True, poll_interval_seconds=interval,
        observations=sorted(selected, key=lambda item: item['pid']),
        processes=sorted(verified, key=lambda item: item['global_pid']),
        scope='selected CUDA PID to observed /proc/exe object and frozen artifact SHA-256',
        note='Nsight process names are retained verbatim and are not used as executable paths. Frozen executable hashes are bound through inode/stat observations and checked again after the campaign.')


def check_allocation_lifetimes(summary):
    """Classify the shared reader's totals without inventing release events.

    Explicit allocations must close. CUDA device/managed static symbols have
    context/program lifetimes; retain their observed residuals separately.
    This is an acceptance policy, not another event parser or residency meter.
    See attempt-908/README.md for the CUDA definitions and motivating evidence.
    """
    dynamic_kinds = {'pageable', 'pinned', 'device', 'array', 'managed'}
    static_kinds = {'device_static', 'managed_static'}
    if not isinstance(summary, dict):
        raise ValueError('missing CUDA allocation summary')
    rows = summary.get('processes')
    unmatched = summary.get('unmatched_live_allocations')
    released = summary.get('all_allocations_released')
    if not isinstance(rows, list) or not rows or type(unmatched) is not int \
            or unmatched < 0 or type(released) is not bool \
            or released != (unmatched == 0):
        raise ValueError('missing or inconsistent CUDA allocation summary')
    processes, device_coverage, nonzero_kinds = [], False, 0
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError('missing CUDA allocation process summary')
        live, peaks = row.get('live_bytes_by_kind'), row.get('peak_bytes_by_kind')
        if not isinstance(live, dict) or not live or not isinstance(peaks, dict) \
                or live.keys() != peaks.keys() \
                or not live.keys() <= dynamic_kinds | static_kinds:
            raise ValueError('missing or unknown CUDA allocation memory kind')
        for kind, size in live.items():
            if type(size) is not int or size < 0 or type(peaks[kind]) is not int \
                    or peaks[kind] < size:
                raise ValueError('invalid CUDA allocation byte totals')
            if kind in dynamic_kinds and size != 0:
                raise ValueError(f'unreleased explicit CUDA allocation: {kind}')
            nonzero_kinds += size > 0
        device_coverage |= any(peaks.get(kind, 0) > 0 for kind in ('device', 'array'))
        processes.append(dict(global_pid=row['global_pid'], device=row['device'],
            dynamic_live_bytes_by_kind={kind: live[kind] for kind in sorted(live)
                                        if kind in dynamic_kinds},
            static_live_bytes_by_kind={kind: live[kind] for kind in sorted(live)
                                       if kind in static_kinds}))
    if not device_coverage:
        raise ValueError('CUDA trace lacks explicit device or array allocation coverage')
    if (nonzero_kinds == 0) != released or unmatched < nonzero_kinds:
        raise ValueError('CUDA live totals disagree with unmatched allocation count')
    return dict(schema=1, all_dynamic_allocations_released=True,
        dynamic_device_allocation_coverage=True, all_allocations_released=released,
        unmatched_live_allocations=unmatched, processes=processes,
        note='Static residuals are retained as observed; their release at context/program teardown is not inferred. This classification does not measure physical VRAM.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--scientific-python', type=Path, required=True)
    parser.add_argument('--profiler', type=Path, default=Path(shutil.which('nsys') or 'nsys'))
    parser.add_argument('--capacity-rows', type=int, default=16384)
    parser.add_argument('--identity-poll-seconds', type=float, default=0.05)
    args = parser.parse_args()
    if args.capacity_rows < 32:
        parser.error('capacity rows must exceed the compact matrix route')
    if not math.isfinite(args.identity_poll_seconds) or args.identity_poll_seconds <= 0:
        parser.error('identity sampling interval must be positive')
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    require_empty_output_root(output)
    output.mkdir(parents=True, exist_ok=True)
    raw = Path(tempfile.mkdtemp(prefix='device-memory-', dir=build))
    artifacts = dict(arch=build / 'bin/ARCH', validator=build / 'arch_cuda_single_level_validation',
        regrid=build / 'arch_cuda_regrid_transaction', provider=build / 'arch_cuda_cudss_sparse_solver',
        sparse=build / 'arch_cuda_generated_sparse_burn_audit31')
    def identity():
        return provenance.capture_focused(source_root=ROOT, build_dir=build, artifacts=artifacts)
    before, recipe = identity(), provenance.file_identity(Path(__file__).resolve())
    profiler = provenance.file_identity(args.profiler.resolve())
    interpreters = {name: provenance.file_identity(path.resolve()) for name, path in
                    [('runner', Path(sys.executable)), ('scientific', args.scientific_python)]}
    started, records = datetime.now(timezone.utc).isoformat(), []
    matrix_output, sparse_output = output / 'amr-validation', output / 'sparse-validation'
    tasks = [
        ('regrid_transaction', [str(artifacts['regrid'])], 'CUDA_REGRID_TRANSACTION_PASS', None),
        ('sparse_capacity', [str(artifacts['provider']), '--capacity-rows', str(args.capacity_rows)],
         f'cuDSS N={args.capacity_rows}: solve/reuse/refactor and negative contracts passed', None),
        ('audit31_trajectory', [str(args.scientific_python.absolute()), '-B',
         str(ROOT / 'validation/network/run_sparse_validation.py'), '--build-dir', str(build),
         '--output-dir', str(sparse_output), '--network-id', 'audit31', '--rho', '1e7',
         '--temperature', '3e9', '--interval', '1e-10', '--cv', '1e8', '--rtol', '1e-7',
         '--steps', '64', '--timeout', '1800', '--composition', 'c12=0.5', 'o16=0.5'],
         None, sparse_output / 'evidence.json'),
        ('amr_application', [sys.executable, '-B', str(ROOT / 'tools/validate_backend_results.py'),
         '--manifest', str(ROOT / 'validation/amr/gpu_cases.json'), '--arch', str(artifacts['arch']),
         '--checkpoint-validator', str(artifacts['validator']), '--source-root', str(ROOT),
         '--build-dir', str(build), '--output-root', str(matrix_output),
         '--case', 'hydro_amr_rk3_3d', '--case', 'diffusion_amr_rkl2_5stage'],
         None, matrix_output / 'backend-validation-evidence.json'),
    ]
    for name, command, marker, evidence in tasks:
        lane, prefix = output / name, raw / name
        lane.mkdir()
        invocation = [str(args.profiler.resolve()), 'profile', '--sample=none', '--cpuctxsw=none',
            '--trace=cuda', '--cuda-memory-usage=true', '--export=sqlite', '--force-overwrite=false',
            '--output=' + str(prefix), *command]
        with ProcessImageObserver(args.identity_poll_seconds) as observer:
            process = run_arch_with_logs(invocation, source_root=ROOT, lane_root=lane, timeout=3600)
        if process.returncode or (marker and marker not in process.stdout):
            raise RuntimeError(f'{name} failed; retained logs at {lane}')
        if evidence is not None and not evidence.is_file():
            raise RuntimeError(f'{name} lacks its successful ordinary validation evidence')
        summary = read_profile(prefix.with_suffix('.sqlite'))
        try:
            lifetimes = check_allocation_lifetimes(summary)
        except ValueError as error:
            raise RuntimeError(f'{name} has invalid CUDA allocation coverage: {error}') from error
        artifact_key = WORKLOAD_ARTIFACTS[name]
        process_identity_evidence = check_process_identities(summary, observer.capture(),
            {artifact_key: before['artifacts'][artifact_key]},
            {artifact_key: before['artifact_observation'][artifact_key]})
        records.append(dict(name=name, command=invocation, allocation_summary=summary,
            allocation_lifetimes=lifetimes,
            process_identity_evidence=process_identity_evidence,
            raw_local_profile=provenance.file_identity(prefix.with_suffix('.sqlite')),
            ordinary_evidence=None if evidence is None else provenance.file_identity(evidence),
            stdout=provenance.file_identity(lane / 'arch.stdout'),
            stderr=provenance.file_identity(lane / 'arch.stderr')))
        print(f'CUDA allocation profile PASS: {name}', flush=True)
    provenance.require_unchanged(before, identity())
    if recipe != provenance.file_identity(Path(__file__).resolve()) \
            or profiler != provenance.file_identity(args.profiler.resolve()) \
            or interpreters != {name: provenance.file_identity(path.resolve()) for name, path in
                [('runner', Path(sys.executable)), ('scientific', args.scientific_python)]}:
        raise RuntimeError('profiling recipe or tools changed during collection')
    result = dict(schema=1, scope='bounded CUDA allocator capacity and storage overlap',
        focused_gate_pass=True, release_qualified=False, identity=before,
        identity_verified_after_run=True, recipe=recipe, profiler=profiler,
        interpreters=interpreters, records=records, capacity_rows=args.capacity_rows,
        started_utc=started, finished_utc=datetime.now(timezone.utc).isoformat(),
        note='Allocation-request peaks complement whole-device telemetry. Instrumented times are not runtime benchmarks; raw profiler metadata remains local.')
    (output / 'evidence.json').write_text(json.dumps(result, indent=2) + '\n')
    print(f'CUDA allocator capacity PASS: {output / "evidence.json"}')


if __name__ == '__main__':
    main()
