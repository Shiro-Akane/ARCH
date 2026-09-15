"""Derive Host-visible regrid intervals from already qualified JSON records.

No application, profiler, CUDA work, TSV parsing or timing protocol changes.
Intervals include waits inside perform_regrid; they are not exclusive GPU time.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics

MODULES = ('coupled_be_nr_rkl1_all_transport', 'coupled_be_nr_rkl2_all_transport',
           'coupled_bd_rkl1_all_transport')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(ok, message):
    if not ok:
        raise ValueError(message)


def summarize(samples):
    require(len(samples) == 5 and {v['repeat'] for v in samples} == set(range(5)),
            'five distinct measured repeats required')
    values = []
    for lane in sorted(samples, key=lambda v: v['repeat']):
        duration = lane['arch_wall_seconds']
        records = lane['regrid']['records']
        require(math.isfinite(duration) and duration > 0, 'invalid application duration')
        require(all(isinstance(r['macro_step'], int) and 0 <= r['macro_step'] <= lane['metadata']['step']
                    and math.isfinite(r['wall_seconds']) and r['wall_seconds'] >= 0 for r in records),
                'invalid regrid records')
        total = math.fsum(r['wall_seconds'] for r in records)
        summary = lane['regrid']['summary']
        require(total == summary['wall_seconds'] and len(records) == summary['records'],
                'existing regrid summary does not match its records')
        require(summary['overlaps_backend_trace'] is True, 'unexpected trace accounting contract')
        require(total <= duration, 'regrid intervals exceed application duration')
        initial = math.fsum(r['wall_seconds'] for r in records if r['macro_step'] == 0)
        runtime = math.fsum(r['wall_seconds'] for r in records if r['macro_step'] > 0)
        require(math.isclose(total, initial + runtime, rel_tol=1e-14, abs_tol=1e-14),
                'initial/runtime partition mismatch')
        values.append(dict(repeat=lane['repeat'], arch_wall_seconds=duration,
            interval_seconds=total, initial_step_zero_seconds=initial,
            positive_step_interval_seconds=runtime, interval_fraction_percent=100 * total / duration,
            interval_count=len(records), directory=lane['directory']))
    metrics = {}
    for key in ('arch_wall_seconds', 'interval_seconds', 'initial_step_zero_seconds',
                'positive_step_interval_seconds', 'interval_fraction_percent'):
        numbers = [row[key] for row in values]
        metrics[key] = dict(median=statistics.median(numbers), minimum=min(numbers),
            maximum=max(numbers), population_stdev=statistics.pstdev(numbers))
    return dict(samples=values, metrics=metrics)


def analyze(root):
    result = dict(status='derived_interval_analysis_not_new_runtime_qualification',
        release_qualified=False, modules=list(MODULES), warmups_excluded=True,
        scope='Host wall intervals around perform_regrid, including its device waits; not exclusive GPU time',
        steady_state_burn_diffusion_timing_available=False,
        ratio_policy='median of five per-sample interval/application percentages, not ratio of medians',
        inputs={}, groups=[])
    for module in MODULES:
        path = root / module / 'evidence/evidence.json'
        report = json.loads(path.read_text())
        require(report['status'] == 'passed' and not report['pilot']
                and len(report['lanes']) == 108 and len(report['comparisons']) == 105,
                'complete qualified module required')
        require(all(l['status'] == 'passed' for l in report['lanes'])
                and all(c['workload_aligned'] and c['fields']['passed']
                        and c['fields']['status'] == 'pass' for c in report['comparisons']),
                'numerical/workload qualification failed')
        for version in ('baseline', 'candidate'):
            for section in ('artifacts', 'artifact_observation', 'source', 'build', 'execution_environment'):
                require(report['identities_before'][version][section] == report['identities_after'][version][section],
                        'frozen experiment identity changed')
        result['inputs'][path.relative_to(root).as_posix()] = digest(path)
        for blocks in (8, 32, 128):
            for version in ('baseline', 'candidate'):
                selected = [l for l in report['lanes'] if (l['case'], l['version'], l['backend'], l['threads'])
                            == (f'{module}_b{blocks}', version, 'cuda', 8)]
                require(len(selected) == 6 and sum(l['phase'] == 'warmup' for l in selected) == 1,
                        'original warmup/measurement inventory changed')
                group = summarize([l for l in selected if l['phase'] == 'measured'])
                result['groups'].append(dict(module=module, initial_blocks=blocks,
                    version=version, backend='cuda', host_threads=8, **group))
    require(len(result['groups']) == 18, 'incomplete analysis groups')
    result['measured_runs_analyzed'] = 90
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    require(not args.output.exists() and not args.output.is_symlink(), 'refuse to overwrite prior analysis')
    result = analyze(args.root.resolve(strict=True))
    result['analysis_recipe_sha256'] = digest(Path(__file__))
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2, allow_nan=False)
        stream.write('\n')
    print(json.dumps(dict(status=result['status'], groups=len(result['groups']),
                          measured_runs=result['measured_runs_analyzed'])))
