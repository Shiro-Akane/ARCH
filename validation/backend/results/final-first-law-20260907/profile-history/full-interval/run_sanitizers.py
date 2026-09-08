"""Final focused sanitizer inventory, including repaired thermal/weak routes.

Run under the existing memory guard. The shared CUDA instrumentation boundary
owns report validation; this recipe only selects real configured executables
and explicit physical inputs. Full application/restart evidence is separate.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools'))
import validation_provenance as provenance
from validation_sanitizer import CudaSanitizer
from validate_backend_results import require_empty_output_root, run_arch_with_logs

CASES = (
    'cuda_regrid_transaction', 'cuda_regrid_migration', 'cuda_amr_composition',
    'cuda_amr_exchange', 'cuda_curvilinear_geometry_smoke',
    'cuda_refinement_indicators', 'cuda_single_level_smoke',
    'cuda_cudss_sparse_solver', 'cuda_sparse_be_nr_batch',
    'cuda_burn_eos_failure', 'cuda_hydro_eos_failure',
    'eos_host_device_parity', 'network_nse_device',
    'cuda_generated_math_audit31', 'cuda_generated_math_weak_urca',
    'cuda_backend_burn_aprox19_be_nr', 'cuda_backend_burn_aprox19_bd',
    'cuda_backend_burn_aprox19_ros4', 'cuda_burn_thermal_math',
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--sanitizer', type=Path, required=True)
    parser.add_argument('--tool', choices=('memcheck', 'racecheck'), required=True)
    parser.add_argument('--timeout-seconds', type=int, default=1200,
                        help='positive wall-time allowance for each complete route (default: 1200)')
    args = parser.parse_args()
    if args.timeout_seconds <= 0:
        parser.error('--timeout-seconds must be positive')
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    require_empty_output_root(output)
    output.mkdir(parents=True, exist_ok=True)
    query = run_arch_with_logs(['ctest', '--test-dir', str(build), '--show-only=json-v1'],
                              source_root=ROOT, lane_root=output, timeout=60)
    if query.returncode:
        raise RuntimeError('cannot inspect configured sanitizer inventory')
    configured = {test['name']: test['command'] for test in json.loads(query.stdout)['tests']}
    commands = {name: configured[name] for name in CASES}
    weak = str(build / 'arch_cuda_generated_weak_trajectory_weak_urca')
    commands.update({
        'weak_constant_cv': [weak, '4e9', '5e8', '10', '1e8', '1e-11'],
        'weak_helmholtz': [weak, '--helm', '4e9', '5e8', '.01', '1e-11'],
        'weak_owner_cells': [str(build / 'arch_cuda_generated_weak_factory_weak_urca'),
                             '4e9', '5e8', '.01', '1e8'],
        'audit31_sparse_cells': [str(build / 'arch_cuda_generated_sparse_burn_audit31'),
                                '1e7', '3e9', '1e-10', '1e8', '1e-7', '4', 'c12=0.5', 'o16=0.5'],
    })
    def identity():
        return provenance.capture_focused(source_root=ROOT, build_dir=build,
            artifacts={name: Path(command[0]) for name, command in commands.items()})
    before, recipe = identity(), provenance.file_identity(Path(__file__).resolve())
    instrument = CudaSanitizer(args.sanitizer, args.tool)
    started, records = datetime.now(timezone.utc).isoformat(), []
    for name, command in commands.items():
        lane = output / name
        lane.mkdir()
        result = run_arch_with_logs(command, source_root=ROOT, lane_root=lane,
                                   timeout=args.timeout_seconds, sanitizer=instrument)
        if result.returncode:
            raise RuntimeError(f'{name} failed with exit {result.returncode}; retained logs at {lane}')
        records.append(dict(name=name, command=command, returncode=0,
            sanitizer=instrument.evidence(lane), stdout=provenance.file_identity(lane / 'arch.stdout'),
            stderr=provenance.file_identity(lane / 'arch.stderr')))
        print(f'{args.tool} PASS: {name}', flush=True)
    provenance.require_unchanged(before, identity())
    if recipe != provenance.file_identity(Path(__file__).resolve()):
        raise RuntimeError('sanitizer recipe changed during execution')
    evidence = dict(schema=1, scope='final-focused-backend-sanitizer', tool=args.tool,
        timeout_seconds=args.timeout_seconds,
        focused_gate_pass=True, release_qualified=False, identity=before,
        identity_verified_after_run=True, recipe=recipe, cases=records,
        started_utc=started, finished_utc=datetime.now(timezone.utc).isoformat())
    (output / 'evidence.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print(f'{args.tool}: all {len(records)} focused routes PASS')


if __name__ == '__main__':
    main()
