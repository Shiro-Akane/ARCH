"""Diagnostic: separate stop/restore drift from cross-backend evolution drift.

Uses the existing strict checkpoint comparator and original BurnGradient input.
This is not the final mixed-backend/fixed-time qualification recipe.
"""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_backend_results as runtime
import validate_cuda_amr_restart as restart
import validation_provenance as provenance

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, required=True)
parser.add_argument('--output-dir', type=Path, required=True)
args = parser.parse_args()
build, output = args.build_dir.resolve(), args.output_dir.resolve()
runtime.require_empty_output_root(output)
arch, validator = build / 'bin/ARCH', build / 'arch_cuda_single_level_validation'
identity_args = dict(arch=arch, checkpoint_validator=validator, source_root=ROOT, build_dir=build)
before = provenance.capture(**identity_args)
for backend in ('cpu', 'cuda'):
    def run(name, steps, restored=None):
        extra = {} if restored is None else dict(restart_file=restored[0],
            restart_parameters=restored[1], restart_step=restored[2], restart_phase=True)
        return restart.run_lane(arch=arch, source_root=ROOT,
            canonical_input=ROOT / 'validation/amr/inputs/burn_enuc_amr.par',
            output_root=output / backend, name=name, problem='BurnGradient',
            backend=backend, max_steps=steps, checkpoint_validator=validator, **extra)
    reference, source = run('continuous', 26), run('segment_0', 3)
    for index in range(1, 13):
        step, parameter = 2 * index, Path(source['parameter'])
        records = [(path, runtime.checkpoint_metadata(validator=validator,
            checkpoint=path, parameters=parameter, expected_steps=None))
            for path in parameter.parent.glob('*_chk_*.h5')]
        checkpoint, metadata = restart.select_checkpoint(records, step, True)
        source = run('segment_' + str(index), 26 if index == 12 else step + 3,
                     (checkpoint, parameter, step))
    result = runtime.compare_hdf5_checkpoints(Path(reference['checkpoint']),
        Path(source['checkpoint']), restart.comparison_policy(), validator)
    print(json.dumps(dict(backend=backend, strict_restart=result)), flush=True)
    if not result['passed'] or result['output_index_offsets'] != restart.output_index_offsets(None):
        raise RuntimeError('same-backend chained restart diverged')
provenance.require_unchanged(before, provenance.capture(**identity_args))
print('same-backend chained restore diagnostic PASS; fixed-time mixed-backend qualification remains separate')
