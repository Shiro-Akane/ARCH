/**
 * @file CudaBackendExchange.cu
 * @brief Forward runtime exchange plans to device boundary and ghost kernels.
 *
 * Borrowed blocks, operations, scratch and status use the caller's stream.
 * The runtime retains their lifetime and completion responsibility; logical
 * plans and transfer mathematics remain shared with host execution.
 */

#include "cuda/runtime/amr/CudaBackendExchange.h"

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
    int field_count, double* scratch, int* status, cudaStream_t stream)
{
    return launch_coarse_fine_exchange(
        blocks, transfers, transfer_count, field_count, scratch, status, stream);
}

cudaError_t launch_cuda_backend_boundary_plan(
    DeviceStateView state, const DeviceBoundaryTransfer* device_transfers,
    const DeviceCompiledBoundaryPlan& compiled, cudaStream_t stream)
{
    return launch_boundary_plan(
        state, device_transfers, compiled, stream);
}

} // namespace arch::cuda
