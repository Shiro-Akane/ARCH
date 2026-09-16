"""Create an isolated launch-batching overlay; do not touch the v4 source/build.

Only the five launch sites are replaced. Factor-cache identity, cuDSS phases,
residual correction count, scheduling, network/EOS/ODE and floating-point flags
are not edited. Original bytes are checked and retained in the change record.
"""
import argparse
import hashlib
import json
from pathlib import Path

BASE_SHA = 'b0fae9269348dbc0bd7b3d15c93e999c3f2e3125bf7679d61cc094904cac385a'
HERE = Path(__file__).resolve().parent

SOLVE = b'''    WaveKernelBatch kernel_batch(std::span<const SparseWaveTask> tasks,
                                  const std::vector<bool>& selection) const {
        WaveKernelBatch batch{};
        batch.capacity = capacity; batch.extent = extent; batch.nonzeros = nonzeros;
        batch.offsets = offsets; batch.columns = columns;
        batch.column_offsets = column_offsets.get(); batch.column_slots = column_slots.get();
        batch.row_divisors = row_divisors.get(); batch.column_divisors = column_divisors.get();
        batch.scaled_values = scaled_values.get(); batch.scaled_rhs = scaled_rhs.get();
        batch.scaled_solution = scaled_solution.get(); batch.residual = residual.get();
        batch.correction = correction.get(); batch.statuses = statuses.get();
        for (int lane = 0; lane < capacity; ++lane)
            batch.lanes[lane] = {tasks[lane].values, tasks[lane].rhs,
                                 tasks[lane].solution, selection[lane] ? 1u : 0u};
        return batch;
    }
    CuDssResult solve_wave(std::span<const SparseWaveTask> tasks,
                          const std::vector<bool>& selected, bool correction_solve) {
        const auto batch = kernel_batch(tasks, selected);
        check_cuda(wave_normalize_rhs(batch, correction_solve, stream), "batch normalize RHS");
        ++stats.kernels;
        auto result = launch_native(CUDSS_PHASE_SOLVE);
        if (!result.success()) return result;
        check_cuda(wave_denormalize_solution(batch, correction_solve, stream), "batch denormalize solution");
        ++stats.kernels;
        if (correction_solve) {
            for (int lane = 0; lane < capacity; ++lane) {
                if (!selected[lane]) continue;
                // Retain the original correction-addition kernel and arithmetic.
                check_cuda(accumulate_sparse_correction(extent, correction.get() + n(lane),
                    tasks[lane].solution, invalid(lane), stream), "wave accumulate correction");
                ++stats.kernels;
            }
        }
        check_cuda(wave_original_residual(batch, stream), "batch original residual");
        ++stats.kernels;
        return complete_native();
    }
'''

FACTOR = b'''    for (int lane = 0; lane < p.capacity; ++lane) {
        const auto& t = tasks[lane];
        if (!active(t)) continue;
        p.stats.requested_factors += factors(t);
        p.stats.requested_solves += solves(t);
    }
    if (std::any_of(prepare.begin(), prepare.end(), [](bool b) { return b; })) {
        const auto batch = p.kernel_batch(tasks, prepare);
        check_cuda(wave_normalize_rows(batch, p.stream), "batch normalize matrix rows");
        check_cuda(wave_normalize_columns(batch, p.stream), "batch normalize matrix columns");
        p.stats.kernels += 2;
    }
'''


def transform(original):
    if hashlib.sha256(original).hexdigest() != BASE_SHA:
        raise ValueError('exact qualified v4 provider bytes required')
    start = original.index(b'    CuDssResult solve_wave(')
    end = original.index(b'\n};', start) + 1
    factor_start = original.index(b'    for (int lane = 0; lane < p.capacity; ++lane) {',
                                  original.index(b'    } guard{*this};'))
    factor_end = original.index(b'    if (!p.analyzed)', factor_start)
    include = b'#include "cuda/microphysics/SparseEquilibration.h"'
    changes = [(original[start:end], SOLVE), (original[factor_start:factor_end], FACTOR),
               (include, include + b'\n#include "SparseWaveKernels.h"')]
    changed = original
    for old, new in changes:
        if changed.count(old) != 1:
            raise ValueError('ambiguous change region')
        changed = changed.replace(old, new)
    restored = changed
    for old, new in reversed(changes):
        if restored.count(new) != 1:
            raise ValueError('ambiguous reverse change')
        restored = restored.replace(new, old)
    if restored != original:
        raise ValueError('edit escaped the three execution-only regions')
    return changed, [dict(before_sha256=hashlib.sha256(old).hexdigest(),
                          after_sha256=hashlib.sha256(new).hexdigest()) for old, new in changes]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError('new isolated output required')
    original = (HERE.parent / 'CuDssSparseWaveSolver.cpp').read_bytes()
    changed, regions = transform(original)
    files = {'CuDssSparseWaveSolver.cpp': changed}
    for name in ('SparseWaveKernels.h', 'SparseWaveKernels.cu'):
        files[name] = (HERE / name).read_bytes()
    # Keep ABI identical for the already compiled nuclear factory objects.
    files['CuDssSparseWaveSolver.h'] = (HERE.parent / 'CuDssSparseWaveSolver.h').read_bytes()
    args.output.mkdir(parents=True)
    for name, content in files.items():
        with (args.output / name).open('xb') as stream:
            stream.write(content)
    record = dict(status='prepared_not_compiled_not_runtime_validated', base_provider_sha256=BASE_SHA,
                  replaced_regions=regions, shared_math_modified=False, factory_ABI_modified=False,
                  scope='CUDA launch grouping only; maximum 32 matrices; same scalar math and statuses',
                  scientific_qualified=False, performance_qualified=False,
                  files={name: dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
                         for name, data in files.items()})
    with (args.output / 'overlay-record.json').open('x') as stream:
        json.dump(record, stream, indent=2)
        stream.write('\n')
    print(json.dumps(record, indent=2))


if __name__ == '__main__':
    main()
