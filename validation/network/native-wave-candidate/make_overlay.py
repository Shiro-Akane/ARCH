"""Prepare a new test-only sparse host-scheduler overlay; never build or run it.

The original GPU continuation/correction kernels are preserved. All canonical
inputs are hash-pinned. Production files and active server jobs are untouched.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath

SCHEDULER = 'src/cuda/microphysics/SparseOdeBatch.cuh'
SCHEDULER_SHA256 = 'dc7d736d6845019e0537a0327bba57433be3b13771dbf8aa7bea9c814dbf179f'
REQUIRED_SHARED = {
    'src/cuda/microphysics/CuDssSparseSolver.h',
    'src/cuda/microphysics/SparseEquilibration.h',
    'src/cuda/microphysics/SparseEquilibration.cu',
    'src/cuda/common/DeviceAllocation.h',
    'src/numerics/linalg/CsrMatrixView.h',
    'src/numerics/linalg/LinearEquilibration.h',
    'src/numerics/linalg/SparseResidual.h',
    'src/core/CompensatedSum.h',
    'src/core/ArchPortability.h',
}
START = '            int outstanding = 0;\n'
END = '            if (outstanding == 0) return;\n'
WAVE_SCHEDULER = '''            int outstanding = 0;
            bool invalidate_previous = false;
            std::fill(wave_tasks_.begin(), wave_tasks_.end(), experimental::SparseWaveTask{});
            std::fill(responses_.begin(), responses_.end(), 0);
            // Validate every request before any new native numeric operation.
            for (int lane = 0; lane < count; ++lane) {
                const int raw = requests_[lane];
                if (raw == sparse_burn_detail::invalid_structure_request)
                    throw std::runtime_error("Sparse ODE network wrote outside its symbolic pattern");
                constexpr int last = static_cast<int>(OdeLinearRequest::SolveWithFactors);
                if (raw < -last - 2 || raw > last)
                    throw std::runtime_error("Sparse ODE returned an unknown linear request");
                invalidate_previous = invalidate_previous
                    || sparse_burn_detail::rejected_previous_response(raw);
            }
            if (invalidate_previous) {
                if (!provider_) throw std::logic_error("Sparse ODE rejected a response without a provider");
                provider_->invalidate();
            }
            for (int lane = 0; lane < count; ++lane) {
                const auto request = sparse_burn_detail::decode_request(requests_[lane]);
                if (request == OdeLinearRequest::Complete) continue;
                ++outstanding;
                auto& task = wave_tasks_[lane];
                task.values = batch_.coefficients + static_cast<std::size_t>(lane) * batch_.nonzeros;
                task.rhs = batch_.contexts[lane].b;
                task.solution = batch_.solutions + static_cast<std::size_t>(lane) * Network::ODE_NEQ;
                using Operation = experimental::SparseWaveOperation;
                if (request == OdeLinearRequest::SolveWithFactors) {
                    if (factor_tokens_[lane] == 0)
                        throw std::logic_error("Sparse ODE reused a factor before any factor request");
                    task.operation = Operation::SolveWithFactors;
                } else {
                    if (next_token_ == std::numeric_limits<std::uint64_t>::max())
                        throw std::overflow_error("Sparse ODE matrix token exhausted");
                    factor_tokens_[lane] = ++next_token_;
                    task.operation = request == OdeLinearRequest::Factorize
                        ? Operation::Factorize : Operation::FactorizeAndSolve;
                }
                task.token = factor_tokens_[lane];
                responses_[lane] = 1;
            }
            if (outstanding == 0) return;
            if (!provider_) {
                provider_ = std::make_unique<experimental::CuDssSparseWaveSolver>(
                    batch_.capacity, Network::ODE_NEQ, batch_.nonzeros,
                    batch_.row_offsets, batch_.column_indices, stream_);
            }
            // Native failure is fatal. The existing shared residual kernel
            // below still supplies numerical rejection to the same ODE.
            provider_->execute(wave_tasks_).require_success();
'''


def sha(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('missing or ambiguous overlay anchor: ' + old[:80])
    return text.replace(old, new, 1)


def rewrite_scheduler(raw):
    if sha(raw) != SCHEDULER_SHA256:
        raise ValueError('canonical scheduler changed; review a new overlay, never fuzzy-patch it')
    before = raw.decode('utf-8').replace('\r\n', '\n')
    text = replace_once(before, '#include "CuDssSparseSolver.h"', '#include "CuDssSparseWaveSolver.h"')
    text = replace_once(text, '#include <cstddef>', '#include <algorithm>\n#include <cstddef>')
    text = replace_once(text, '        factor_tokens_.resize(batch.capacity);',
        '        factor_tokens_.resize(batch.capacity);\n        wave_tasks_.resize(batch.capacity);')
    if text.count(START) != 1 or text.count(END) != 1:
        raise ValueError('ambiguous host scheduler extent')
    begin = text.index(START)
    end = text.index(END, begin) + len(END)
    text = text[:begin] + WAVE_SCHEDULER + text[end:]
    for old, new in (('provider_->kernel_count()', 'provider_->statistics().kernels'),
                     ('provider_->bytes_d2h()', 'provider_->statistics().bytes_d2h'),
                     ('provider_->bytes_h2d()', 'provider_->statistics().bytes_h2d'),
                     ('provider_->synchronization_count()', 'provider_->statistics().synchronizations')):
        text = replace_once(text, old, new)
    text = replace_once(text,
        '    // One provider owns the required factor and a count/memory-bounded optional\n'
        '    // cache; a different lane\'s token can still evict the selected factors.\n'
        '    std::unique_ptr<CuDssSparseSolver> provider_;',
        '    // Test-only fixed, bounded native cohort; all matrix-system work is counted.\n'
        '    std::unique_ptr<experimental::CuDssSparseWaveSolver> provider_;\n'
        '    std::vector<experimental::SparseWaveTask> wave_tasks_;')
    text = replace_once(text, '    std::uint64_t cached_factor_token_ = 0;\n', '')
    # Explicit finish gates for the device math and response transport suffix.
    marker = 'namespace sparse_burn_detail {'
    closing = '} // namespace sparse_burn_detail'
    def kernel_region(value):
        return value[value.index(marker):value.index(closing) + len(closing)]
    if kernel_region(before) != kernel_region(text):
        raise ValueError('device continuation/correction region changed')
    suffix = '            sparse_burn_detail::checked(cudaMemcpyAsync(batch_.responses, responses_.data(),'
    suffix_end = '\nprivate:\n'
    def response_region(value):
        return value[value.index(suffix):value.index(suffix_end)]
    if response_region(before) != response_region(text):
        raise ValueError('response upload or shared correction acceptance changed')
    return text.encode('utf-8')


def prepare(root, candidate, output):
    root, candidate = Path(root).resolve(strict=True), Path(candidate).resolve(strict=True)
    output = Path(output)
    if output.exists() or output.is_symlink():
        raise ValueError('output must be new; existing/partial evidence is never overwritten')
    output = output.parent.resolve(strict=True) / output.name
    if output == root or root.is_relative_to(output) or any(
            output.is_relative_to(root / part) for part in ('src', 'cmake', 'cases', 'tests', '.git')):
        raise ValueError('output cannot replace canonical project files')
    manifest_bytes = (candidate / 'shared-inputs.json').read_bytes()
    manifest = json.loads(manifest_bytes)
    keys = set(manifest['sha256'])
    for relative in keys:
        pure = PurePosixPath(relative)
        if pure.is_absolute() or '..' in pure.parts or '\\' in relative or not pure.parts or pure.parts[0] != 'src':
            raise ValueError('unsafe canonical input path')
    if keys != REQUIRED_SHARED:
        raise ValueError('canonical input inventory differs from the reviewed nine files')
    payload = {}
    for relative, expected in manifest['sha256'].items():
        pure = PurePosixPath(relative)
        if pure.is_absolute() or '..' in pure.parts or '\\' in relative or pure.parts[0] != 'src':
            raise ValueError('unsafe canonical input path')
        source = root.joinpath(*pure.parts)
        if source.is_symlink() or not source.resolve(strict=True).is_relative_to(root):
            raise ValueError('canonical input escapes root')
        raw = source.read_bytes()
        if sha(raw) != expected:
            raise ValueError('canonical input SHA mismatch: ' + relative)
        payload[relative] = raw
    raw_scheduler = (root / SCHEDULER).read_bytes()
    payload[SCHEDULER] = rewrite_scheduler(raw_scheduler)
    for name in ('CuDssSparseWaveSolver.h', 'CuDssSparseWaveSolver.cpp'):
        payload['src/cuda/microphysics/' + name] = (candidate / name).read_bytes()
    payload['tests/cuda/test_sparse_wave.cpp'] = (candidate / 'test_sparse_wave.cpp').read_bytes()
    record = dict(status='preparing', scope='test-only generated overlay; no compile or runtime',
        release_qualified=False, canonical_commit=manifest['canonical_commit'],
        canonical_scheduler_sha256=sha(raw_scheduler), shared_manifest_sha256=sha(manifest_bytes),
        recipe_sha256=sha(Path(__file__).read_bytes()),
        unchanged_device_math_and_response_regions=True, files={}, pending=None)
    output.mkdir()
    def save():
        (output / 'overlay-record.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    try:
        for relative, data in payload.items():
            record['pending'] = relative
            save()
            dest = output / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            with dest.open('xb') as stream:
                stream.write(data)
            record['files'][relative] = dict(bytes=len(data), sha256=sha(data))
            record['pending'] = None
        record['status'] = 'prepared_not_compiled_not_runtime_validated'
    except BaseException as error:
        record.update(status='failed_partial_preserved', error=repr(error))
        raise
    finally:
        save()
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = prepare(args.root, Path(__file__).parent, args.output)
    print(json.dumps(dict(status=result['status'], files=len(result['files']),
                         release_qualified=result['release_qualified'])))
