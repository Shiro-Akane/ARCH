"""Run remaining release campaigns with exactly one GPU workload at a time.

Each child uses the existing validator and resource guard; this is scheduling,
not another scientific acceptance implementation. Stop at the first failure and
retain all logs. Start only after other GPU campaigns have completed. Cold-build
and allocator capacity phases must also run without another heavy CPU workload.
"""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools'))
from validate_backend_results import require_empty_output_root, run_arch_with_logs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--debug-build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--hdf-python', type=Path, required=True)
    parser.add_argument('--scientific-python', type=Path, required=True)
    parser.add_argument('--start', default='eos-770')
    parser.add_argument('--stop', default='capacity-781')
    args = parser.parse_args()
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    require_empty_output_root(output)
    output.mkdir(parents=True, exist_ok=True)
    arch, validator = build / 'bin/ARCH', build / 'arch_cuda_single_level_validation'
    # Preserve the virtual-environment entry path: resolving its executable
    # symlink would start the system interpreter without the selected packages.
    python, hdf_python = sys.executable, str(args.hdf_python.absolute())
    scientific_python = str(args.scientific_python.absolute())
    sanitizer = '/usr/local/cuda-12.3/bin/compute-sanitizer'
    result_root = ROOT / 'validation/backend/results/final-first-law-20260907'
    runtime = [python, '-B', str(ROOT / 'tools/validate_backend_results.py'),
        '--arch', str(arch), '--checkpoint-validator', str(validator),
        '--source-root', str(ROOT), '--build-dir', str(build)]
    restart = [python, '-B', str(ROOT / 'tools/validate_cuda_amr_restart.py'),
        '--arch', str(arch), '--checkpoint-validator', str(validator),
        '--source-root', str(ROOT), '--build-dir', str(build),
        '--input', 'validation/amr/inputs/burn_enuc_amr.par', '--problem', 'BurnGradient']
    def script(interpreter, path, destination, selected_build=build):
        return [interpreter, '-B', str(ROOT / path), '--build-dir', str(selected_build),
                '--output-dir', str(ROOT / destination)]
    tasks = [
        ('eos-770', script(hdf_python, 'validation/eos/results/application-native-20260907/replay.py',
            'validation/eos/results/application-native-20260907/release-770')),
        ('nse-771', script(hdf_python, 'validation/burn/results/nse-application-native-20260907/replay.py',
            'validation/burn/results/nse-application-native-20260907/release-771')),
        ('uniform-772', runtime + ['--manifest', 'validation/backend/cases.json', '--output-root',
            str(ROOT / 'validation/backend/results/uniform-native-20260907/release-772')]),
        ('generated-773', runtime + ['--manifest', 'validation/network/runtime_cases.json', '--output-root',
            str(ROOT / 'validation/network/results/runtime-native-20260907/release-773')]),
        ('debug-774', script(python, 'validation/backend/results/final-first-law-20260907/run_regression.py',
            'validation/backend/results/final-first-law-20260907/debug-regression-774',
            args.debug_build_dir.resolve()) + ['--expected-tests', '98']),
        ('burn-775', script(hdf_python, 'validation/burn/results/application-first-law-20260907/replay.py',
            'validation/burn/results/application-first-law-20260907/release-775')),
    ]
    for number, tool in ((776, 'memcheck'), (777, 'racecheck')):
        tasks.append((f'focused-{number}', script(python,
            'validation/backend/results/final-first-law-20260907/run_sanitizers.py',
            f'validation/backend/results/final-first-law-20260907/{tool}-{number}')
            + ['--sanitizer', sanitizer, '--tool', tool]))
    for number, tool in ((778, 'memcheck'), (779, 'racecheck')):
        tasks.append((f'curved-{number}', runtime + ['--manifest', 'validation/amr/gpu_curvilinear_cases.json',
            '--case', 'spherical_coupled_amr_rkl2_wedge_3d', '--output-root',
            str(ROOT / f'validation/amr/results/curved-native-20260907/{tool}-{number}'),
            '--cuda-sanitizer', sanitizer, '--sanitizer-tool', tool]))
        tasks.append((f'restart-{number}', restart + ['--output-root',
            str(ROOT / f'validation/amr/results/restart-burn-native-20260907/{tool}-{number}'),
            '--cuda-sanitizer', sanitizer, '--sanitizer-tool', tool]))
    tasks += [
        ('hydro-time-780', script(hdf_python, 'validation/hydro/time_reference.py',
            'validation/hydro/results/time-native-20260907/release-780')),
        ('geometry-780', script(scientific_python, 'validation/amr/geometry_reference.py',
            'validation/amr/results/geometry-native-20260907/release-780')),
        ('capacity-781', script(python, 'validation/backend/results/device-memory-first-law-20260907/replay.py',
            'validation/backend/results/device-memory-first-law-20260907/release-781')
            + ['--scientific-python', scientific_python]),
    ]
    names = [name for name, _ in tasks]
    if args.start not in names or args.stop not in names or names.index(args.start) > names.index(args.stop):
        parser.error('invalid serial phase interval: ' + ', '.join(names))
    for name, command in tasks[names.index(args.start):names.index(args.stop) + 1]:
        lane = output / name
        lane.mkdir()
        guarded = [python, '-B', str(ROOT / 'tools/run_memory_guarded.py'),
            '--min-available-mib', '1536', '--max-swap-growth-mib', '256', '--pressure-guard',
            '--gpu-memory-device', '0', '--nvidia-smi', '/usr/lib/wsl/lib/nvidia-smi',
            '--log', str(lane / 'resource.log'), '--', *command]
        print('SERIAL_START ' + name, flush=True)
        result = run_arch_with_logs(guarded, source_root=ROOT, lane_root=lane, timeout=14400)
        if result.returncode:
            raise RuntimeError('serial phase failed: ' + name + '; logs retained at ' + str(lane))
        print('SERIAL_PASS ' + name, flush=True)
    print('selected serial profile completed; aggregate release decision remains separate', flush=True)


if __name__ == '__main__':
    main()
