#include "CudaBackendExchange.h"

#include "cuda/hydro/ExchangeKernels.cuh"

namespace arch::cuda {

cudaError_t launch_cuda_backend_exchange_phase(
    const DeviceExchangeBlock* blocks,
    const DeviceExchangeOperation* operations, int operation_count,
    int field_count, std::uint64_t total_cells, double* scratch,
    cudaStream_t stream)
{
    return launch_same_level_exchange_phase(
        blocks, operations, operation_count, field_count, total_cells,
        scratch, stream);
}

} // namespace arch::cuda
