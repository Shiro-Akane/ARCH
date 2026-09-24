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
#include "cuda/hydro/boundary/Boundary.cuh"
#include "cuda/hydro/boundary/ExchangeKernels.cuh"
#include <algorithm>
#include <limits>

namespace arch::cuda {

namespace {
/** Apply one immutable physical donor stencil per singular-face ghost. */
__global__ void coordinate_seam_kernel(
    const DeviceExchangeBlock* blocks,
    const DeviceCoordinateSeamTransfer* transfers,
    int transfer_count, int* status)
{
    const int index = static_cast<int>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (index >= transfer_count) return;
    const auto& transfer = transfers[index];
    const DeviceStateView source = blocks[transfer.source_block].state;
    const DeviceStateView destination = blocks[transfer.destination_block].state;
    const amr::CoordinateSeamFields<const double> donor{
        source.rho, source.mom_u, source.mom_v, source.mom_w,
        source.eng, source.enuc_rate, source.mass_fractions,
        source.total_size, source.n_species};
    const amr::CoordinateSeamFields<double> ghost{
        destination.rho, destination.mom_u, destination.mom_v,
        destination.mom_w, destination.eng, destination.enuc_rate,
        destination.mass_fractions, destination.total_size,
        destination.n_species};
    if (!amr::apply_coordinate_seam_transfer(transfer.stencil, donor, ghost))
        atomicExch(status, 1);
}

__global__ void boundary_batch_phase_kernel(const DeviceBoundaryBatchBlock* blocks, int phase)
{
    const auto& b = blocks[blockIdx.y];
    const auto p = b.phases[phase];
    if (p.count == 0) return; // No pointer arithmetic on a block with no transfers.
    detail::boundary_phase_kernel_work(b.state, b.transfers + p.first, static_cast<int>(p.count));
}
}

cudaError_t launch_cuda_backend_boundary_batch(
    const DeviceBoundaryBatchBlock* blocks, int block_count,
    const std::array<int, 3>& phase_counts, cudaStream_t stream, int& kernels)
{
    kernels = 0;
    if (block_count < 0 || (block_count > 0 && !blocks)) return cudaErrorInvalidValue;
    for (const int count : phase_counts) if (count < 0) return cudaErrorInvalidValue;
    for (int first = 0; first < block_count;) {
        const int count = std::min(1024, block_count - first);
        for (int phase = 0; phase < 3; ++phase) {
            if (phase_counts[phase] == 0) continue;
            const dim3 grid(detail::hydro_launch_blocks(phase_counts[phase], 256), count);
            boundary_batch_phase_kernel<<<grid, 256, 0, stream>>>(blocks + first, phase);
            const auto status = cudaGetLastError();
            if (status != cudaSuccess) return status;
            ++kernels;
        }
        first += count;
    }
    return cudaSuccess;
}

/** Queue shared seam arithmetic after same-level/coarse-fine exchange. */
cudaError_t launch_cuda_backend_coordinate_seam(
    const DeviceExchangeBlock* blocks,
    const DeviceCoordinateSeamTransfer* transfers,
    int transfer_count, int* status, cudaStream_t stream)
{
    if (transfer_count == 0) return cudaSuccess;
    if (!blocks || !transfers || !status || transfer_count < 0)
        return cudaErrorInvalidValue;
    constexpr int threads = 128;
    const std::uint64_t count = (static_cast<std::uint64_t>(transfer_count)
        + threads - 1) / threads;
    if (count > std::numeric_limits<unsigned int>::max())
        return cudaErrorInvalidValue;
    cudaError_t result = cudaMemsetAsync(status, 0, sizeof(int), stream);
    if (result != cudaSuccess) return result;
    coordinate_seam_kernel<<<static_cast<unsigned int>(count), threads, 0, stream>>>(
        blocks, transfers, transfer_count, status);
    return cudaGetLastError();
}

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
