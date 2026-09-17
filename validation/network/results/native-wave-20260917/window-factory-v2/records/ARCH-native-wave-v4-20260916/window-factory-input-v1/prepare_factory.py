"""Prepare (never build/dispatch) an isolated bounded-window factory overlay.

Only two pinned execution/ownership headers change reversibly. Window selection
is test-only Host setup; no kernel launch shape, mathematical body, response
validation, block gather/scatter or production registration is changed.
"""
import argparse
import hashlib
import json
from pathlib import Path

PINS = {
    'src/cuda/microphysics/SparseOdeBatch.cuh': 'ae3cc59770f4b952a31ded69734fa80304e06665648d40ef173551fa28edbe58',
    'src/cuda/runtime/burn/CudaBackendBurnSparseImpl.cuh': '5345948738d8701889b56bbc722f8bdb466d243e94bf43615f1e1d8bddde3932',
}


def transform(original, relative):
    if relative not in PINS or hashlib.sha256(original).hexdigest() != PINS[relative]:
        raise ValueError('unreviewed original factory execution header')
    newline = b'\r\n' if original.count(b'\r\n') == original.count(b'\n') else b'\n'
    encode = lambda text: text.replace('\n', newline.decode()).encode()
    if relative.endswith('/SparseOdeBatch.cuh'):
        replacements = [
            ('#include "CuDssSparseWaveSolver.h"', '#include "CuDssSparseWindowSolver.h"'),
            ('std::make_unique<experimental::CuDssSparseWaveSolver>(\n'
             '                    batch_.capacity, Network::ODE_NEQ, batch_.nonzeros,',
             'std::make_unique<experimental::CuDssSparseWindowSolver>(\n'
             '                    batch_.capacity, std::min(batch_.capacity, 32), Network::ODE_NEQ, batch_.nonzeros,'),
            ('std::unique_ptr<experimental::CuDssSparseWaveSolver> provider_;',
             'std::unique_ptr<experimental::CuDssSparseWindowSolver> provider_;'),
        ]
    else:
        helper = '''// Isolated experiment selector, not a production parameter or warp override.
inline int experimental_sparse_window_limit(int warp)
{
    if (warp != 32) throw std::runtime_error("Window experiment requires the measured 32-wide device warp");
    const char* selected = std::getenv("ARCH_NATIVE_WINDOW_CELLS");
    if (!selected || std::strcmp(selected, "32") == 0) return 32;
    if (std::strcmp(selected, "64") == 0) return 64;
    if (std::strcmp(selected, "128") == 0) return 128;
    throw std::invalid_argument("Window experiment accepts only 32, 64, or 128");
}
'''
        old_comment = '''        // A hardware-sized active group bounds ODE storage and is shared across
        // ALL blocks and AMR generations. The provider's private factor cache
        // is separately bounded by owner count and native peak-memory estimates.
        // This is not a claim that cuDSS's opaque factor memory equals lane_bytes_.'''
        new_comment = '''        // Test-only logical window; native factors still have <=32 slots and
        // their separate unchanged 256 MiB estimate budget. This owner still
        // executes one block at a time, not an implicit cross-block gather.
        // The explicit 32 MiB ODE workspace cap is not a whole-GPU memory limit.'''
        replacements = [
            ('#include "numerics/linalg/CsrPattern.h"',
             '#include "numerics/linalg/CsrPattern.h"\n#include <cstdlib>\n#include <cstring>\n#include <iostream>'),
            ('namespace arch::cuda::burn_detail {\ntemplate <class Network, template <class, class, class> class Solver, class Eos>',
             'namespace arch::cuda::burn_detail {\n'+helper+'template <class Network, template <class, class, class> class Solver, class Eos>'),
            ('        const std::size_t fitting = available / lane_bytes_;',
             '        const int window = experimental_sparse_window_limit(warp);\n'
             '        constexpr std::size_t workspace_budget = 32ull*1024*1024;\n'
             '        const std::size_t fitting = std::min(available, workspace_budget) / lane_bytes_;'),
            (old_comment, new_comment),
            ('            static_cast<std::size_t>(std::max(warp, 0)), fitting}));',
             '            static_cast<std::size_t>(window), fitting}));'),
            ('        const auto lanes = static_cast<std::size_t>(capacity_);',
             '        const auto lanes = static_cast<std::size_t>(capacity_);\n'
             '        std::cout << "WINDOW_OWNER selected_window=" << window << " actual_capacity=" << capacity_\n'
             '                  << " native_capacity=" << std::min(capacity_, 32)\n'
             '                  << " workspace_bytes=" << lanes*lane_bytes_\n'
             '                  << " workspace_budget=" << workspace_budget << " device_warp=" << warp << \'\\n\';'),
        ]
    changed, applied = original, []
    for before, after in replacements:
        old, new = encode(before), encode(after)
        if changed.count(old) != 1:
            raise ValueError('ambiguous reviewed factory insertion')
        changed = changed.replace(old, new, 1)
        applied.append((old, new))
    restored = changed
    for old, new in reversed(applied):
        if restored.count(new) != 1:
            raise ValueError('non-reversible factory change')
        restored = restored.replace(new, old, 1)
    if restored != original:
        raise ValueError('change outside execution-only sites')
    return changed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    source, output = args.source.resolve(strict=True), args.output.resolve()
    if output.exists() or output.is_symlink():
        raise ValueError('preserve existing or partial factory preparation')
    changes = {name: transform((source/name).read_bytes(), name) for name in PINS}
    output.mkdir(parents=True)
    manifest = {}
    for name, data in changes.items():
        target = output/name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        manifest[name] = dict(original_sha256=PINS[name], bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
    record = dict(status='prepared_not_built_not_runtime_validated', files=manifest,
                  changed_files=2, production_registration_modified=False,
                  mathematical_bodies_modified=False, kernel_launch_shape_modified=False,
                  block_gather_scatter_modified=False, native_provider_budget_changed=False,
                  environment_selector='ARCH_NATIVE_WINDOW_CELLS', selector_values=[32, 64, 128],
                  workspace_budget_bytes=32*1024*1024, native_cohort_max=32,
                  nuclear_qualified=False, performance_qualified=False, release_qualified=False,
                  requires_fresh_cuda_factory_objects=True, requires_window_contracts_first=True)
    (output/'window-factory-overlay.json').write_text(json.dumps(record, indent=2)+'\n')
    print(json.dumps(record, indent=2))


if __name__ == '__main__':
    main()
