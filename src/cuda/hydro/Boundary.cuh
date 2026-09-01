#pragma once

#include "BoundaryPlan.h"

#include <cuda_runtime.h>

#include <cstddef>
#include <limits>

namespace arch::cuda
{
namespace detail
{
__global__ void boundary_phase_kernel(
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
    for (int species = 0; species < state.n_species; ++species) {
        state.set_species(
            species, destination,
            boundary_signed_copy(
                state.species(species, source), transfer.species_sign));
    }
}
} // namespace detail

inline cudaError_t launch_boundary_plan(
    DeviceStateView state, const DeviceBoundaryTransfer* device_transfers,
    const DeviceCompiledBoundaryPlan& compiled, cudaStream_t stream)
{
    if (!valid_hydro_view(state)
        || state.total_size != compiled.total_size
        || compiled.transfers.empty()
        || device_transfers == nullptr
        || compiled.transfers.size()
            > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return cudaErrorInvalidValue;

    std::size_t expected_first = 0;
    for (std::size_t phase_index = 0;
         phase_index < compiled.phases.size(); ++phase_index) {
        const auto& phase = compiled.phases[phase_index];
        if (phase.id != static_cast<boundary::BoundaryPhaseId>(phase_index)
            || phase.first != expected_first
            || phase.first > compiled.transfers.size()
            || phase.count > compiled.transfers.size() - phase.first)
            return cudaErrorInvalidValue;
        expected_first += phase.count;
    }
    if (expected_first != compiled.transfers.size())
        return cudaErrorInvalidValue;
    for (std::size_t index = 0; index < compiled.transfers.size(); ++index) {
        const auto& transfer = compiled.transfers[index];
        const auto valid_sign = [](std::int8_t sign) {
            return sign == -1 || sign == 1;
        };
        bool signs_valid = valid_sign(transfer.species_sign);
        for (const auto sign : transfer.conserved_signs)
            signs_valid = signs_valid && valid_sign(sign);
        if (transfer.logical_ordinal != index
            || transfer.source_index < 0
            || transfer.source_index >= compiled.total_size
            || transfer.destination_index < 0
            || transfer.destination_index >= compiled.total_size
            || !signs_valid)
            return cudaErrorInvalidValue;
    }

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
