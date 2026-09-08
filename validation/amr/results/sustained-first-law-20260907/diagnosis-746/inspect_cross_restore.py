"""Diagnostic only: cross-backend continuation from late native-X snapshots.

Use the shared strict restart comparator. The input archive is the completed
same-backend diagnostic; this does not replace a fresh final-identity campaign.
"""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_backend_results as runtime
import validate_cuda_amr_restart as restart

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, required=True)
parser.add_argument('--output-dir', type=Path, required=True)
args = parser.parse_args()
build, output = args.build_dir.resolve(), args.output_dir.resolve()
runtime.require_empty_output_root(output)
arch, validator = build / 'bin/ARCH', build / 'arch_cuda_single_level_validation'
results = []
for origin in ('cpu', 'cuda'):
    directory = Path(__file__).resolve().parent / origin / 'continuous'
    parameter, = directory.glob('*.par')
    records = [(path, runtime.checkpoint_metadata(validator=validator,
        checkpoint=path, parameters=parameter, expected_steps=None))
        for path in directory.glob('*_chk_*.h5')]
    checkpoint, _ = restart.select_checkpoint(records, 24, True)
    reference, _ = restart.select_checkpoint(records, 26, False)
    for destination in ('cpu', 'cuda'):
        lane = restart.run_lane(arch=arch, source_root=ROOT,
            canonical_input=ROOT / 'validation/amr/inputs/burn_enuc_amr.par',
            output_root=output, name=origin + '_to_' + destination,
            problem='BurnGradient', backend=destination, max_steps=26,
            checkpoint_validator=validator, restart_file=checkpoint,
            restart_parameters=parameter, restart_step=24, restart_phase=True)
        result = runtime.compare_hdf5_checkpoints(reference, Path(lane['checkpoint']),
            restart.comparison_policy(), validator)
        results.append(dict(origin=origin, destination=destination,
                            lane=lane, strict_comparison=result))
        print(json.dumps(results[-1]), flush=True)
(output / 'diagnostic.json').write_text(json.dumps(results, indent=2) + '\n')
if not all(record['strict_comparison']['passed'] for record in results):
    raise RuntimeError('late cross-backend strict continuation differs; diagnostic only')
