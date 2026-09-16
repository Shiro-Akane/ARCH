/** Test-only ABI: Host trajectory diagnostics borrow the production GPU owner.
 * The selected network is instantiated in the device binding TU, not here.
 */
#pragma once
#include "cuda/runtime/burn/CudaBackendBurnSparse.h"

namespace arch::cuda::testing {
std::unique_ptr<CudaSparseBurnOwner> make_generated_sparse_burn_owner(
    const dispatch::ResolvedExecutionPlan& plan, IdealGasView eos,
    int max_cells, cudaStream_t stream);
} // namespace arch::cuda::testing
