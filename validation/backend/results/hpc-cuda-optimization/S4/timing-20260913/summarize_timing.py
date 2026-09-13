"""Summarize the unchanged S4 timer and use its comparator across thread counts.

No new scientific tolerances or performance pass threshold. Historical speed
ratios are explicitly separated from contemporaneous CPU/CUDA measurements.
"""
import argparse
from datetime import datetime, timezone
import importlib.util
import json
from pathlib import Path
import statistics
from types import SimpleNamespace


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    parser.add_argument('--threads', nargs='+', type=int, default=[1, 2, 4, 8, 16])
    args = parser.parse_args()
    source, output = args.source_root.resolve(), args.output_root.resolve()
    if not output.is_relative_to(source / 'build') or 8 not in args.threads:
        parser.error('output must be under build and retain the 8-thread reference')
    recipe = source / 'validation/backend/results/maintenance-freeze-20260908/run_sedov_amr_timing.py'
    spec = importlib.util.spec_from_file_location('original_sedov_timer', recipe)
    original = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(original)
    reports = {}
    for threads in args.threads:
        path = output / f'observed-timing-threads{threads}/phase/measurements/timing-report.json'
        report = json.loads(path.read_text())
        if report['status'] != 'passed' or not report['identity_verified_after_run']:
            raise RuntimeError(f'unqualified timing report: {path}')
        if report['settings']['threads'] != threads:
            raise RuntimeError('thread identity mismatch')
        reports[threads] = report
    baseline = reports[8]
    comparator_args = SimpleNamespace(
        checkpoint_validator=Path(baseline['settings']['checkpoint_validator']),
        physical_time=baseline['settings']['physical_time'],
        field_policy=baseline['field_policy'])
    def representative(report, blocks, backend):
        return next(lane for lane in report['lanes']
                    if lane['blocks_per_axis'] == blocks and lane['backend'] == backend
                    and lane['phase'] == 'measured')
    result = dict(status='running', captured_utc=datetime.now(timezone.utc).isoformat(),
                  scope='end-to-end paired timings; one warmup and five measured runs per backend/scale/thread',
                  performance_scope='observed VM idle window; vGPU physical-host isolation is not observable',
                  release_qualified=False, thread_reports={}, rows=[], cross_thread_comparisons=[], historical={})
    for threads, report in reports.items():
        for section in ('artifacts', 'source', 'build'):
            if report['identity_before'][section] != baseline['identity_before'][section]:
                raise RuntimeError(f'cross-thread {section} identity changed')
        def unchanged_environment(identity):
            return {k:v for k,v in identity['execution_environment'].items() if k != 'OMP_NUM_THREADS'}
        if unchanged_environment(report['identity_before']) != unchanged_environment(baseline['identity_before']):
            raise RuntimeError('unplanned execution environment change')
        if report['recipe_and_helpers_before'] != baseline['recipe_and_helpers_before']:
            raise RuntimeError('timing recipe or helpers changed between thread counts')
        if (report['field_policy'] != baseline['field_policy'] or
                report['conservation_policy'] != baseline['conservation_policy'] or
                report['settings']['arch'] != baseline['settings']['arch']):
            raise RuntimeError('baseline, binary path or scientific budget changed')
        result['thread_reports'][threads] = dict(status=report['status'],
            comparisons=len(report['comparisons']), runs=len(report['lanes']),
            max_abs=max(item['field_comparison']['max_abs'] for item in report['comparisons']),
            max_field_normalized=max(item['field_comparison']['max_field_normalized'] for item in report['comparisons']),
            all_workload_aligned=all(item['workload_aligned'] for item in report['comparisons']))
        for blocks in (4, 8):
            row = dict(threads=threads, blocks_per_axis=blocks, **report['statistics'][str(blocks)])
            for backend in ('cpu', 'cuda'):
                samples = row[backend]['seconds']
                if len(samples) != 5:
                    raise RuntimeError('formal report does not contain five measurements')
                row[backend]['sample_stdev_seconds'] = statistics.stdev(samples)
                row[backend]['sample_cv'] = statistics.stdev(samples) / statistics.mean(samples)
                row[backend]['range_over_median'] = (max(samples)-min(samples)) / statistics.median(samples)
                if threads != 8:
                    check = original.compare_lanes(comparator_args,
                        representative(baseline, blocks, backend),
                        representative(report, blocks, backend), 'cross-thread-vs-eight')
                    check.update(reference_threads=8, candidate_threads=threads, backend=backend, blocks_per_axis=blocks)
                    result['cross_thread_comparisons'].append(check)
                    if not check['workload_aligned'] or not check['field_comparison']['passed']:
                        raise RuntimeError('cross-thread workload or scientific mismatch')
            result['rows'].append(row)
    historical_path = source / 'validation/backend/results/h100-performance-20260909/timing-report.json'
    historical = json.loads(historical_path.read_text())
    for blocks in (4, 8):
        old, new = historical['statistics'][str(blocks)], baseline['statistics'][str(blocks)]
        result['historical'][blocks] = dict(
            label='historical old run vs new run; not a contemporaneous A/B experiment',
            old_cpu_median_seconds=old['cpu']['median'], old_cuda_median_seconds=old['cuda']['median'],
            old_cuda_over_new_cuda=old['cuda']['median']/new['cuda']['median'],
            old_cpu_over_new_cpu=old['cpu']['median']/new['cpu']['median'])
    result['best_observed'] = {}
    for blocks in (4, 8):
        selected = [r for r in result['rows'] if r['blocks_per_axis'] == blocks]
        cpu = min(selected, key=lambda r:r['cpu']['median'])
        gpu = min(selected, key=lambda r:r['cuda']['median'])
        result['best_observed'][blocks] = dict(
            cpu_threads=cpu['threads'], cpu_seconds=cpu['cpu']['median'],
            cuda_threads=gpu['threads'], cuda_seconds=gpu['cuda']['median'],
            best_cpu_over_best_cuda=cpu['cpu']['median']/gpu['cuda']['median'],
            best_cpu_over_cuda_at_same_threads=cpu['cpu_over_cuda_median_ratio'],
            selection_scope='best median among the recorded thread scan, not a population optimum')
    result['status'] = 'passed'
    destination = output / 'summary.json'
    if destination.exists():
        raise RuntimeError('refuse to overwrite previous summary')
    destination.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k:result[k] for k in ('status','rows','best_observed','historical')},indent=2))


if __name__ == '__main__':
    main()
