/**
 * @file CudaBackendExchange.h
 * @brief Runtime launch interface for CUDA ghost exchange.
 *
 * POD records lower shared same-level, coarse/fine and physical-boundary plans
 * to device addresses. Launches borrow plans, scratch, status and stream; the
 * runtime retains them and publishes valid ghosts only after completion.
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

struct DeviceBoundaryBatchBlock {
    DeviceStateView state;
    const DeviceBoundaryTransfer* transfers = nullptr;
    std::array<boundary::BoundaryPhase, 3> phases{};
};
static_assert(std::is_trivially_copyable_v<DeviceBoundaryBatchBlock>);

cudaError_t launch_cuda_backend_boundary_batch(
    const DeviceBoundaryBatchBlock* blocks, int block_count,
    const std::array<int, 3>& phase_counts, cudaStream_t stream, int& kernels);

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
    int slope_cells[6]{};
    double fine_position[3]{};
    double source_measures[8]{};
    double source_measure_sum = 0.0;
    std::uint8_t source_count = 0;
    std::uint8_t prolongation_dimension = 0;
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
    int field_count, double* scratch, int* status, cudaStream_t stream);

cudaError_t launch_cuda_backend_boundary_plan(
    DeviceStateView state, const DeviceBoundaryTransfer* device_transfers,
    const DeviceCompiledBoundaryPlan& compiled, cudaStream_t stream);

} // namespace arch::cuda
