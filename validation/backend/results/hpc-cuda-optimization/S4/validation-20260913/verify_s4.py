"""S4 validation recipe: delegates all scientific checks to existing ARCH tools.

Each phase has a new output directory and records its exact commands/identity.
Never rebuilds, adjusts tolerances, overwrites evidence, or qualifies S5/release.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    parser.add_argument('--phase', choices=('contracts', 'tails', 'amr', 'curved', 'lifecycle', 'restart', 'sanitizer', 'pilot', 'timing'), required=True)
    parser.add_argument('--sanitizer', type=Path)
    parser.add_argument('--threads', type=int, default=8)
    args = parser.parse_args()
    source, build, output = (p.resolve() for p in (args.source_root, args.build_dir, args.output_root))
    if args.threads < 1 or output == source / 'build' or not output.is_relative_to(source / 'build'):
        parser.error('positive threads and new source/build subdirectory required')
    output.mkdir(parents=True, exist_ok=False)
    sys.path.insert(0, str(source / 'tools'))
    import validation_provenance as provenance
    import validation_sanitizer
    identity_args = dict(arch=build / 'bin/ARCH', checkpoint_validator=build / 'arch_cuda_single_level_validation',
                         source_root=source, build_dir=build)
    before = provenance.capture(**identity_args)
    report = dict(phase=args.phase, status='running', release_qualified=False,
                  started_utc=datetime.now(timezone.utc).isoformat(),
                  recipe=provenance.file_identity(Path(__file__)), identity_before=before, commands=[])
    env = dict(os.environ, OMP_NUM_THREADS=str(args.threads), OMP_DYNAMIC='FALSE', OMP_PLACES='cores', OMP_PROC_BIND='close')
    report['thread_environment'] = {key: env[key] for key in ('OMP_NUM_THREADS', 'OMP_DYNAMIC', 'OMP_PLACES', 'OMP_PROC_BIND')}

    def save():
        (output / 'status.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    def execute(name, command, timeout=2400, allow_failure=False):
        lane = output / name
        lane.mkdir()
        record = dict(name=name, command=[str(v) for v in command], status='running')
        report['commands'].append(record)
        save()
        print(f'S4_PHASE_START {name}', flush=True)
        try:
            with (lane / 'stdout.log').open('w') as stdout, (lane / 'stderr.log').open('w') as stderr:
                result = subprocess.run(record['command'], cwd=source, env=env, stdout=stdout, stderr=stderr, timeout=timeout)
            record.update(returncode=result.returncode, status='passed' if result.returncode == 0 else 'failed')
        except BaseException as error:
            record.update(status='interrupted' if isinstance(error, KeyboardInterrupt) else 'failed', error=str(error))
            save()
            raise
        save()
        print(f'S4_PHASE_END {name} exit={result.returncode}', flush=True)
        if result.returncode and not allow_failure:
            raise RuntimeError(f'{name} exited {result.returncode}; original logs retained')
        return lane, record

    common = ['--arch', identity_args['arch'], '--checkpoint-validator', identity_args['checkpoint_validator'],
              '--source-root', source, '--build-dir', build]
    timing = [sys.executable, source / 'validation/backend/results/maintenance-freeze-20260908/run_sedov_amr_timing.py',
              '--arch', identity_args['arch'], '--checkpoint-validator', identity_args['checkpoint_validator'],
              '--blocks', '4', '8', '--levels', '2', '--time', '0.02', '--threads', str(args.threads),
              '--backend', 'cpu', 'cuda', '--warmups', '1', '--repeats', '5', '--timeout', '1200']
    save()
    try:
        if args.phase == 'contracts':
            tests = ['compute_backend', 'shared_stage_scheduler', 'state_residency', 'device_block_store_lifecycle',
                     'amr_operation_plans', 'same_level_exchange_plan', 'checkpoint_temporal_comparison',
                     'cuda_single_level_smoke', 'cuda_multiblock_hydro', 'cuda_multiblock_diffusion',
                     'cuda_multiblock_burn', 'cuda_amr_exchange', 'cuda_regrid_transaction', 'cuda_regrid_migration',
                     'cuda_store_lifecycle', 'hydro_leaf_parity', 'boundary_plan_parity', 'cuda_hydro_eos_failure',
                     'cuda_hydro_dispatch', 'cuda_hydro_route_matrix', 'cuda_hydro_integrator_matrix',
                     'cuda_backend_eos_owner_matrix', 'cuda_backend_diffusion_rkl1', 'cuda_backend_diffusion_rkl2']
            expression = '^(' + '|'.join(map(re.escape, tests)) + ')$'
            lane, _ = execute('ctest-inventory', ['ctest', '--test-dir', build, '--show-only=json-v1', '-R', expression])
            found = [entry['name'] for entry in json.loads((lane / 'stdout.log').read_text())['tests']]
            if sorted(found) != sorted(tests):
                raise RuntimeError('CTest inventory does not match required tests')
            lane, _ = execute('ctest', ['ctest', '--test-dir', build, '--output-on-failure', '--no-tests=error',
                                      '--parallel', '1', '--timeout', '600', '-R', expression], timeout=14400)
            text = (lane / 'stdout.log').read_text() + (lane / 'stderr.log').read_text()
            if '***Skipped' in text or 'Not Run' in text or 'tests did not run' in text:
                raise RuntimeError('skipped/unexecuted contract is not a pass')
        elif args.phase == 'tails':
            # Extra fixed-topology application controls, NOT replacements for
            # the unchanged mixed-level AMR matrix or original timing inputs.
            original = source / 'validation/amr/gpu_cases.json'
            prototype = next(case for case in json.loads(original.read_text())['cases']
                             if case['id'] == 'hydro_amr_euler_1d')
            cases, expected_counts = [], {}
            for blocks, method in ((1, 'Euler'), (1024, 'Euler'), (1025, 'Euler'), (1025, 'RK2'), (1025, 'RK3')):
                name = f'batch_boundary_{blocks}_{method.lower()}'
                expected_counts[name] = blocks
                cases.append(dict(id=name, problem=prototype['problem'], input=prototype['input'],
                    overrides=dict(nblockx1=str(blocks), nblockx2='0', nblockx3='0',
                        max_blocks=str(max(128, blocks + 16)), lrefinemin='0', lrefinemax='0',
                        regrid_interval='1', reconstruct='ppm', time_integrator=method),
                    accepted_steps=[1, 2], timeout_seconds=1200,
                    conservation_policy=prototype['conservation_policy'], reduction_policy=prototype['reduction_policy']))
            manifest = output / 'tail-wave-cases.json'
            manifest.write_text(json.dumps(dict(schema=1, description='S4 single-level 1024-block wave boundary controls', cases=cases), indent=2) + '\n')
            report['tail_scope'] = 'new 1/1024/1025 block controls; original parity/conservation budgets; original AMR matrix still required'
            report['budget_source'] = provenance.file_identity(original)
            execute('tail-wave-matrix', [sys.executable, source / 'tools/validate_backend_results.py', *common,
                    '--manifest', manifest, '--output-root', output / 'matrix'], timeout=21600)
            evidence = json.loads((output / 'matrix/backend-validation-evidence.json').read_text())
            for case in evidence['cases']:
                for item in case['checkpoints']:
                    actual = item['parity']
                    if actual['blocks'] != expected_counts[case['id']] or actual['min_level'] != 0 or actual['max_level'] != 0:
                        raise RuntimeError('tail-wave case did not retain the required actual fixed leaf count')
        elif args.phase in ('amr', 'curved'):
            manifest_name = 'gpu_cases.json' if args.phase == 'amr' else 'gpu_curvilinear_cases.json'
            execute('amr-matrix', [sys.executable, source / 'tools/validate_backend_results.py', *common,
                    '--manifest', source / 'validation/amr' / manifest_name, '--output-root', output / 'matrix'], timeout=21600)
        elif args.phase == 'lifecycle':
            execute('dynamic-3d', [sys.executable, '-B', source / 'validation/amr/results/dynamic-3d-final-20260907/replay.py',
                    '--build-dir', build, '--output-dir', output / 'dynamic-3d-results'], timeout=21600)
            inventory = {case['id']: case for case in json.loads((source / 'validation/amr/gpu_cases.json').read_text())['cases']}
            cases = []
            for name, observations in (('hydro_amr_regrid_cycle_1d', [100, 500]), ('diffusion_amr_rkl2_5stage', [25, 100])):
                case = dict(inventory[name])
                case['accepted_steps'] = sorted(set(case['accepted_steps'] + observations))
                cases.append(case)
            manifest = output / 'sustained-amr-cases.json'
            manifest.write_text(json.dumps(dict(schema=1, description='Longer observations from existing sustained AMR recipe; unchanged inputs/budgets', cases=cases), indent=2) + '\n')
            execute('sustained-amr', [sys.executable, source / 'tools/validate_backend_results.py', *common,
                    '--manifest', manifest, '--output-root', output / 'sustained-results'], timeout=21600)
        elif args.phase == 'restart':
            for name, problem, parameter in [('smooth', 'SmoothAdvection', 'smooth_amr80_l1.par'),
                                             ('burn', 'BurnGradient', 'burn_enuc_amr.par')]:
                execute(name, [sys.executable, source / 'tools/validate_cuda_amr_restart.py', *common,
                        '--problem', problem, '--input', source / 'validation/amr/inputs' / parameter,
                        '--output-root', output / (name + '-results')], timeout=14400)
        elif args.phase == 'sanitizer':
            if args.sanitizer is None:
                raise ValueError('explicit existing sanitizer executable is required')
            report['safety_scope'] = 'focused S1-S4 backend witnesses; not complete application sanitizer acceptance'
            for tool in ('memcheck', 'racecheck'):
                for target in ('arch_cuda_multiblock_hydro', 'arch_cuda_amr_exchange', 'arch_cuda_regrid_transaction', 'arch_cuda_store_lifecycle'):
                    name = tool + '-' + target
                    sanitizer = validation_sanitizer.CudaSanitizer(args.sanitizer, tool)
                    command = sanitizer.command([str(build / target)], output / name)
                    lane, record = execute(name, command, timeout=3600, allow_failure=True)
                    text = '\n'.join(path.read_text(errors='replace') for path in lane.glob('*.log'))
                    if 'GPU debugging features are disabled' in text:
                        record['status'] = 'blocked_environment'
                        report['status'] = 'blocked_environment'
                        report['reason'] = 'vGPU debugging features are disabled; no sanitizer safety pass'
                        break
                    if record['returncode']:
                        raise RuntimeError(f'{name} instrumented execution failed')
                    record['sanitizer'] = sanitizer.evidence(lane)
                    save()
                if report['status'] == 'blocked_environment':
                    break
        else:
            command = timing + ['--output-root', output / 'measurements']
            if args.phase == 'pilot':
                command += ['--pilot']
            execute(args.phase, command, timeout=21600)
        if report['status'] == 'running':
            report['status'] = 'passed'
    except BaseException as error:
        report.update(status='interrupted' if isinstance(error, KeyboardInterrupt) else 'failed', error=f'{type(error).__name__}: {error}')
        raise
    finally:
        try:
            report['identity_after'] = provenance.capture(**identity_args)
            provenance.require_unchanged(before, report['identity_after'])
            report['identity_verified_after_run'] = True
        except BaseException as error:
            report.update(status='failed', identity_error=str(error))
            raise
        finally:
            report['finished_utc'] = datetime.now(timezone.utc).isoformat()
            save()
            print(json.dumps({'phase': args.phase, 'status': report['status'], 'output': str(output)}), flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
