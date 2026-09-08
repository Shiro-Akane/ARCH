/** Selected-network instantiation only; numerical bodies stay in production. */
#include "GeneratedSparseBurnFactory.h"
#include "cuda/runtime/burn/CudaBackendBurnSparseImpl.cuh"
#include "physics/eos/IdealGas.h"

namespace arch::cuda::testing {
std::unique_ptr<CudaSparseBurnOwner> make_generated_sparse_burn_owner(
    const dispatch::ResolvedExecutionPlan& plan, IdealGasView eos,
    int max_cells, cudaStream_t stream)
{
    return make_sparse_burn_owner_for_network<ARCH_TEST_NETWORK_TYPE>(
        plan, eos, max_cells, stream);
}
} // namespace arch::cuda::testing
