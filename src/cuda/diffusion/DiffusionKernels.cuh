/**
 * @file DiffusionKernels.cuh
 * @brief Device flux, stability and RKL-stage traversal for shared diffusion.
 *
 * DiffFlux and DiffusionAMRStages own transport, geometry and stage arithmetic.
 * Launch results describe queued kernels and their write extent; the runtime
 * owns the stream, workspaces, EOS status checks and logical-slot publication.
 * A successful interior update does not make the surrounding ghosts valid.
 */

#pragma once

#include <cuda_runtime.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "../common/CudaCommon.cuh"
#include "../common/DeviceEosStatus.h"
#include "../hydro/HydroStateKernels.cuh"
#include "../hydro/GridGeometryAdapter.cuh"
#include "cuda/runtime/amr/CudaBackendAmrFlux.h"
#include "../../numerics/diffusion/DiffFlux.h"
#include "../../numerics/diffusion/DiffusionAMRStages.h"
#include "../../physics/species/Species.h"

namespace arch::cuda
{

enum class DiffusionWriteEffect : int
{
    None = 0,
    InteriorWritten = 1,
    CompleteStateCopied = 2,
};

struct DiffusionLaunchResult
{
    cudaError_t error = cudaSuccess;
    int kernels_launched = 0;
    DiffusionWriteEffect effect = DiffusionWriteEffect::None;
    double resolved_dt = DiffFlux::diffusion_dt_sentinel();
};

struct DiffusionWorkspaceView
{
    DeviceStateView face_flux;
    double* dt_candidates = nullptr;
    double* dt_result = nullptr;
    int* status = nullptr;
    SpeciesWorkspaceView species_workspace{};
};

static_assert(std::is_standard_layout_v<DiffusionLaunchResult>);
static_assert(std::is_trivially_copyable_v<DiffusionLaunchResult>);
static_assert(std::is_standard_layout_v<DiffusionWorkspaceView>);
static_assert(std::is_trivially_copyable_v<DiffusionWorkspaceView>);

namespace detail
{

inline bool same_state_storage(
    const DeviceStateView& left, const DeviceStateView& right)
{
    return left.rho == right.rho
        && left.mom_u == right.mom_u
        && left.mom_v == right.mom_v
        && left.mom_w == right.mom_w
        && left.eng == right.eng
        && left.enuc_rate == right.enuc_rate
        && left.mass_fractions == right.mass_fractions;
}

inline bool any_state_storage_alias(
    const DeviceStateView& writable, const DeviceStateView& readable)
{
    struct StorageRange
    {
        const double* pointer;
        std::size_t count;
    };
    const StorageRange writable_fields[] = {
        {writable.rho, static_cast<std::size_t>(writable.total_size)},
        {writable.mom_u, static_cast<std::size_t>(writable.total_size)},
        {writable.mom_v, static_cast<std::size_t>(writable.total_size)},
        {writable.mom_w, static_cast<std::size_t>(writable.total_size)},
        {writable.eng, static_cast<std::size_t>(writable.total_size)},
        {writable.enuc_rate, static_cast<std::size_t>(writable.total_size)},
        {writable.mass_fractions,
         static_cast<std::size_t>(writable.total_size)
             * static_cast<std::size_t>(writable.n_species)}};
    const StorageRange readable_fields[] = {
        {readable.rho, static_cast<std::size_t>(readable.total_size)},
        {readable.mom_u, static_cast<std::size_t>(readable.total_size)},
        {readable.mom_v, static_cast<std::size_t>(readable.total_size)},
        {readable.mom_w, static_cast<std::size_t>(readable.total_size)},
        {readable.eng, static_cast<std::size_t>(readable.total_size)},
        {readable.enuc_rate, static_cast<std::size_t>(readable.total_size)},
        {readable.mass_fractions,
         static_cast<std::size_t>(readable.total_size)
             * static_cast<std::size_t>(readable.n_species)}};
    for (const StorageRange& destination : writable_fields) {
        if (destination.pointer == nullptr || destination.count == 0) continue;
        const std::uintptr_t destination_begin =
            reinterpret_cast<std::uintptr_t>(destination.pointer);
        const std::uintptr_t destination_end = destination_begin
            + destination.count * sizeof(double);
        for (const StorageRange& source : readable_fields) {
            if (source.pointer == nullptr || source.count == 0) continue;
            const std::uintptr_t source_begin =
                reinterpret_cast<std::uintptr_t>(source.pointer);
            const std::uintptr_t source_end =
                source_begin + source.count * sizeof(double);
            if (destination_end < destination_begin
                || source_end < source_begin
                || (destination_begin < source_end
                    && source_begin < destination_end))
                return true;
        }
    }
    return false;
}

inline bool matching_state_shape(
    const DeviceStateView& left, const DeviceStateView& right)
{
    return left.total_size == right.total_size
        && left.n_species == right.n_species;
}

inline bool valid_diffusion_grid(const DeviceGridView& grid)
{
    if (!valid_hydro_grid(grid)
        || make_grid_geometry_view(grid).geometry == GridMetrics::Geometry::Unsupported)
        return false;
    if (grid.is < 1 || grid.ie >= grid.total_x) return false;
    if (grid.dim >= 2 && (grid.js < 1 || grid.je >= grid.total_y)) return false;
    if (grid.dim == 3 && (grid.ks < 1 || grid.ke >= grid.total_z)) return false;
    return grid.dx1 > 0.0
        && (grid.dim < 2 || grid.dx2 > 0.0)
        && (grid.dim < 3 || grid.dx3 > 0.0);
}

inline cudaError_t write_dt_sentinel(double* result, cudaStream_t stream)
{
    if (result == nullptr) return cudaErrorInvalidValue;
    static const double sentinel = DiffFlux::diffusion_dt_sentinel();
    return cudaMemcpyAsync(result, &sentinel, sizeof(double),
                           cudaMemcpyHostToDevice, stream);
}

ARCH_INLINE int diffusion_face_count(
    const DeviceGridView& grid, int direction)
{
    const int ni = grid.ie - grid.is + (direction == 0 ? 1 : 0);
    const int nj = grid.je - grid.js + (direction == 1 ? 1 : 0);
    const int nk = grid.ke - grid.ks + (direction == 2 ? 1 : 0);
    return ni * nj * nk;
}

ARCH_INLINE int diffusion_face_cell(
    const DeviceGridView& grid, int direction, int linear)
{
    const int ni = grid.ie - grid.is + (direction == 0 ? 1 : 0);
    const int nj = grid.je - grid.js + (direction == 1 ? 1 : 0);
    const int i = grid.is + linear % ni;
    linear /= ni;
    const int j = grid.js + linear % nj;
    const int k = grid.ks + linear / nj;
    return grid.index(i, j, k);
}

template <typename EosView>
__global__ void diffusion_face_kernel(
    DeviceStateView state, DeviceStateView flux, DeviceGridView grid,
    EosView eos, SpeciesPODView species,
    DiffFlux::DiffusionConfigView config, int direction, int* status,
    SpeciesWorkspaceView workspace = {})
{
    const int lane = blockIdx.x * blockDim.x + threadIdx.x;
    const int count = diffusion_face_count(grid, direction);
    SpeciesLaneScratch<5> scratch(workspace, lane);
    double* left_species = scratch.array(0);
    double* right_species = scratch.array(1);
    double* face_species = scratch.array(2);
    double* charge = scratch.array(3);
    double* inverse_mass = scratch.array(4);
    for (int linear = lane; linear < count; linear += blockDim.x * gridDim.x) {
        const int right_cell = diffusion_face_cell(grid, direction, linear);
        const int left_cell = right_cell - grid.stride(direction);

        FluidVector face_flux{};
        const auto geometry = make_grid_geometry_view(grid);
        const int ni = grid.ie - grid.is + (direction == 0 ? 1 : 0);
        const int nj = grid.je - grid.js + (direction == 1 ? 1 : 0);
        const int i = grid.is + linear % ni;
        const int j = grid.js + (linear / ni) % nj;
        const double spacing = DiffFlux::diffusion_face_spacing(
            geometry.geometry, grid.dim, direction, grid.dx1, grid.dx2, grid.dx3,
            geometry.GetCellCenterX(i), geometry.GetCellCenterY(j));
        const DiffFlux::DiffusionFaceStatus face_status =
            DiffFlux::evaluate_diffusion_face(
                state.load(left_cell), state.load(right_cell),
                state.n_species > 0
                    ? state.mass_fractions + left_cell : nullptr,
                state.n_species > 0
                    ? state.mass_fractions + right_cell : nullptr,
                state.n_species, state.total_size, spacing, eos, species, config,
                left_species, right_species, face_species, charge, inverse_mass,
                face_flux,
                state.n_species > 0
                    ? flux.mass_fractions + right_cell : nullptr,
                flux.total_size, config.use_viscous_diffusion
                    ? DiffFlux::viscous_basis_rotation(geometry, direction, i, j)
                    : DiffFlux::ViscousBasisRotation{});
        if (!face_status.valid) {
            atomicExch(status, 1);
            continue;
        }
        if (face_status.active) flux.store(right_cell, face_flux);
    }
}

struct DiffusionStateReader {
    DeviceStateView state;
    ARCH_INLINE FluidVector operator()(int cell) const { return state.load(cell); }
    ARCH_INLINE double fraction(int species, int cell) const
    { return state.mass_fractions[species * state.total_size + cell]; }
};

template <typename EosView>
__global__ void diffusion_dt_candidates_kernel(
    DeviceStateView state, DeviceGridView grid, EosView eos,
    SpeciesPODView species, DiffFlux::DiffusionConfigView config,
    double* candidates, int* status, SpeciesWorkspaceView workspace = {})
{
    const int lane = blockIdx.x * blockDim.x + threadIdx.x;
    const int count = grid.active_cell_count();
    SpeciesLaneScratch<5> scratch(workspace, lane);
    double* composition = scratch.array(0);
    double* neighbour_composition = scratch.array(1);
    double* face_composition = scratch.array(2);
    double* charge = scratch.array(3);
    double* inverse_mass = scratch.array(4);
    for (int linear = lane; linear < count; linear += blockDim.x * gridDim.x) {
        const int cell = grid.active_cell(linear);
        const auto geometry = make_grid_geometry_view(grid);
        const int ni = grid.ie - grid.is;
        const int nj = grid.je - grid.js;
        const int i = grid.is + linear % ni;
        const int j = grid.js + (linear / ni) % nj;
        const int k = grid.ks + linear / (ni * nj);
        const DiffFlux::DiffusionDtCandidate candidate =
            DiffFlux::evaluate_diffusion_dt_candidate(
                state.load(cell),
                state.n_species > 0 ? state.mass_fractions + cell : nullptr,
                state.n_species, state.total_size, eos, species, config,
                geometry, i, j, k,
                composition, neighbour_composition, face_composition,
                charge, inverse_mass, DiffusionStateReader{state});
        if (!candidate.valid) atomicExch(status, 1);
        candidates[linear] = candidate.value;
    }
}

template <typename EosView>
__global__ void diffusion_geometric_source_kernel(
    DeviceStateView state, DeviceStateView output, DeviceGridView grid,
    EosView eos, SpeciesPODView species, DiffFlux::DiffusionConfigView config,
    int* status, SpeciesWorkspaceView workspace = {})
{
    const int lane = blockIdx.x * blockDim.x + threadIdx.x;
    SpeciesLaneScratch<3> scratch(workspace, lane);
    double* composition = scratch.array(0);
    double* charge = scratch.array(1);
    double* inverse_mass = scratch.array(2);
    for (int linear = lane; linear < grid.active_cell_count();
         linear += blockDim.x * gridDim.x) {
        const int cell = grid.active_cell(linear);
        const int ni = grid.ie - grid.is;
        const int nj = grid.je - grid.js;
        const int i = grid.is + linear % ni;
        const int j = grid.js + (linear / ni) % nj;
        const int k = grid.ks + linear / (ni * nj);
        for (int species_index = 0; species_index < state.n_species; ++species_index)
            composition[species_index] = state.species(species_index, cell);
        FluidVector delta = output.load(cell);
        const auto result = DiffFlux::evaluate_geometric_diffusion_cell(
            state.load(cell), state.n_species > 0 ? composition : nullptr,
            eos, species, config, make_grid_geometry_view(grid), i, j, k, 1.0,
            charge, inverse_mass, delta, DiffusionStateReader{state});
        if (!result.valid) {
            atomicExch(status, 1);
            continue;
        }
        if (result.active) output.store(cell, delta);
    }
}

static __global__ void diffusion_dt_reduce_kernel(
    const double* candidates, int count, double* result)
{
    if (blockIdx.x != 0 || threadIdx.x != 0) return;
    const auto spec = arch::reduction::minimum_spec(
        DiffFlux::diffusion_dt_sentinel());
    auto state = arch::reduction::begin_reduction(spec);
    amr::CellLogicalKey seed_key{};
    seed_key.logical_i = -1;
    seed_key.component = 2;
    arch::reduction::combine_candidate(
        spec, state,
        {DiffFlux::diffusion_dt_sentinel(), seed_key, true});
    for (int cell = 0; cell < count; ++cell) {
        amr::CellLogicalKey key{};
        key.logical_i = cell;
        key.component = 2;
        arch::reduction::combine_candidate(
            spec, state, {candidates[cell], key, true});
    }
    const auto reduced = arch::reduction::finalize_reduction(spec, state);
    *result = DiffFlux::finalize_raw_diffusion_dt(reduced.value);
}

static __global__ void first_rkl_stage_kernel(
    DeviceStateView state_n, DeviceStateView increment,
    DeviceStateView destination, DeviceGridView grid, double coefficient)
{
    const int linear = blockIdx.x * blockDim.x + threadIdx.x;
    if (linear >= grid.active_cell_count()) return;
    const int cell = grid.active_cell(linear);
    FluidVector updated;
    Numerics::Diffusion::detail::apply_first_rkl_stage_cell(
        state_n.load(cell),
        state_n.n_species > 0 ? state_n.mass_fractions + cell : nullptr,
        increment.load(cell),
        increment.n_species > 0 ? increment.mass_fractions + cell : nullptr,
        state_n.n_species, state_n.total_size, coefficient, updated,
        destination.n_species > 0
            ? destination.mass_fractions + cell : nullptr);
    destination.store(cell, updated);
}

static __global__ void initialize_rkl_stage_buffers_kernel(
    DeviceStateView source, DeviceStateView scratch, DeviceStateView next)
{
    const int cell = blockIdx.x * blockDim.x + threadIdx.x;
    if (cell >= source.total_size) return;
    const FluidVector value = source.load(cell);
    scratch.store(cell, value);
    next.store(cell, value);
    scratch.enuc_rate[cell] = source.enuc_rate[cell];
    next.enuc_rate[cell] = source.enuc_rate[cell];
    for (int species = 0; species < source.n_species; ++species) {
        const double composition = source.species(species, cell);
        scratch.set_species(species, cell, composition);
        next.set_species(species, cell, composition);
    }
}

static __global__ void recursive_rkl_stage_kernel(
    DeviceStateView state_n, DeviceStateView state_previous,
    DeviceStateView state_older, DeviceStateView increment_previous,
    DeviceStateView increment_initial, DeviceStateView destination,
    DeviceGridView grid, DiffFunction::RKLCoeffs coefficients,
    bool second_order, double dt, bool increments_are_scaled)
{
    const int linear = blockIdx.x * blockDim.x + threadIdx.x;
    if (linear >= grid.active_cell_count()) return;
    const int cell = grid.active_cell(linear);
    FluidVector updated;
    Numerics::Diffusion::detail::apply_recursive_rkl_stage_cell(
        state_n.load(cell),
        state_n.n_species > 0 ? state_n.mass_fractions + cell : nullptr,
        state_previous.load(cell),
        state_previous.n_species > 0
            ? state_previous.mass_fractions + cell : nullptr,
        state_older.load(cell),
        state_older.n_species > 0
            ? state_older.mass_fractions + cell : nullptr,
        increment_previous.load(cell),
        increment_previous.n_species > 0
            ? increment_previous.mass_fractions + cell : nullptr,
        increment_initial.load(cell),
        increment_initial.n_species > 0
            ? increment_initial.mass_fractions + cell : nullptr,
        state_n.n_species, state_n.total_size, coefficients, second_order,
        dt, increments_are_scaled, updated,
        destination.n_species > 0
            ? destination.mass_fractions + cell : nullptr);
    destination.store(cell, updated);
}

inline bool valid_stage_views(
    const DeviceStateView& state_n, const DeviceStateView& increment,
    const DeviceStateView& destination, const DeviceGridView& grid)
{
    return valid_hydro_view(state_n)
        && valid_hydro_view(increment)
        && valid_hydro_view(destination)
        && valid_diffusion_grid(grid)
        && state_n.total_size == grid.total_size
        && matching_state_shape(state_n, increment)
        && matching_state_shape(state_n, destination);
}

} // namespace detail

template <typename EosView>
inline DiffusionLaunchResult launch_diffusion_operator(
    DeviceStateView state, DeviceStateView output, const EosView& eos,
    SpeciesPODView species, DeviceGridView grid,
    DiffFlux::DiffusionConfigView config,
    DiffusionWorkspaceView workspace,
    const CudaAmrFluxDirectionRouteView* amr_routes,
    double registration_weight, bool capture_initial_operator,
    cudaStream_t stream)
{
    DiffusionLaunchResult result{};
    if (!config.use_diffusion || !DiffFlux::diffusion_routes_enabled(config))
        return result;
    if (!valid_hydro_view(state) || !valid_hydro_view(output)
        || !valid_species_workspace(workspace.species_workspace, state.n_species, 5)
        || !valid_hydro_view(workspace.face_flux)
        || !detail::valid_diffusion_grid(grid)
        || state.total_size != grid.total_size
        || !detail::matching_state_shape(state, output)
        || !detail::matching_state_shape(state, workspace.face_flux)
        || species.size() != state.n_species
        || workspace.status == nullptr
        || detail::any_state_storage_alias(output, state)
        || detail::any_state_storage_alias(workspace.face_flux, state)
        || detail::any_state_storage_alias(workspace.face_flux, output)) {
        result.error = cudaErrorInvalidValue;
        return result;
    }

    result.error = cudaMemsetAsync(workspace.status, 0, sizeof(int), stream);
    if (result.error == cudaSuccess)
        result.error = clear_hydro_buffer(output, stream);
    if (result.error != cudaSuccess) return result;

    const auto bound_eos = bind_device_eos_status(eos, workspace.status);
    const int threads = detail::species_launch_threads(workspace.species_workspace);
    for (int direction = 0; direction < grid.dim; ++direction) {
        result.error = clear_hydro_buffer(workspace.face_flux, stream);
        if (result.error != cudaSuccess) return result;
        const int face_count = detail::diffusion_face_count(grid, direction);
        detail::diffusion_face_kernel
            <<<detail::species_launch_blocks(face_count, workspace.species_workspace),
               threads, 0, stream>>>(
                state, workspace.face_flux, grid, bound_eos, species,
                config, direction, workspace.status, workspace.species_workspace);
        result.error = cudaGetLastError();
        if (result.error != cudaSuccess) return result;
        ++result.kernels_launched;
        result.error = launch_hydro_divergence(
            workspace.face_flux, output, grid, 1.0, direction, stream);
        if (result.error != cudaSuccess) return result;
        ++result.kernels_launched;
        if (amr_routes != nullptr) {
            if (capture_initial_operator) {
                const auto capture = launch_cuda_amr_flux_capture_initial(
                    amr_routes[direction], grid, stream);
                if (capture.error != cudaSuccess) {
                    result.error = capture.error;
                    return result;
                }
                result.kernels_launched += capture.kernels_launched;
            }
            const auto registration = launch_cuda_amr_flux_register(
                amr_routes[direction], AmrFluxSource::StageScratch,
                registration_weight, stream);
            if (registration.error != cudaSuccess) {
                result.error = registration.error;
                return result;
            }
            result.kernels_launched += registration.kernels_launched;
        }
    }
    if (config.use_viscous_diffusion
        && grid.geometry != static_cast<int>(DeviceGeometry::Cartesian)) {
        detail::diffusion_geometric_source_kernel
            <<<detail::species_launch_blocks(grid.active_cell_count(), workspace.species_workspace), threads, 0, stream>>>(
                state, output, grid, bound_eos, species, config, workspace.status, workspace.species_workspace);
        result.error = cudaGetLastError();
        if (result.error != cudaSuccess) return result;
        ++result.kernels_launched;
    }
    result.effect = DiffusionWriteEffect::InteriorWritten;
    return result;
}

/** Preserve the focused operator-test ABI when AMR observation is absent. */
template <typename EosView>
inline DiffusionLaunchResult launch_diffusion_operator(
    DeviceStateView state, DeviceStateView output, const EosView& eos,
    SpeciesPODView species, DeviceGridView grid,
    DiffFlux::DiffusionConfigView config,
    DiffusionWorkspaceView workspace, cudaStream_t stream)
{
    return launch_diffusion_operator(
        state, output, eos, species, grid, config, workspace,
        nullptr, 0.0, false, stream);
}

template <typename EosView>
inline DiffusionLaunchResult launch_raw_diffusion_dt(
    DeviceStateView state, const EosView& eos, SpeciesPODView species,
    DeviceGridView grid, DiffFlux::DiffusionConfigView config,
    DiffusionWorkspaceView workspace, cudaStream_t stream)
{
    DiffusionLaunchResult result{};
    if (!config.use_diffusion || !DiffFlux::diffusion_routes_enabled(config)) {
        result.error = detail::write_dt_sentinel(workspace.dt_result, stream);
        return result;
    }
    if (!valid_hydro_view(state)
        || !valid_species_workspace(workspace.species_workspace, state.n_species, 5)
        || !detail::valid_diffusion_grid(grid)
        || state.total_size != grid.total_size
        || species.size() != state.n_species
        || workspace.dt_candidates == nullptr
        || workspace.dt_result == nullptr || workspace.status == nullptr) {
        result.error = cudaErrorInvalidValue;
        return result;
    }
    result.error = cudaMemsetAsync(workspace.status, 0, sizeof(int), stream);
    if (result.error != cudaSuccess) return result;
    const int threads = detail::species_launch_threads(workspace.species_workspace);
    const int count = grid.active_cell_count();
    detail::diffusion_dt_candidates_kernel
        <<<detail::species_launch_blocks(count, workspace.species_workspace), threads, 0, stream>>>(
            state, grid, bind_device_eos_status(eos, workspace.status), species, config,
            workspace.dt_candidates, workspace.status, workspace.species_workspace);
    result.error = cudaGetLastError();
    if (result.error != cudaSuccess) return result;
    ++result.kernels_launched;
    detail::diffusion_dt_reduce_kernel<<<1, 1, 0, stream>>>(
        workspace.dt_candidates, count, workspace.dt_result);
    result.error = cudaGetLastError();
    if (result.error == cudaSuccess) ++result.kernels_launched;
    return result;
}

inline DiffusionLaunchResult launch_first_rkl_stage(
    DeviceStateView state_n, DeviceStateView increment,
    DeviceStateView destination, DeviceGridView grid,
    DiffFlux::DiffusionConfigView config, double coefficient,
    cudaStream_t stream)
{
    DiffusionLaunchResult result{};
    if (!config.use_diffusion || !DiffFlux::diffusion_routes_enabled(config))
        return result;
    if (!detail::valid_stage_views(state_n, increment, destination, grid)
        || detail::any_state_storage_alias(destination, state_n)
        || detail::any_state_storage_alias(destination, increment)) {
        result.error = cudaErrorInvalidValue;
        return result;
    }
    constexpr int threads = 128;
    const int count = grid.active_cell_count();
    detail::first_rkl_stage_kernel
        <<<detail::hydro_launch_blocks(count, threads), threads, 0, stream>>>(
            state_n, increment, destination, grid, coefficient);
    result.error = cudaGetLastError();
    if (result.error == cudaSuccess) {
        result.kernels_launched = 1;
        result.effect = DiffusionWriteEffect::InteriorWritten;
    }
    return result;
}

inline DiffusionLaunchResult launch_recursive_rkl_stage(
    DeviceStateView state_n, DeviceStateView state_previous,
    DeviceStateView state_older, DeviceStateView increment_previous,
    DeviceStateView increment_initial, DeviceStateView destination,
    DeviceGridView grid, DiffFlux::DiffusionConfigView config,
    DiffFunction::RKLCoeffs coefficients, bool second_order,
    double dt, bool increments_are_scaled, cudaStream_t stream)
{
    DiffusionLaunchResult result{};
    if (!config.use_diffusion || !DiffFlux::diffusion_routes_enabled(config))
        return result;
    if (!detail::valid_stage_views(
            state_n, increment_previous, destination, grid)
        || !valid_hydro_view(state_previous)
        || !valid_hydro_view(state_older)
        || !valid_hydro_view(increment_initial)
        || !detail::matching_state_shape(state_n, state_previous)
        || !detail::matching_state_shape(state_n, state_older)
        || !detail::matching_state_shape(state_n, increment_initial)
        || detail::any_state_storage_alias(destination, state_n)
        || detail::any_state_storage_alias(destination, state_previous)
        || detail::any_state_storage_alias(destination, increment_previous)
        || detail::any_state_storage_alias(destination, increment_initial)
        || (!detail::same_state_storage(destination, state_older)
            && detail::any_state_storage_alias(destination, state_older))) {
        result.error = cudaErrorInvalidValue;
        return result;
    }
    constexpr int threads = 128;
    const int count = grid.active_cell_count();
    detail::recursive_rkl_stage_kernel
        <<<detail::hydro_launch_blocks(count, threads), threads, 0, stream>>>(
            state_n, state_previous, state_older, increment_previous,
            increment_initial, destination, grid, coefficients,
            second_order, dt, increments_are_scaled);
    result.error = cudaGetLastError();
    if (result.error == cudaSuccess) {
        result.kernels_launched = 1;
        result.effect = DiffusionWriteEffect::InteriorWritten;
    }
    return result;
}

inline DiffusionLaunchResult launch_initialize_rkl_stage_buffers(
    DeviceStateView source, DeviceStateView scratch, DeviceStateView next,
    DiffFlux::DiffusionConfigView config, cudaStream_t stream)
{
    DiffusionLaunchResult result{};
    if (!config.use_diffusion || !DiffFlux::diffusion_routes_enabled(config))
        return result;
    if (!valid_hydro_view(source)
        || !valid_hydro_view(scratch) || !valid_hydro_view(next)
        || !detail::matching_state_shape(source, scratch)
        || !detail::matching_state_shape(source, next)
        || detail::any_state_storage_alias(scratch, source)
        || detail::any_state_storage_alias(next, source)
        || detail::any_state_storage_alias(scratch, next)) {
        result.error = cudaErrorInvalidValue;
        return result;
    }
    constexpr int threads = 128;
    detail::initialize_rkl_stage_buffers_kernel
        <<<detail::hydro_launch_blocks(source.total_size, threads),
           threads, 0, stream>>>(source, scratch, next);
    result.error = cudaGetLastError();
    if (result.error == cudaSuccess) {
        result.kernels_launched = 1;
        result.effect = DiffusionWriteEffect::CompleteStateCopied;
    }
    return result;
}

// A successful stage result declares only an interior write. Runtime control owns the
// ensuing ghost invalidation, boundary/exchange completion, and complete
// logical-slot rotation; these launchers never rotate pointers or publish a
// readable ghost region.

} // namespace arch::cuda
