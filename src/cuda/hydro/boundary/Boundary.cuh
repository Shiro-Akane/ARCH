/**
 * @file Boundary.cuh
 * @brief Execute a compiled physical-boundary plan on device fields.
 *
 * Transfers and component signs come from the shared logical boundary plan.
 * Phases are enqueued in order on the supplied stream so later phases can use
 * completed earlier-phase values. The runtime owns transfer/state storage and
 * fences completion before declaring the ghost region readable.
 * Workflow:
 * 1. Receive device-resident block state and boundary plans.
 * 2. Launch CUDA ghost and boundary transport for the active stage.
 * 3. Leave core hydro mathematics in the shared host/device policies.
 * 4. For a positive-r native reflector, evaluate the common point law into
 *    disjoint candidate buffers. Immutable axis layers, final signed axis
 *    corners, exchange and completed EOS publication remain Runtime-owned.
 */

#pragma once

#include <cmath>
#include <cstddef>
#include <limits>

#include <cuda_runtime.h>

#include "cuda/hydro/GridGeometryAdapter.cuh"
#include "cuda/hydro/boundary/BoundaryPlan.h"
#include "cuda/hydro/policies/CheckedHydroEos.cuh"
#include "physics/boundary/NativeRzBoundaryMath.h"

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


// Header-private launch implementation; each including TU owns its own stub.
static __global__ void boundary_phase_kernel(
    DeviceStateView state, const DeviceBoundaryTransfer* transfers,
    int count)
{
    boundary_phase_kernel_work(state, transfers, count);
}

/** Evaluate one immutable native reflecting layer without scattering ghosts.
 * Each lane borrows 10*N contiguous scratch doubles and N provisional Xi;
 * outputs belong to the caller's candidate buffer, never the input fields.
 * The shared leaf owns the original point EOS, mirror and V/W integrals.
 * The first failure remains sticky; sibling lanes still validate their own
 * required EOS queries. Readonly prefix entries override actual resident
 * ghosts, including the inherited ENUC; siblings never read candidate outputs.
 * Runtime joins all layers before any publication.
 */
template<class Eos>
__global__ void native_reflecting_candidates_kernel(
    DeviceStateView input, DeviceGridView grid,
    const boundary::native_rz_math::Request* requests, int count,
    state::Bounds bounds, Eos eos, SpeciesWorkspaceView workspace,
    FluidVector* conserved, double* fractions, int* failed,
    NativeBoundaryPrefixView prefix, double* enuc)
{
    namespace math = boundary::native_rz_math;
    __shared__ int required_status[kSpeciesKernelThreads];
    required_status[threadIdx.x] = 0;
    const int lane = blockIdx.x * blockDim.x + threadIdx.x;
    const int lanes = blockDim.x * gridDim.x;
    const auto checked = make_checked_hydro_eos(eos, required_status + threadIdx.x);
    const math::Context context{make_grid_geometry_view(grid),
        grid.total_x, grid.total_y, grid.is, grid.ie, grid.js, grid.je};
    const auto read = [input, prefix](int index) { return prefix.read(input, index); };
    const auto fraction = [input, prefix](int species, int index) {
        return prefix.fraction(input, species, index);
    };
    // This buffer is borrowed for this launch only. Contiguous lane rows let
    // the unchanged common leaf reuse its original source/support/sample Xi
    // layout; other kernel workspace layouts retain their existing ownership.
    const std::size_t width = static_cast<std::size_t>(input.n_species);
    double* scratch = width ? workspace.values + lane * (11 * width) : nullptr;
    double* mean_x = width ? scratch + 10 * width : nullptr;
    const math::Workspace local{scratch, 10 * width, mean_x, width};
    for (int ordinal = lane; ordinal < count; ordinal += lanes) {
        const auto result = math::reflect_cell(context, requests[ordinal], bounds,
            input.n_species, checked, read, fraction, local);
        if (!result.valid() || checked.required_query_failed()) {
            const auto status = result.valid() ? math::Status::invalid_point_eos : result.status;
            atomicCAS(failed, 0, static_cast<int>(status));
            break;
        }
        if (enuc) {
            const int source = grid.index(requests[ordinal].source[0], requests[ordinal].source[1]);
            const double inherited_enuc = prefix.enuc_value(input, source);
            if (!std::isfinite(inherited_enuc)) {
                atomicCAS(failed, 0, static_cast<int>(math::Status::invalid_source_state));
                break;
            }
            enuc[ordinal] = inherited_enuc;
        }
        conserved[ordinal] = result.conserved;
        for (int s = 0; s < input.n_species; ++s)
            fractions[static_cast<std::size_t>(ordinal) * width + s] = mean_x[s];
    }
}
} // namespace detail

/** Launch the common positive-r reflector into a disjoint provisional layer.
 * Actual immutable input, request and output allocations remain caller-owned.
 * N>0 requires explicit 11*N per-lane workspace with no species-count ceiling;
 * N=0 requires none. A reused failure word is never reset here. This operation
 * grants neither completed ghosts nor a Native Device Runtime capability.
 */
template<class Eos>
inline cudaError_t launch_native_reflecting_candidates(
    DeviceStateView input, DeviceGridView grid,
    const boundary::native_rz_math::Request* requests, int count,
    state::Bounds bounds, Eos eos, SpeciesWorkspaceView workspace,
    FluidVector* conserved, double* fractions, int* failed, cudaStream_t stream,
    NativeBoundaryPrefixView prefix = {}, double* enuc = nullptr)
{
    if (!valid_hydro_view(input) || !valid_hydro_grid(grid)
        || grid.semantics != GridMetrics::GeometrySemantics::AxisymmetricRz
        || input.total_size != grid.total_size || count < 1 || !requests
        || !conserved || !failed || !state::valid_bounds(bounds)
        || (input.n_species > 0 && (!fractions || !workspace.values))
        || !valid_species_workspace(workspace, input.n_species, 11)
        || prefix.count < 0
        || (prefix.count > 0 && (!prefix.indices || !prefix.conserved || !prefix.enuc
            || prefix.species != input.n_species
            || (input.n_species > 0 && !prefix.fractions))))
        return cudaErrorInvalidValue;
    const int threads = detail::species_launch_threads(workspace);
    const int blocks = detail::species_launch_blocks(count, workspace);
    detail::native_reflecting_candidates_kernel<<<blocks, threads, 0, stream>>>(
        input, grid, requests, count, bounds, eos, workspace, conserved, fractions, failed,
        prefix, enuc);
    return cudaPeekAtLastError();
}

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
