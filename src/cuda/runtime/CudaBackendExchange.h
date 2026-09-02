/**
 * @file CudaBackendExchange.h
 * @brief Narrow runtime ABI for CUDA same-level and coarse/fine exchange.
 */

#pragma once

#include "cuda/common/CudaCommon.cuh"
#include "cuda/hydro/BoundaryPlan.h"

#include <cuda_runtime.h>

#include <cstdint>

namespace arch::cuda {

struct DeviceExchangeBlock {
    DeviceStateView state{};
    DeviceGridView grid{};
};

struct DeviceExchangeOperation {
    int source_block = 0;
    int destination_block = 0;
    int source_first[3]{};
    int destination_first[3]{};
    unsigned int extent[3]{};
    std::uint64_t scratch_first = 0;
};

struct DeviceCoarseFineTransfer {
    int source_block = 0;
    int destination_block = 0;
    int destination_cell = 0;
    int source_cells[8]{};
    std::uint8_t source_count = 0;
};

static_assert(std::is_standard_layout_v<DeviceCoarseFineTransfer>);
static_assert(std::is_trivially_copyable_v<DeviceCoarseFineTransfer>);

cudaError_t launch_cuda_backend_exchange_phase(
    const DeviceExchangeBlock* blocks,
    const DeviceExchangeOperation* operations, int operation_count,
    int field_count, std::uint64_t total_cells, double* scratch,
    cudaStream_t stream);

cudaError_t launch_cuda_backend_coarse_fine_exchange(
    const DeviceExchangeBlock* blocks,
    const DeviceCoarseFineTransfer* transfers, int transfer_count,
    int field_count, double* scratch, cudaStream_t stream);

cudaError_t launch_cuda_backend_boundary_plan(
    DeviceStateView state, const DeviceBoundaryTransfer* device_transfers,
    const DeviceCompiledBoundaryPlan& compiled, cudaStream_t stream);

} // namespace arch::cuda
