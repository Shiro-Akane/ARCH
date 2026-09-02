#include "CudaBackendExchange.h"

#include "cuda/amr/CoarseFineExchangeKernels.cuh"
#include "cuda/hydro/Boundary.cuh"
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

cudaError_t launch_cuda_backend_coarse_fine_exchange(
    const DeviceExchangeBlock* blocks,
    const DeviceCoarseFineTransfer* transfers, int transfer_count,
    int field_count, double* scratch, cudaStream_t stream)
{
    return launch_coarse_fine_exchange(
        blocks, transfers, transfer_count, field_count, scratch, stream);
}

cudaError_t launch_cuda_backend_boundary_plan(
    DeviceStateView state, const DeviceBoundaryTransfer* device_transfers,
    const DeviceCompiledBoundaryPlan& compiled, cudaStream_t stream)
{
    return launch_boundary_plan(
        state, device_transfers, compiled, stream);
}

} // namespace arch::cuda
