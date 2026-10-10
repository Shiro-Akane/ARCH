/**
 * @file CoarseFineExchangeKernels.cuh
 * @brief Execute host-planned coarse/fine ghost transfers on CUDA.
 *
 * Gather evaluates the common conservative restriction or limited-linear
 * prolongation rules into borrowed scratch. A same-stream scatter writes
 * destinations only after a successful gather; runtime control checks the
 * completed status before publishing ghost validity.
 *
 * Native RZ ghost transfers use a separate launch: the ordinary plan keeps its
 * own ordering and rules, while the Native plan gathers every fine->coarse
 * family with the shared amr::regrid_math restriction leaf, lends that
 * immutable same-frame prefix to the coarse->fine readers, and only then
 * scatters. No reconstruction formula, EOS, floor or projection is defined
 * here; all mathematics stays in the shared Host/device leaves.
 */

#pragma once

#include "amr/transfer/ConservativeRestriction.h"
#include "amr/transfer/LimitedLinearProlongation.h"
#include "amr/transfer/NativeRzRegridTransfer.h"
#include "cuda/common/DeviceStateFields.cuh"
#include "cuda/hydro/GridGeometryAdapter.cuh"
#include "cuda/runtime/amr/CudaBackendExchange.h"
#include "numerics/state/RzNativeClosure.h"

#include <cuda_runtime.h>

#include <cmath>
#include <cstdint>
#include <limits>

namespace arch::cuda {

__device__ inline amr::prolongation_math::CompositionStencilView
prolong_device_stencil(
    DeviceStateView source, const DeviceCoarseFineTransfer& transfer,
    int species_count)
{
    return {source.rho, source.mass_fractions,
            static_cast<std::size_t>(source.total_size),
            transfer.source_cells[0], transfer.slope_cells,
            transfer.prolongation_dimension, species_count};
}

__global__ void gather_coarse_fine_exchange_kernel(
    const DeviceExchangeBlock* blocks,
    const DeviceCoarseFineTransfer* transfers, int field_count,
    std::uint64_t work_count, double* scratch, int* status)
{
    const std::uint64_t linear =
        static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (linear >= work_count) return;
    const int transfer_index = static_cast<int>(linear / field_count);
    const int field = static_cast<int>(linear % field_count);
    const DeviceCoarseFineTransfer& transfer = transfers[transfer_index];
    const DeviceStateView source = blocks[transfer.source_block].state;
    if (transfer.prolongation_dimension != 0) {
        const auto stencil = prolong_device_stencil(
            source, transfer, field_count - 6);
        if (field > 0 && field < 6) {
            scratch[linear] = amr::prolongation_math::reconstruct_field(
                stencil, device_state_field(source, field), transfer.fine_position);
            return;
        }
        const int closure_species =
            amr::prolongation_math::composition_closure_species(stencil);
        const auto family =
            amr::prolongation_math::classify_composition_family(stencil, closure_species);
        if (family == amr::prolongation_math::CompositionFamily::InvalidDensity) {
            atomicExch(status, static_cast<int>(family));
            return;
        }
        const double density = amr::prolongation_math::reconstruct_field(
            stencil, source.rho, transfer.fine_position);
        scratch[linear] = field == 0 ? density
            : amr::prolongation_math::reconstruct_mass_fraction(
                stencil, family, density, field - 6, transfer.fine_position, closure_species);
        return;
    }
    if (field < 6) {
        double integral = 0.0;
        for (int cell = 0; cell < transfer.source_count; ++cell)
            integral += amr::restriction_math::weighted_conserved_value(
                device_state_field(source, field)[transfer.source_cells[cell]],
                transfer.source_measures[cell]);
        scratch[linear] = amr::restriction_math::restricted_average(
            integral, transfer.source_measure_sum);
        return;
    }

    double density_integral = 0.0;
    double species_density_integral = 0.0;
    for (int cell = 0; cell < transfer.source_count; ++cell) {
        const int source_cell = transfer.source_cells[cell];
        density_integral +=
            amr::restriction_math::weighted_conserved_value(
                source.rho[source_cell], transfer.source_measures[cell]);
        species_density_integral +=
            amr::restriction_math::weighted_species_density(
                source.rho[source_cell],
                device_state_field(source, field)[source_cell], transfer.source_measures[cell]);
    }
    scratch[linear] = amr::restriction_math::restricted_mass_fraction(
        species_density_integral, density_integral);
}

__global__ void scatter_coarse_fine_exchange_kernel(
    const DeviceExchangeBlock* blocks,
    const DeviceCoarseFineTransfer* transfers, int field_count,
    std::uint64_t work_count, const double* scratch, const int* status)
{
    // The preceding gather is complete on this stream.  Reject the entire
    // plan before any destination write, just as the Host gather/throw path.
    if (*status != 0) return;
    const std::uint64_t linear =
        static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (linear >= work_count) return;
    const int transfer_index = static_cast<int>(linear / field_count);
    const int field = static_cast<int>(linear % field_count);
    const DeviceCoarseFineTransfer& transfer = transfers[transfer_index];
    const DeviceStateView destination =
        blocks[transfer.destination_block].state;
    device_state_field(destination, field)[transfer.destination_cell] =
        scratch[linear];
}

inline cudaError_t launch_coarse_fine_exchange(
    const DeviceExchangeBlock* blocks,
    const DeviceCoarseFineTransfer* transfers, int transfer_count,
    int field_count, double* scratch, int* status, cudaStream_t stream)
{
    if (transfer_count == 0) return cudaSuccess;
    if (blocks == nullptr || transfers == nullptr || scratch == nullptr
        || status == nullptr
        || transfer_count < 0 || field_count < 6)
        return cudaErrorInvalidValue;
    const std::uint64_t work = static_cast<std::uint64_t>(transfer_count)
        * static_cast<std::uint64_t>(field_count);
    constexpr int threads = 256;
    const std::uint64_t block_count = (work + threads - 1) / threads;
    if (work == 0
        || block_count > static_cast<std::uint64_t>(
            std::numeric_limits<unsigned int>::max()))
        return cudaErrorInvalidValue;
    cudaError_t result = cudaMemsetAsync(status, 0, sizeof(int), stream);
    if (result != cudaSuccess) return result;
    gather_coarse_fine_exchange_kernel
        <<<static_cast<unsigned int>(block_count), threads, 0, stream>>>(
            blocks, transfers, field_count, work, scratch, status);
    result = cudaGetLastError();
    if (result != cudaSuccess) return result;
    scatter_coarse_fine_exchange_kernel
        <<<static_cast<unsigned int>(block_count), threads, 0, stream>>>(
            blocks, transfers, field_count, work, scratch, status);
    return cudaGetLastError();
}

// ---------------------------------------------------------------------------
// Native axisymmetric-RZ ghost branch.
//
// Workflow mirrors the Host Native gather/scatter: every fine->coarse family is
// restricted first into reusable scratch, the immutable restriction prefix is
// then lent to the coarse->fine readers exactly like the Host overlay map, and
// the existing scatter publishes destinations only after both gathers. The
// restriction/prolongation formulas, the density/inertia closure, the species
// simplex correction and the V/W measures are owned by the shared leaves
// (amr::regrid_math::restrict_family, amr::regrid_math::prolong_native_family,
// GridMetrics); this adapter only binds actual device metadata to them.
// ---------------------------------------------------------------------------

/** Workflow: borrow the immutable species-major device state as shared samples.
 * Field order and species stride follow the existing shared ConstStateView. */
__device__ inline amr::regrid_math::ConstStateView native_state_samples(
    DeviceStateView state)
{
    return {{state.rho, state.mom_u, state.mom_v, state.mom_w,
             state.eng, state.enuc_rate}, state.mass_fractions,
            static_cast<std::size_t>(state.total_size)};
}

/** Workflow: recover one actual padded cell coordinate triple from a lowered
 * flat view index; the shared layout convention (i + stride_y*j) owns the
 * mapping, so no alternative coordinate is inferred here. */
__device__ inline void native_decode_cell(
    const DeviceGridView& grid, int flat, int& i, int& j, int& k)
{
    const int stride_y = grid.stride_y > 0 ? grid.stride_y : 1;
    const int rows = grid.total_y > 0 ? grid.total_y : 1;
    const int stride_z = grid.stride_z > 0 ? grid.stride_z : stride_y * rows;
    i = flat % stride_y;
    j = (flat / stride_y) % rows;
    k = flat / stride_z;
}

/** Workflow: bind one actual fine family and its real coarse destination to the
 * shared restriction geometry; V/W measures come from the shared GridMetrics
 * leaves exactly as the Host adapter computes them. */
__device__ inline void native_restriction_geometry(
    const DeviceCoarseFineTransfer& transfer, const DeviceGridView& source_grid,
    const DeviceGridView& destination_grid,
    amr::regrid_math::RestrictionGeometry& geometry)
{
    const auto source_geometry = make_grid_geometry_view(source_grid);
    const auto destination_geometry = make_grid_geometry_view(destination_grid);
    int i = 0;
    int j = 0;
    int k = 0;
    native_decode_cell(destination_grid, transfer.destination_cell, i, j, k);
    geometry.count = transfer.source_count;
    geometry.angular_momentum = true;
    geometry.coarse_volume =
        GridMetrics::CellVolume(destination_geometry, i, j, k);
    geometry.coarse_angular_measure =
        GridMetrics::Rz::AngularMomentumMeasure(destination_geometry, i, j);
    for (int cell = 0; cell < transfer.source_count; ++cell) {
        native_decode_cell(source_grid, transfer.source_cells[cell], i, j, k);
        geometry.source_cells[cell] = transfer.source_cells[cell];
        geometry.volumes[cell] = transfer.source_measures[cell];
        geometry.angular_measures[cell] =
            GridMetrics::Rz::AngularMomentumMeasure(source_geometry, i, j);
    }
}

/** Workflow: bind the same real source/destination geometry fields the Host
 * NativeRzProlongationContext receives; the selected 2x2 member, its four real
 * face intervals and the destination layout all come from actual metadata and
 * the shared GridMetrics leaves. */
__device__ inline void native_prolongation_context(
    const DeviceCoarseFineTransfer& transfer, const DeviceGridView& source_grid,
    const DeviceGridView& destination_grid,
    amr::regrid_math::NativeRzProlongationContext& context, int& child)
{
    const auto source_geometry = make_grid_geometry_view(source_grid);
    const auto destination_geometry = make_grid_geometry_view(destination_grid);
    int i = 0;
    int j = 0;
    int k = 0;
    native_decode_cell(source_grid, transfer.source_cells[0], i, j, k);
    context.source_geometry = source_geometry;
    context.logical_nx = source_grid.total_x;
    context.logical_ny = source_grid.total_y;
    context.radial_i = i;
    context.axial_j = j;
    child = (transfer.fine_position[0] > 0.0 ? 1 : 0)
        | (transfer.fine_position[1] > 0.0 ? 2 : 0);
    native_decode_cell(destination_grid, transfer.destination_cell, i, j, k);
    const int fine_i = i - (child & 1);
    const int fine_j = j - ((child >> 1) & 1);
    context.has_destination_geometry = true;
    context.destination_geometry = destination_geometry;
    context.destination_nx = destination_grid.total_x;
    context.destination_ny = destination_grid.total_y;
    context.fine_i = fine_i;
    context.fine_j = fine_j;
    for (int member = 0; member < 4; ++member) {
        const int ci = fine_i + (member & 1);
        const int cj = fine_j + ((member >> 1) & 1);
        context.children[member] = {destination_geometry.GetFacePosL(ci),
            destination_geometry.GetFacePosR(ci),
            destination_geometry.GetAxialFacePosL(cj),
            destination_geometry.GetAxialFacePosR(cj)};
    }
}

/** Workflow: locate one immutable same-frame restriction candidate inside the
 * already sorted restriction prefix. Bounded binary search keeps the lookup
 * O(log N); no full scan and no dense state copy is used. */
__device__ inline int native_overlay_slot(
    const DeviceCoarseFineTransfer* transfers, int restriction_count,
    int source_block, int source_cell)
{
    int lower = 0;
    int upper = restriction_count - 1;
    while (lower <= upper) {
        const int middle = lower + (upper - lower) / 2;
        const DeviceCoarseFineTransfer& candidate = transfers[middle];
        if (candidate.destination_block == source_block
            && candidate.destination_cell == source_cell)
            return middle;
        if (candidate.destination_block < source_block
            || (candidate.destination_block == source_block
                && candidate.destination_cell < source_cell))
            lower = middle + 1;
        else
            upper = middle - 1;
    }
    return -1;
}

/** Workflow: apply the same borrowed provisional native/ENUC gate the Host
 * gather applies after each candidate. Thermal closure, EOS, floors and
 * projections stay with the later completed-domain owner. */
__device__ inline amr::regrid_math::Status native_provisional_status(
    const double* values, int species, const state::Bounds& bounds,
    amr::regrid_math::Status enuc_status)
{
    if (!std::isfinite(values[5])) return enuc_status;
    const FluidVector native{values[0], values[1], values[2], values[3],
                             values[4]};
    if (RzThermodynamics::provisional_native_state(native,
            species > 0 ? values + 6 : nullptr, species, 1, bounds)
        != arch::state::Status::valid)
        return amr::regrid_math::Status::NativeDestination;
    return amr::regrid_math::Status::Ok;
}

/** Native restriction gather: one borrowed thread per fine->coarse transfer.
 * Workflow: bind the actual fine family, invoke the shared restriction leaf,
 * precheck the provisional native/ENUC gate and publish the candidate only to
 * scratch. The state is never written before the completed scatter. */
__global__ void native_restriction_gather_kernel(
    const DeviceExchangeBlock* blocks,
    const DeviceCoarseFineTransfer* transfers, int restriction_count,
    int field_count, state::Bounds bounds, double* scratch,
    double* workspace, int* status)
{
    const int index = static_cast<int>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (index >= restriction_count) return;
    const DeviceCoarseFineTransfer& transfer = transfers[index];
    amr::regrid_math::RestrictionGeometry geometry{};
    native_restriction_geometry(transfer,
        blocks[transfer.source_block].grid,
        blocks[transfer.destination_block].grid, geometry);
    const int species = field_count - 6;
    double* per_transfer = species > 0
        ? workspace + static_cast<std::size_t>(index)
            * amr::regrid_math::prolongation_workspace_per_species
            * static_cast<std::size_t>(species)
        : nullptr;
    amr::regrid_math::RestrictionResult result{};
    const auto outcome = amr::regrid_math::restrict_family(
        native_state_samples(blocks[transfer.source_block].state),
        geometry, species, bounds.density, bounds.internal_min, per_transfer,
        result);
    if (outcome != amr::regrid_math::Status::Ok) {
        atomicCAS(status, 0, static_cast<int>(outcome));
        return;
    }
    double* values = scratch + static_cast<std::size_t>(index) * field_count;
    values[0] = result.fluid.rho;
    values[1] = result.fluid.mom_u;
    values[2] = result.fluid.mom_v;
    values[3] = result.fluid.mom_w;
    values[4] = result.fluid.eng;
    values[5] = result.enuc;
    for (int sp = 0; sp < species; ++sp) values[6 + sp] = result.fractions[sp];
    const auto provisional = native_provisional_status(values, species, bounds,
        amr::regrid_math::Status::RestrictionEnuc);
    if (provisional != amr::regrid_math::Status::Ok)
        atomicCAS(status, 0, static_cast<int>(provisional));
}

/** Native prolongation gather: one borrowed thread per coarse->fine transfer.
 * Workflow: borrow the resident coarse frame plus the immutable restriction
 * prefix through the Host-equivalent readers, invoke the shared native
 * prolongation leaf and precheck the provisional native/ENUC gate. The
 * selected member's rhoX/rho fraction and signed ENUC mean are published to
 * scratch only; no new interpolation or coordinate choice is defined. */
__global__ void native_prolongation_gather_kernel(
    const DeviceExchangeBlock* blocks,
    const DeviceCoarseFineTransfer* transfers, int restriction_count,
    int transfer_count, int field_count, state::Bounds bounds,
    double* scratch, double* workspace, int* status)
{
    // The completed restriction gather runs earlier on the same stream. Veto
    // the whole plan before touching the borrowed overlay, so a recorded
    // restriction error can never publish reads of unfinished scratch.
    if (atomicAdd(status, 0) != 0) return;
    const int index = restriction_count
        + static_cast<int>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (index >= transfer_count) return;
    const DeviceCoarseFineTransfer& transfer = transfers[index];
    const DeviceStateView source = blocks[transfer.source_block].state;
    const int source_block = transfer.source_block;
    amr::regrid_math::NativeRzProlongationContext context{};
    int child = 0;
    native_prolongation_context(transfer, blocks[source_block].grid,
        blocks[transfer.destination_block].grid, context, child);
    /** Read the same immutable restriction candidate overlay before the live
     * seed data, exactly like the Host sparse overlay map. */
    const auto read = [&](int cell) {
        const int slot =
            native_overlay_slot(transfers, restriction_count, source_block, cell);
        if (slot >= 0) {
            const double* candidate =
                scratch + static_cast<std::size_t>(slot) * field_count;
            return FluidVector{candidate[0], candidate[1], candidate[2],
                               candidate[3], candidate[4]};
        }
        return source.load(cell);
    };
    const auto enuc = [&](int cell) {
        const int slot =
            native_overlay_slot(transfers, restriction_count, source_block, cell);
        return slot >= 0
            ? scratch[static_cast<std::size_t>(slot) * field_count + 5]
            : source.enuc_rate[cell];
    };
    const auto fraction = [&](int sp, int cell) {
        const int slot =
            native_overlay_slot(transfers, restriction_count, source_block, cell);
        return slot >= 0
            ? scratch[static_cast<std::size_t>(slot) * field_count + 6 + sp]
            : source.species(sp, cell);
    };
    const int species = field_count - 6;
    double* per_transfer = species > 0
        ? workspace + static_cast<std::size_t>(index)
            * amr::regrid_math::prolongation_workspace_per_species
            * static_cast<std::size_t>(species)
        : nullptr;
    amr::regrid_math::ProlongationResult result{};
    const auto outcome = amr::regrid_math::prolong_native_family(context, read,
        enuc, fraction, species, bounds, per_transfer, result);
    if (outcome != amr::regrid_math::Status::Ok) {
        atomicCAS(status, 0, static_cast<int>(outcome));
        return;
    }
    const FluidVector& fluid = result.fluid[child];
    double* values = scratch + static_cast<std::size_t>(index) * field_count;
    values[0] = fluid.rho;
    values[1] = fluid.mom_u;
    values[2] = fluid.mom_v;
    values[3] = fluid.mom_w;
    values[4] = fluid.eng;
    values[5] = result.enuc[child];
    for (int sp = 0; sp < species; ++sp)
        values[6 + sp] = result.rhoX[
            static_cast<std::size_t>(sp) * amr::regrid_math::maximum_children
                + child] / fluid.rho;
    const auto provisional = native_provisional_status(values, species, bounds,
        amr::regrid_math::Status::ProlongationEnuc);
    if (provisional != amr::regrid_math::Status::Ok)
        atomicCAS(status, 0, static_cast<int>(provisional));
}

/** Queue the Native RZ ghost branch on the caller's stream.
 * Workflow: validate the borrowed extents, clear the shared status, run the
 * completed restriction gather, run the completed prolongation gather and then
 * reuse the existing scatter. Two gathers always precede every destination
 * write and a non-zero status vetoes the whole scatter. */
inline cudaError_t launch_coarse_fine_exchange_native(
    const DeviceExchangeBlock* blocks,
    const DeviceCoarseFineTransfer* transfers, int transfer_count,
    int restriction_count, int field_count, double* scratch,
    state::Bounds bounds, int* status, cudaStream_t stream)
{
    if (transfer_count == 0) return cudaSuccess;
    if (blocks == nullptr || transfers == nullptr || scratch == nullptr
        || status == nullptr || transfer_count < 0 || field_count < 6
        || restriction_count < 0 || restriction_count > transfer_count)
        return cudaErrorInvalidValue;
    const int species = field_count - 6;
    const std::uint64_t gathered = static_cast<std::uint64_t>(transfer_count)
        * static_cast<std::uint64_t>(field_count);
    const std::uint64_t workspace_width = static_cast<std::uint64_t>(
        amr::regrid_math::prolongation_workspace_per_species)
        * static_cast<std::uint64_t>(species);
    if (workspace_width != 0
        && static_cast<std::uint64_t>(transfer_count)
            > (std::numeric_limits<std::uint64_t>::max() - gathered)
                / workspace_width)
        return cudaErrorInvalidValue;
    constexpr int threads = 256;
    const std::uint64_t scatter_blocks = (gathered + threads - 1) / threads;
    const std::uint64_t restriction_blocks = (
        static_cast<std::uint64_t>(restriction_count) + threads - 1) / threads;
    const std::uint64_t prolongation_blocks = (
        static_cast<std::uint64_t>(transfer_count - restriction_count)
            + threads - 1) / threads;
    if (gathered == 0 || scatter_blocks > static_cast<std::uint64_t>(
            std::numeric_limits<unsigned int>::max())
        || restriction_blocks > static_cast<std::uint64_t>(
            std::numeric_limits<unsigned int>::max())
        || prolongation_blocks > static_cast<std::uint64_t>(
            std::numeric_limits<unsigned int>::max()))
        return cudaErrorInvalidValue;
    const auto gathered_blocks = static_cast<unsigned int>(scatter_blocks);
    const auto restriction_grid = static_cast<unsigned int>(
        restriction_blocks == 0 ? 1 : restriction_blocks);
    const auto prolongation_grid = static_cast<unsigned int>(
        prolongation_blocks == 0 ? 1 : prolongation_blocks);
    double* workspace = scratch + gathered;
    cudaError_t result = cudaMemsetAsync(status, 0, sizeof(int), stream);
    if (result != cudaSuccess) return result;
    native_restriction_gather_kernel<<<restriction_grid, threads, 0, stream>>>(
        blocks, transfers, restriction_count, field_count, bounds, scratch,
        workspace, status);
    result = cudaGetLastError();
    if (result != cudaSuccess) return result;
    native_prolongation_gather_kernel<<<prolongation_grid, threads, 0, stream>>>(
        blocks, transfers, restriction_count, transfer_count, field_count,
        bounds, scratch, workspace, status);
    result = cudaGetLastError();
    if (result != cudaSuccess) return result;
    scatter_coarse_fine_exchange_kernel<<<gathered_blocks, threads, 0, stream>>>(
        blocks, transfers, field_count, gathered, scratch, status);
    return cudaGetLastError();
}

} // namespace arch::cuda
