#!/usr/bin/env python3
"""Serial, source-identified ARCH/FLASH or CPU/CUDA complete-task measurements.

Use the existing O6 fixtures unchanged except output location/backend. Actual
checkpoint time and resolved backend are checked before a sample is accepted.
This manual performance entry does not add another CI matrix or error budget.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import statistics
import sys
import subprocess
import time

import h5py
import numpy as np

ROOT = Path(__file__).resolve().parents[3]
FIXTURES = Path(__file__).resolve().parent
CASES = {
    'sod': ('Sod', 'arch_sod_o6plus.par', 'flash_sod_o6plus.par', 'object_sod'),
    'jeans64': ('JeansWave', 'arch_jeans_o6plus.par', 'flash_jeans_o6plus.par', 'object_jeans'),
    'jeans128': ('JeansWave', 'arch_jeans_o6plus_128.par', 'flash_jeans_o6plus_128.par', 'object_jeans'),
    'a': ('CellularDet', 'arch_cellular_o6_matched.par', 'flash_cellular_o6plus_a.par', 'object'),
    'b': ('CellularDet', 'arch_cellular_o6_holdout.par', 'flash_cellular_o6plus_b.par', 'object'),
    'noburn': ('CellularDet', 'arch_cellular_o6plus_noburn.par', 'flash_cellular_o6plus_noburn.par', 'object'),
    'fine': ('CellularDet', 'arch_cellular_o6plus_dx025.par', 'flash_cellular_o6plus_dx025.par', 'object'),
    # Existing four-module capability fixtures: backend comparisons only.
    'snia2d': ('SNIaCoupled', '../../../simulation/SNIaCoupled/SNIaCoupled_2d_cartesian_amr.par', None, None),
    'snia3d': ('SNIaCoupled', '../../../simulation/SNIaCoupled/SNIaCoupled_3d_cylindrical_amr.par', None, None),
}


def argument_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch-cpu', type=Path, default=ROOT / 'bin/ARCH')
    parser.add_argument('--arch-cuda', type=Path, default=ROOT / 'build-cuda/bin/ARCH')
    parser.add_argument('--flash-root', type=Path, default=Path.home() / 'FLASH4.8')
    parser.add_argument('--mpirun', type=Path, default=Path('/usr/bin/mpirun'),
                        help='OpenMPI launcher matching the registered FLASH reference build')
    parser.add_argument('--helm-binary', type=Path,
                        help='existing FLASH helm_table.bdat, to preserve reference initialization scope')
    parser.add_argument('--routes', nargs='+', choices=('arch', 'flash', 'cuda'), default=['flash', 'arch'])
    parser.add_argument('--cases', nargs='+', choices=CASES,
                        default=[name for name, fixture in CASES.items() if fixture[2]])
    parser.add_argument('--threads', type=int, default=8)
    parser.add_argument('--ranks', type=int, default=8)
    parser.add_argument('--affinity', default='0,2,4,6,8,10,12,14')
    parser.add_argument('--cuda-affinity', default='0')
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--timeout', type=float, default=900.)
    parser.add_argument('--prefix')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--report-only', action='store_true',
                        help='summarize a saved manifest without running a simulation')
    parser.add_argument('--reference-manifest', type=Path, action='append', default=[],
                        help='CPU thread-scan manifest for saved CUDA field/cost comparison; repeatable')
    return parser


def replace_parameter(text, key, value):
    line = f'{key}={value}'
    pattern = rf'(?m)^{re.escape(key)}\s*=.*$'
    return re.sub(pattern, line, text) if re.search(pattern, text) else text + '\n' + line + '\n'


def checkpoint_time(folder, flash):
    checkpoint = next(folder.glob('*hdf5_chk_0001' if flash else '*_chk_0001.h5'))
    with h5py.File(checkpoint) as handle:
        if not flash:
            return float(handle.attrs['time'])
        return float(next(item['value'] for item in handle['real scalars'][()]
                          if item['name'].decode().strip() == 'time'))


def same_endpoint(case, left, right):
    """Reuse the original coupled budget; fixed-time FLASH fixtures stay exact."""
    if case.startswith('snia'):
        sys.path.insert(0, str(FIXTURES.parent / 'curved'))
        from compare_backends import physical_times_agree
        return physical_times_agree(left, right)
    return left == right


def compare_saved_backend(reference, candidate, case):
    """Check the existing O6 parity contract, including vanishing vector components."""
    def physics_input(folder):
        return '\n'.join(line for line in (folder / 'input.par').read_text().splitlines()
                         if line.split('=', 1)[0].strip() not in ('out_dir', 'compute_backend'))
    if physics_input(reference) != physics_input(candidate):
        raise ValueError(f'{case}: backend runs used different physical inputs')
    if case.startswith('snia'):
        sys.path.insert(0, str(FIXTURES.parent / 'curved'))
        from compare_backends import compare_pair
        return compare_pair(case, reference, candidate, 5)
    with h5py.File(next(reference.glob('*_chk_0001.h5'))) as a, \
            h5py.File(next(candidate.glob('*_chk_0001.h5'))) as b:
        if a.attrs['time'] != b.attrs['time']:
            raise ValueError(f'{case}: checkpoint physical endpoints differ')
        if set(a['Blocks']) != set(b['Blocks']) or any(
                not np.array_equal(a['Blocks/' + key][()], b['Blocks/' + key][()])
                for key in a['Blocks']):
            raise ValueError(f'{case}: checkpoint AMR topology differs')
        if set(a['Data']) != set(b['Data']):
            raise ValueError(f'{case}: checkpoint fields differ')
        errors = {}
        momentum = ('mom_u', 'mom_v', 'mom_w')
        momentum_scale = max(float(np.max(np.abs(a['Data/' + key][()])))
                             for key in momentum)
        for key in a['Data']:
            expected, actual = a['Data/' + key][()], b['Data/' + key][()]
            if (expected.shape != actual.shape or not np.all(np.isfinite(expected))
                    or not np.all(np.isfinite(actual))):
                raise ValueError(f'{case}: {key} shape or finiteness differs')
            if not expected.size:
                continue
            difference = float(np.max(np.abs(expected - actual)))
            scale = (1. if key == 'X' else momentum_scale if key in momentum
                     else float(np.max(np.abs(expected))))
            relative = difference / max(scale, 1e-100)
            limit = 1e-9 if key == 'X' else 2.02e-8
            if relative > limit:
                raise ValueError(f'{case}: {key} scaled Linf {relative} exceeds {limit}')
            errors[key] = {'absolute_linf': difference, 'scaled_linf': relative}
    return {'same_topology_and_time': True, 'checkpoint_errors': errors}


def summarize_saved(manifest, references):
    """Summarize all samples; verify endpoints before forming any time ratio.

    CPU/FLASH remains a task-cost comparison, not a shared scientific budget.
    CUDA requires saved CPU fields; the fastest measured CPU configuration is
    the main denominator. This analysis adds no simulation or CI test entry.
    """
    data = json.loads(manifest.read_text())
    scans = [json.loads(path.read_text()) for path in references]
    result = {}
    for case in sorted({row['case'] for row in data['records']}):
        rows = [row for row in data['records'] if row['case'] == case]
        if any(row['exit_code'] for row in rows):
            raise ValueError(f'{case}: failed samples cannot form a performance report')
        endpoints = {row['physical_time_seconds'] for row in rows}
        endpoint = rows[0]['physical_time_seconds']
        if any(not same_endpoint(case, endpoint, value) for value in endpoints):
            raise ValueError(f'{case}: saved physical endpoints differ')
        groups = {}
        for route in sorted({row['backend'] for row in rows}):
            values = [row['wall_seconds'] for row in rows if row['backend'] == route]
            groups[route] = dict(samples=len(values), median_seconds=statistics.median(values),
                                 minimum_seconds=min(values), maximum_seconds=max(values))
        entry = {'physical_time_seconds': endpoint, 'groups': groups}
        if 'arch' in groups and 'flash' in groups:
            entry['cpu_flash_task_cost_ratio'] = (groups['arch']['median_seconds']
                                                / groups['flash']['median_seconds'])
        if 'cuda' in groups and scans:
            cpu = {}
            reference = None
            for scan in scans:
                selected = [row for row in scan['records']
                            if row['case'] == case and row['backend'] == 'arch']
                if not selected:
                    continue
                if any(row['exit_code'] or row.get('resolved_backend') != 'cpu'
                       or not same_endpoint(case, row['physical_time_seconds'],
                                            entry['physical_time_seconds'])
                       for row in selected):
                    raise ValueError(f'{case}: CPU reference identity or endpoint differs')
                threads = scan['cpu_threads']
                if threads in cpu:
                    raise ValueError(f'{case}: duplicate CPU thread scan {threads}')
                cpu[threads] = statistics.median(row['wall_seconds'] for row in selected)
                if reference is None or threads == 8:
                    reference = Path(selected[0]['output'])
            if not cpu:
                raise ValueError(f'{case}: no CPU reference samples')
            best = min(cpu, key=cpu.get)
            entry.update(cpu_median_seconds=cpu, best_cpu_threads=best,
                         gpu_vs_best_cpu_speedup=cpu[best] / groups['cuda']['median_seconds'])
            if 8 in cpu:
                entry['gpu_vs_cpu8_speedup'] = cpu[8] / groups['cuda']['median_seconds']
            checks = []
            for row in rows:
                if row['backend'] == 'cuda':
                    if row.get('resolved_backend') != 'cuda':
                        raise ValueError(f'{case}: requested CUDA actually ran another backend')
                    checks.append(compare_saved_backend(reference, Path(row['output']), case))
            entry['backend_checks'] = checks
            entry['endpoint_contract'] = ('existing coupled compare_pair time budget'
                if case.startswith('snia') else 'equal checkpoint time')
        result[case] = entry
    data['analysis'] = result
    data['analysis_reference_manifests'] = [str(path.resolve()) for path in references]
    manifest.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n')
    for case, entry in result.items():
        print(case, json.dumps({key: value for key, value in entry.items()
                                if key != 'backend_checks'}), flush=True)


def main():
    args = argument_parser().parse_args()
    if args.report_only:
        summarize_saved(args.manifest, args.reference_manifest)
        return
    if args.output is None or not args.prefix:
        raise ValueError('simulation runs require --output and --prefix')
    if min(args.threads, args.ranks, args.repeats) < 1:
        raise ValueError('thread/rank/repeat counts must be positive')
    if 'flash' in args.routes and any(CASES[name][2] is None for name in args.cases):
        raise ValueError('four-module SNIa fixtures support ARCH CPU/CUDA comparison only')
    launcher_identity = None
    if 'flash' in args.routes:
        args.mpirun = args.mpirun.resolve(strict=True)
        version = subprocess.check_output([str(args.mpirun), '--version'], text=True)
        if 'Open MPI' not in version and 'OpenRTE' not in version:
            raise ValueError('FLASH reference uses OpenMPI; select its matching --mpirun, not conda MPICH')
        launcher_identity = {'path': str(args.mpirun), 'version': version.strip()}
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.manifest.exists():
        raise FileExistsError(f'refusing to overwrite evidence: {args.manifest}')
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    binaries = {'arch': args.arch_cpu.resolve(), 'cuda': args.arch_cuda.resolve()}
    if 'flash' in args.routes:
        for name in args.cases:
            directory = CASES[name][3]
            binaries['flash_' + directory] = (args.flash_root / directory / 'flash4').resolve()
    identities = {key: {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                  for key, path in binaries.items() if key in args.routes or key.startswith('flash_')}
    record = {'scope': 'complete process wall clock; serial alternating runs; actual checkpoint endpoint',
              'binary_identity': identities, 'mpi_launcher': launcher_identity,
              'cpu_threads': args.threads, 'flash_ranks': args.ranks,
              'cpu_affinity': args.affinity, 'cuda_affinity': args.cuda_affinity, 'records': []}
    expected_times = {}
    for name in args.cases:
        problem, cpu_input, flash_input, flash_directory = CASES[name]
        for repeat in range(1, args.repeats + 1):
            routes = args.routes if repeat % 2 else list(reversed(args.routes))
            for route in routes:
                folder = args.output / f'{args.prefix}_{name}_{route}_{repeat}'
                folder.mkdir()  # Preserve every previous run, even a failed one.
                env = os.environ.copy()
                env.pop('OMP_PROC_BIND', None)
                env.pop('OMP_PLACES', None)
                env['OMP_NUM_THREADS'] = str(args.threads if route == 'arch' else 1)
                if route == 'flash':
                    (folder / 'flash.par').write_text((FIXTURES / flash_input).read_text())
                    if flash_directory == 'object':
                        if args.helm_binary is None:
                            raise ValueError('Cellular FLASH runs require --helm-binary from the reference build')
                        for target, source in {'helm_table.bdat': args.helm_binary,
                            'helm_table.dat': args.flash_root / 'object/helm_table.dat',
                            'SpeciesList.txt': args.flash_root / 'object/SpeciesList.txt'}.items():
                            (folder / target).symlink_to(source.resolve(strict=True))
                    env['OMPI_MCA_btl_vader_single_copy_mechanism'] = 'none'
                    command = [str(args.mpirun), '--bind-to', 'none', '-np', str(args.ranks),
                               str(binaries['flash_' + flash_directory])]
                    cwd = folder
                else:
                    params = replace_parameter((FIXTURES / cpu_input).read_text(), 'out_dir', folder)
                    params = replace_parameter(params, 'compute_backend', 'cpu' if route == 'arch' else 'cuda')
                    (folder / 'input.par').write_text(params)
                    command = [str(binaries[route]), problem, str(folder / 'input.par')]
                    cwd = ROOT
                affinity = args.cuda_affinity if route == 'cuda' else args.affinity
                if affinity:
                    command = ['taskset', '-c', affinity] + command
                started = time.perf_counter()
                with (folder / 'run_stdout.txt').open('w') as out, (folder / 'run_stderr.txt').open('w') as err:
                    result = subprocess.run(command, cwd=cwd, env=env, stdout=out, stderr=err,
                                            timeout=args.timeout)
                row = {'case': name, 'backend': route, 'repeat': repeat,
                       'wall_seconds': time.perf_counter() - started,
                       'exit_code': result.returncode, 'output': str(folder),
                       'input_fixture': flash_input if route == 'flash' else cpu_input}
                if result.returncode == 0:
                    row['physical_time_seconds'] = checkpoint_time(folder, route == 'flash')
                    if route != 'flash':
                        plan = next(folder.glob('*_backend_plan.txt')).read_text()
                        row['resolved_backend'] = re.search(r'(?m)^resolved=(\w+)', plan).group(1)
                record['records'].append(row)
                args.manifest.write_text(json.dumps(record, indent=2) + '\n')
                print(json.dumps(row), flush=True)
                if result.returncode:
                    raise RuntimeError(f'{name}/{route} failed; see {folder}')
                if route != 'flash' and row['resolved_backend'] != ('cpu' if route == 'arch' else 'cuda'):
                    raise RuntimeError('timed executable did not run the requested backend')
                endpoint = expected_times.setdefault(name, row['physical_time_seconds'])
                if not same_endpoint(name, row['physical_time_seconds'], endpoint):
                    raise RuntimeError('different actual physical endpoints cannot share a performance ratio')


if __name__ == '__main__':
    main()
