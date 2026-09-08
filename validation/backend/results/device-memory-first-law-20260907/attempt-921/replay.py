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
from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools'))
import validation_provenance as provenance
from validation_device_memory import read_profile
from validate_backend_results import require_empty_output_root, run_arch_with_logs


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
    args = parser.parse_args()
    if args.capacity_rows < 32:
        parser.error('capacity rows must exceed the compact matrix route')
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
        expected_names = {Path(argument).name for argument in artifacts.values()}
        if any(Path(row['executable']).name not in expected_names for row in summary['processes']):
            raise RuntimeError(f'{name} includes an unexpected CUDA allocation process')
        records.append(dict(name=name, command=invocation, allocation_summary=summary,
            allocation_lifetimes=lifetimes,
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
