/**
 * @file Boundary.cuh
 * @brief Execute a compiled physical-boundary plan on device fields.
 *
 * Transfers and component signs come from the shared logical boundary plan.
 * Phases are enqueued in order on the supplied stream so later phases can use
 * completed earlier-phase values. The runtime owns transfer/state storage and
 * fences completion before declaring the ghost region readable.
 */

#pragma once

#include "BoundaryPlan.h"

#include <cuda_runtime.h>

#include <cstddef>
#include <limits>

namespace arch::cuda
{
namespace detail
{
__device__ inline void boundary_phase_kernel_work(
    DeviceStateView state, const DeviceBoundaryTransfer* transfers,
    int count)
{
    const int offset = blockIdx.x * blockDim.x + threadIdx.x;
    if (offset >= count)
        return;
    const DeviceBoundaryTransfer transfer = transfers[offset];
    const int source = transfer.source_index;
    const int destination = transfer.destination_index;
    state.rho[destination] = boundary_signed_copy(
        state.rho[source], transfer.conserved_signs[0]);
    state.mom_u[destination] = boundary_signed_copy(
        state.mom_u[source], transfer.conserved_signs[1]);
    state.mom_v[destination] = boundary_signed_copy(
        state.mom_v[source], transfer.conserved_signs[2]);
    state.mom_w[destination] = boundary_signed_copy(
        state.mom_w[source], transfer.conserved_signs[3]);
    state.eng[destination] = boundary_signed_copy(
        state.eng[source], transfer.conserved_signs[4]);
    state.enuc_rate[destination] = boundary_signed_copy(
        state.enuc_rate[source], transfer.conserved_signs[5]);
    for (int species = 0; species < state.n_species; ++species) {
        state.set_species(
            species, destination,
            boundary_signed_copy(
                state.species(species, source), transfer.species_sign));
    }
}


__global__ void boundary_phase_kernel(
    DeviceStateView state, const DeviceBoundaryTransfer* transfers,
    int count)
{
    boundary_phase_kernel_work(state, transfers, count);
}
} // namespace detail

inline cudaError_t launch_boundary_plan(
    DeviceStateView state, const DeviceBoundaryTransfer* device_transfers,
    const DeviceCompiledBoundaryPlan& compiled, cudaStream_t stream)
{
    const auto validation = validate_boundary_launch(state, device_transfers, compiled);
    if (validation != cudaSuccess) return validation;

    constexpr int threads = 256;
    for (const auto& phase : compiled.phases) {
        if (phase.count == 0)
            continue;
        const int count = static_cast<int>(phase.count);
        detail::boundary_phase_kernel<<<
            detail::hydro_launch_blocks(count, threads), threads, 0, stream>>>(
            state, device_transfers + phase.first, count);
        const auto status = cudaPeekAtLastError();
        if (status != cudaSuccess)
            return status;
    }
    return cudaSuccess;
}
} // namespace arch::cuda
