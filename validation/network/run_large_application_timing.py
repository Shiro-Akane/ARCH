"""Qualified, same-binary CPU/KLU versus CUDA/cuDSS application timing.

Requires the original large runtime matrix to have passed on this exact source
and binary. Weak reactions need not conserve Ye; no aprox13 source-balance
formula is borrowed here. This is parity/closure/workload evidence, not an
independent nuclear-rate or weak-source energy oracle.
"""
import argparse
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import statistics
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'validation/backend'))
import validation_provenance as provenance
import validate_backend_results as validation
import run_microphysics_timing as timing


def require(value, message):
    if not value:
        raise RuntimeError(message)


def configurations(threads, gpu_threads):
    require(threads and len(threads) == len(set(threads)) and min(threads) > 0
            and gpu_threads > 0, 'positive distinct CPU threads and GPU Host threads required')
    return [('cpu', t) for t in threads] + [('cuda', gpu_threads)]


def expected_species(case):
    network = case['plan_policy']['network']
    require(network in ('custom:audit150', 'custom:audit200'), 'unsupported large network')
    require(case['problem'] == 'BurnOneZone'
            and case['plan_policy']['eos'] == 'helmholtz'
            and case['plan_policy']['linear'] == {'cpu': 'sparseklu', 'cuda': 'cudss'},
            'large timing must retain the original Helm/KLU/cuDSS application')
    return int(network.removeprefix('custom:audit'))


def trajectory_quality(initial, final, count, closure_budget):
    """Additional finite/bounds/evolution gates; changing Ye is reported, not rejected."""
    require(closure_budget == 1e-8, 'original large application closure budget changed')
    fields = ('rho', 'eng', 'mom_u', 'mom_v', 'mom_w', 'enuc_rate', 'X', 'rhoX')
    for state in (initial, final):
        require(set(fields).issubset(state), 'incomplete one-zone checkpoint fields')
        require(all(np.all(np.isfinite(state[k])) for k in fields), 'nonfinite large-network state')
        require(state['X'].shape[0] == count and state['rhoX'].shape == state['X'].shape,
                'large network species layout/count changed')
        require(np.all(state['rho'] == 1e7) and np.all(state['eng'] > 0), 'one-zone density/energy changed invalidly')
        require(all(np.all(state[k] == 0) for k in ('mom_u', 'mom_v', 'mom_w')),
                'uniform one-zone acquired momentum')
        fractions = state['X']
        require(np.max(np.abs(np.sum(fractions, axis=0) - 1)) <= closure_budget
                and np.min(fractions) >= -1e-12 and np.max(fractions) <= 1 + 1e-12,
                'large network closure/fraction bounds failed')
        require(np.max(np.abs(state['rhoX'] / state['rho'] - fractions)) <= 1e-12,
                'conserved and primitive species fields disagree')
        require(state['A'].shape == state['Z'].shape == (count,)
                and state['names'].shape == (count,) and len(set(state['names'].tolist())) == count
                and np.all(np.isfinite(state['A'])) and np.all(np.isfinite(state['Z']))
                and np.all(state['A'] > 0) and np.all(state['Z'] >= 0)
                and np.all(state['Z'] <= state['A']), 'invalid species metadata')
    require(all(np.array_equal(initial[k], final[k]) for k in ('A', 'Z', 'names')),
            'species metadata changed during timing')
    require(initial['X'].shape == final['X'].shape and initial['rho'].shape == final['rho'].shape,
            'uniform one-zone topology changed')
    dx = final['X'].astype(np.longdouble) - initial['X'].astype(np.longdouble)
    evolution = float(np.max(np.abs(dx)))
    require(evolution > 1e-9, 'large timing input did not measurably burn')
    z_over_a = final['Z'].astype(np.longdouble) / final['A'].astype(np.longdouble)
    charge_change = float(np.max(np.abs(np.sum(dx * z_over_a.reshape((-1,) + (1,) * (dx.ndim - 1)), axis=0))))
    return dict(species=count, max_species_evolution=evolution,
                species_sum_error=float(np.max(np.abs(np.sum(final['X'], axis=0) - 1))),
                species_sum_budget=closure_budget, max_ye_change=charge_change,
                require_constant_ye=False, independent_weak_source_energy_oracle=False)


def read_state(path):
    import h5py
    with h5py.File(path) as f:
        state = {key: f['Data/' + key][:] for key in
                 ('rho', 'eng', 'mom_u', 'mom_v', 'mom_w', 'enuc_rate', 'X', 'rhoX')}
        state.update({key: f['Species/' + dataset][:] for key, dataset in
                      (('A', 'A'), ('Z', 'Z'), ('names', 'name'))})
        return state


def require_original_gate(report, cases, manifest_hash, identity):
    require(report.get('identity_verified_after_run') is True
            and report.get('manifest_sha256') == manifest_hash, 'unverified or different original runtime matrix')
    provenance.require_unchanged(report['provenance'], identity)
    by_id = {c['id']: c for c in report['cases']}
    require(len(by_id) == len(report['cases']), 'duplicate original case evidence')
    for case in cases:
        row = by_id[case['id']]
        require([c['cpu']['steps'] for c in row['checkpoints']] == case['accepted_steps']
                and [c['cuda']['steps'] for c in row['checkpoints']] == case['accepted_steps']
                and all(c['parity']['passed'] for c in row['checkpoints'])
                and row['scientific']['parity']['passed'], 'original large runtime gate incomplete/failed')


def run_lane(args, case, backend, threads, phase, repeat, row, save):
    directory = args.output_root / case['id'] / f'{phase}-{repeat}-{backend}-t{threads}'
    directory.mkdir(parents=True)
    parameter = directory / 'run.par'
    validation.render_terminal_parameter_file(ROOT / case['input'], parameter,
        backend=backend, output_dir=directory, base_name='LargeTiming',
        terminal_time=case['scientific_time'], scientific_overrides=case['overrides'])
    env = dict(os.environ, OMP_NUM_THREADS=str(threads), OMP_DYNAMIC='FALSE',
               OMP_PLACES='cores', OMP_PROC_BIND='close')
    row.update(case=case['id'], backend=backend, threads=threads, phase=phase, repeat=repeat,
        status='running', directory=str(directory), parameter=provenance.file_identity(parameter),
        effective_parameters=validation.read_parameter_map(parameter),
        environment={k: env[k] for k in ('OMP_NUM_THREADS', 'OMP_DYNAMIC', 'OMP_PLACES', 'OMP_PROC_BIND')},
        command=[str(args.build_dir / 'bin/ARCH'), case['problem'], str(parameter)])
    save()
    row.update(timing.timed_process(row['command'], ROOT, env, directory, args.timeout))
    save()
    require(row['returncode'] == 0 and not row['timed_out'], 'large ARCH timing process failed; logs retained')
    require(row['parameter'] == provenance.file_identity(parameter), 'parameter changed')
    initial, final = [directory / f'LargeTiming_chk_{i:04d}.h5' for i in (0, 1)]
    require(sorted(directory.glob('*_chk_*.h5')) == [initial, final], 'unexpected checkpoint inventory')
    validator = args.build_dir / 'arch_cuda_single_level_validation'
    meta = validation.checkpoint_metadata(validator=validator, checkpoint=final,
        parameters=parameter, expected_steps=None)
    require(meta['time'] == case['scientific_time'] and meta['step'] > 0, 'wrong final step/time')
    match = validation.STEP_RE.search((directory / 'arch.stdout').read_text())
    require(match is not None and int(match.group(1)) == meta['step'], 'stdout/checkpoint step mismatch')
    row.update(checkpoint=str(final), initial_checkpoint=str(initial), metadata=meta,
        checkpoint_identity=provenance.file_identity(final),
        plan=validation.validate_resolved_plan(directory / 'LargeTiming_backend_plan.txt', backend, case['plan_policy']),
        qualification=validation.qualify_checkpoint(validator, final, case, scientific=True, parameter_file=parameter),
        regrid=validation.read_regrid_metrics(directory / 'LargeTiming_regrid.tsv', backend, meta['step']),
        trajectory=trajectory_quality(read_state(initial), read_state(final), expected_species(case),
            case['qualification']['species_sum_atol']))
    if backend == 'cuda':
        row['trace'] = validation.validate_cuda_trace(directory / 'LargeTiming_backend_trace.tsv', meta['step'])
    row['status'] = 'passed'
    save()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir', type=Path, required=True)
    p.add_argument('--validation-evidence', type=Path, required=True)
    p.add_argument('--output-root', type=Path, required=True)
    p.add_argument('--case', action='append', default=[])
    p.add_argument('--threads', type=int, nargs='+', default=[1, 8, 16])
    p.add_argument('--gpu-threads', type=int, default=8)
    p.add_argument('--warmups', type=int, default=1)
    p.add_argument('--repeats', type=int, default=5)
    p.add_argument('--timeout', type=float, default=3600)
    p.add_argument('--pilot', action='store_true')
    args = p.parse_args()
    require(not os.environ.get('LD_PRELOAD'), 'uncontrolled preload is forbidden')
    lanes = configurations(args.threads, args.gpu_threads)
    require(args.warmups >= 1 and args.repeats >= 5 and math.isfinite(args.timeout) and args.timeout > 0,
            'formal protocol requires warmup and five repetitions with bounded timeout')
    if args.pilot:
        args.warmups, args.repeats = 0, 1
    args.build_dir, args.output_root = args.build_dir.resolve(), args.output_root.resolve()
    require(args.output_root != ROOT / 'build' and args.output_root.is_relative_to(ROOT / 'build'),
            'use a new source/build output directory')
    manifest = ROOT / 'validation/network/large_runtime_cases.json'
    cases = validation.select_cases(json.loads(manifest.read_text()), args.case)
    for case in cases:
        expected_species(case)
    identity_args = dict(source_root=ROOT, build_dir=args.build_dir, arch=args.build_dir / 'bin/ARCH',
                         checkpoint_validator=args.build_dir / 'arch_cuda_single_level_validation')
    identity = provenance.capture(**identity_args)
    manifest_id = provenance.file_identity(manifest)
    gate_id = provenance.file_identity(args.validation_evidence)
    require_original_gate(json.loads(args.validation_evidence.read_text()), cases, manifest_id['sha256'], identity)
    args.output_root.mkdir(parents=True, exist_ok=False)
    report = dict(status='running', pilot=args.pilot, release_qualified=False,
        started_utc=datetime.now(timezone.utc).isoformat(), cases=cases, configurations=lanes,
        scope='same-binary KLU/cuDSS startup-to-exit including I/O, excluding qualification; not steady-state',
        recipe=provenance.file_identity(Path(__file__)), identity_before=identity,
        original_gate=gate_id, manifest=manifest_id, runtime_inputs=validation.runtime_case_inputs(cases, ROOT),
        lanes=[], comparisons=[], statistics=[])
    def save():
        (args.output_root / 'evidence.json').write_text(json.dumps(report, indent=2, default=str) + '\n')
    save()
    try:
        for case in cases:
            reference = None
            for phase, count in (('warmup', args.warmups), ('measured', args.repeats)):
                for repeat in range(count):
                    for backend, threads in (lanes if repeat % 2 == 0 else list(reversed(lanes))):
                        row = {}
                        report['lanes'].append(row)
                        print(f'LARGE_TIMING_START {case["id"]} {phase} {repeat} {backend} t{threads}', flush=True)
                        run_lane(args, case, backend, threads, phase, repeat, row, save)
                        if reference is None:
                            reference = row
                        else:
                            report['comparisons'].append(timing.compare(case, reference, row,
                                args.build_dir / 'arch_cuda_single_level_validation'))
                        save()
                        print(f'LARGE_TIMING_PASS {row["arch_wall_seconds"]:.6f}s', flush=True)
            for backend, threads in lanes:
                samples = [r['arch_wall_seconds'] for r in report['lanes'] if
                    (r['case'], r['backend'], r['threads'], r['phase']) == (case['id'], backend, threads, 'measured')]
                report['statistics'].append(dict(case=case['id'], backend=backend, threads=threads,
                    samples=samples, median=statistics.median(samples), minimum=min(samples), maximum=max(samples),
                    population_stdev=statistics.pstdev(samples)))
        report['status'] = 'passed'
    except BaseException as error:
        report.update(status='failed', error=repr(error))
        raise
    finally:
        try:
            report['identity_after'] = provenance.capture(**identity_args)
            provenance.require_unchanged(identity, report['identity_after'])
            require(report['recipe'] == provenance.file_identity(Path(__file__))
                    and manifest_id == provenance.file_identity(manifest)
                    and gate_id == provenance.file_identity(args.validation_evidence)
                    and report['runtime_inputs'] == validation.runtime_case_inputs(cases, ROOT), 'timing input identity changed')
        except BaseException as error:
            report.update(status='failed', identity_error=repr(error))
            raise
        finally:
            report['finished_utc'] = datetime.now(timezone.utc).isoformat()
            save()


if __name__ == '__main__':
    main()
