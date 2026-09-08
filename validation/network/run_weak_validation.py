"""Archive the controlled Urca factory/trajectory gate with observed identities.

Requires a CMake build registering the weak_urca recipe and its three focused
generated test targets. This is NOT a final ARCH application certificate.
Run under tools/run_memory_guarded.py; output directories must be empty.
"""
from contextlib import redirect_stdout
from datetime import datetime, timezone
import argparse
import io
import json
from pathlib import Path
import sys

from weak_reference import reference, compare_trajectory, TrajectoryBudgetError

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import validation_provenance as provenance
from validate_backend_results import require_empty_output_root, run_arch_with_logs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--network-id', default='weak_urca')
    parser.add_argument('--eos', choices=('constant_cv', 'helmholtz'), default='constant_cv',
                        help='thermal closure for the independent trajectory gate; factory controls use cv')
    for name in ('rho', 'temperature', 'interval', 'cv'):
        parser.add_argument('--' + name, type=float, required=True)
    args = parser.parse_args()
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    require_empty_output_root(output)
    output.mkdir(parents=True, exist_ok=True)
    executables = {kind: build / f'arch_cuda_generated_weak_{kind}_{args.network_id}'
                   for kind in ('trajectory', 'factory')}
    controls = {name: getattr(args, name) for name in ('rho', 'temperature', 'interval', 'cv')}
    arguments = [str(controls[name]) for name in controls]
    def identity():
        return provenance.capture_focused(source_root=ROOT, build_dir=build, artifacts=executables)
    before = identity()
    started = datetime.now(timezone.utc).isoformat()
    independent = reference(**controls, eos=args.eos)
    commands = []
    def run(kind, lane, extra=()):
        directory = output / lane
        directory.mkdir()
        trajectory_arguments = ['--helm', *arguments[:3]] if args.eos == 'helmholtz' else arguments
        command = [str(executables[kind]),
                   *(trajectory_arguments if kind == 'trajectory' else arguments), *extra]
        commands.append(command)
        process = run_arch_with_logs(command, source_root=ROOT,
            lane_root=directory, timeout=600)
        if process.returncode != 0:  # A GPU skip is not a successful gate.
            raise RuntimeError(f'{lane} failed with code {process.returncode}; see retained logs')
        return directory / 'arch.stdout'
    factory = run('factory', 'factory')
    base_log = run('trajectory', 'base', ('1e-7',))
    try:
        with redirect_stdout(io.StringIO()):
            base = compare_trajectory(independent, base_log)
    except TrajectoryBudgetError as error:
        base = error.comparison  # Retain the failed coarse scientific control explicitly.
    tight_log = run('trajectory', 'tight', ('1e-11',))
    tight = compare_trajectory(independent, tight_log)  # Must pass the original budget.
    base_be = max(row['normalized_error'] for row in base['rows'] if row['method'] == 0)
    tight_be = max(row['normalized_error'] for row in tight['rows'] if row['method'] == 0)
    # Four decades tighter local tolerance must improve a first-order method
    # materially while retaining all original parity/closure checks.
    if not tight_be < base_be / 4:
        raise ArithmeticError('BE_NR did not converge under local-tolerance refinement')
    after = identity()
    provenance.require_unchanged(before, after)
    result = {'schema': 1, 'scope': 'controlled-Urca-weak-factory-and-trajectories',
              'release_qualified': False,
              'qualification_note': f'{args.eos} two-isotope independent scientific trajectories and '
                  'constant-cv typed owner/cell paths. Whole-application/AMR and release qualification '
                  'are recorded by their separate gates.',
              'started_utc': started, 'finished_utc': datetime.now(timezone.utc).isoformat(),
              'identity': before, 'identity_verified_after_run': True, 'commands': commands,
              'independent': independent, 'base_scientific_control': base,
              'tight_scientific_control': tight, 'be_nr_error_improvement': base_be / tight_be,
              'factory_log': provenance.file_identity(factory), 'focused_gate_pass': True}
    (output / 'evidence.json').write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
    print(json.dumps({'focused_gate_pass': True, 'release_qualified': False,
                      'be_nr_error_improvement': base_be / tight_be,
                      'evidence': str(output / 'evidence.json')}, indent=2))


if __name__ == '__main__':
    main()
