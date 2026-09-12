/** Cross-block traversal of the same per-thread Hydro work as scalar launches. */
#pragma once

#include "HydroIntegratorPolicies.cuh"
#include "CheckedHydroEos.cuh"
#include "cuda/runtime/hydro/CudaBackendHydro.h"
#include <algorithm>

namespace arch::cuda {
namespace detail {

static __global__ void hydro_batch_clear(const DeviceHydroBatchBlock* blocks)
{
    const auto& b = blocks[blockIdx.y];
    const int cell = blockIdx.x * blockDim.x + threadIdx.x;
    if (cell == 0) *b.eos_status = 0;
    if (cell >= b.delta.total_size) return;
    b.delta.store(cell, {0.0, 0.0, 0.0, 0.0, 0.0});
    for (int s = 0; s < b.delta.n_species; ++s) b.delta.set_species(s, cell, 0.0);
    b.output.enuc_rate[cell] = 0.0;
}

template<class Reconstruction, class Flux, class Eos>
__global__ void hydro_batch_faces(const DeviceHydroBatchBlock* blocks, Eos eos,
    int direction, double coefficient, SpeciesWorkspaceView workspace)
{
    const auto& b = blocks[blockIdx.y];
    if (direction >= b.grid.dim) return;
    hydro_face_kernel_work<Reconstruction, Flux>(b.input, b.face_flux, b.grid,
        make_checked_hydro_eos(eos, b.eos_status), direction, coefficient, workspace);
}

static __global__ void hydro_batch_divergence(const DeviceHydroBatchBlock* blocks,
    int direction, double dt)
{
    const auto& b = blocks[blockIdx.y];
    if (direction < b.grid.dim)
        hydro_divergence_kernel_work(b.face_flux, b.delta, b.grid, dt, direction);
}

template<class Eos>
__global__ void hydro_batch_sources(const DeviceHydroBatchBlock* blocks, Eos eos,
    double dt, SpeciesWorkspaceView workspace, Physical::Gravity::ExternalGravityView gravity)
{
    const auto& b = blocks[blockIdx.y];
    if (b.grid.geometry != static_cast<int>(DeviceGeometry::Cartesian) || gravity.enabled)
        hydro_source_kernel_work(b.input, b.delta, b.grid,
            make_checked_hydro_eos(eos, b.eos_status), dt, workspace, gravity);
}

static __global__ void hydro_batch_update(const DeviceHydroBatchBlock* blocks,
    double old_weight, double update_weight, double density_floor,
    double minimum_internal_energy, double maximum_internal_energy)
{
    const auto& b = blocks[blockIdx.y];
    hydro_single_stage_update_kernel_work(b.old_state, b.input, b.output, b.delta,
        b.grid, old_weight, update_weight, density_floor,
        minimum_internal_energy, maximum_internal_energy);
}

template<class Reconstruction>
bool valid_hydro_batch_block(const DeviceHydroBatchBlock& b, SpeciesWorkspaceView workspace)
{
    if (!valid_hydro_grid(b.grid) || !b.eos_status
        || !valid_species_workspace(workspace, b.input.n_species, 4)
        || b.grid.ng < Reconstruction::ghost_depth || !b.grid.cell_volume
        || make_grid_geometry_view(b.grid).geometry == GridMetrics::Geometry::Unsupported)
        return false;
    for (const auto view : {b.old_state, b.input, b.output, b.delta, b.face_flux})
        if (!valid_hydro_view(view) || view.total_size != b.grid.total_size
            || view.n_species != b.input.n_species) return false;
    const int begin[]{b.grid.is, b.grid.js, b.grid.ks};
    const int end[]{b.grid.ie, b.grid.je, b.grid.ke};
    const int total[]{b.grid.total_x, b.grid.total_y, b.grid.total_z};
    for (int axis = 0; axis < b.grid.dim; ++axis)
        if (begin[axis] < Reconstruction::ghost_depth
            || total[axis] - end[axis] < Reconstruction::ghost_depth
            || !b.grid.face_area_lower[axis] || !b.grid.face_area_upper[axis]) return false;
    return true;
}

} // namespace detail

template<class Reconstruction, class Flux, class Eos>
CudaBackendLaunchResult launch_hydro_batch(
    std::span<const DeviceHydroBatchBlock> host, const DeviceHydroBatchBlock* device,
    Eos eos, double coefficient, double density_floor, double min_e, double max_e,
    const scheduler::StageDescriptor& descriptor, double dt, cudaStream_t stream,
    SpeciesWorkspaceView workspace, Physical::Gravity::ExternalGravityView gravity)
{
    CudaBackendLaunchResult result{};
    if (host.empty()) return result;
    if (!device) return {cudaErrorInvalidValue, 0, true};
    for (const auto& b : host)
        if (!detail::valid_hydro_batch_block<Reconstruction>(b, workspace))
            return {cudaErrorInvalidValue, 0, true};
    // Wide species use the existing bounded lane arena. One block per wave
    // avoids cross-block aliasing without multiplying its memory budget.
    // Local per-thread species scratch can execute many blocks concurrently.
    const std::size_t wave_limit = workspace.values ? 1 : 1024;
    for (std::size_t first = 0; first < host.size(); first += wave_limit) {
        const auto count = std::min(wave_limit, host.size() - first);
        const auto wave = host.subspan(first, count);
        int cells = 0, storage = 0, faces = 0, dimension = 0;
        bool sources = gravity.enabled;
        for (const auto& b : wave) {
            cells = std::max(cells, b.grid.active_cell_count());
            storage = std::max(storage, b.grid.total_size);
            dimension = std::max(dimension, b.grid.dim);
            sources |= b.grid.geometry != static_cast<int>(DeviceGeometry::Cartesian);
            for (int d = 0; d < b.grid.dim; ++d) {
                const int extent[]{b.grid.ie - b.grid.is, b.grid.je - b.grid.js, b.grid.ke - b.grid.ks};
                faces = std::max(faces, (extent[0] + (d == 0))
                    * (extent[1] + (d == 1)) * (extent[2] + (d == 2)));
            }
        }
        const auto record = [&] {
            result.error = cudaGetLastError();
            if (result.error == cudaSuccess) ++result.kernels_launched;
            return result.error == cudaSuccess;
        };
        const dim3 storage_grid(detail::hydro_launch_blocks(storage, 128), static_cast<unsigned>(count));
        const dim3 cell_grid(detail::hydro_launch_blocks(cells, 128), static_cast<unsigned>(count));
        const dim3 face_grid(detail::species_launch_blocks(faces, workspace), static_cast<unsigned>(count));
        const dim3 source_grid(detail::species_launch_blocks(cells, workspace), static_cast<unsigned>(count));
        const int threads = detail::species_launch_threads(workspace);
        const auto* bindings = device + first;
        detail::hydro_batch_clear<<<storage_grid, 128, 0, stream>>>(bindings);
        if (!record()) return result;
        for (int direction = 0; direction < dimension; ++direction) {
            detail::hydro_batch_faces<Reconstruction, Flux><<<face_grid, threads, 0, stream>>>(
                bindings, eos, direction, coefficient, workspace);
            if (!record()) return result;
            detail::hydro_batch_divergence<<<cell_grid, 128, 0, stream>>>(bindings, direction, dt);
            if (!record()) return result;
            // Routes can accumulate into the same surface. Preserve Host route
            // order and complete registration before overwriting this direction.
            for (const auto& b : wave) {
                if (direction >= b.grid.dim) continue;
                const auto registered = launch_cuda_amr_flux_register(b.routes[direction],
                    AmrFluxSource::StageScratch, descriptor.flux_register_weight, stream);
                result.error = registered.error;
                result.kernels_launched += registered.kernels_launched;
                if (result.error != cudaSuccess) return result;
            }
        }
        if (sources) {
            detail::hydro_batch_sources<<<source_grid, threads, 0, stream>>>(bindings, eos, dt, workspace, gravity);
            if (!record()) return result;
        }
        detail::hydro_batch_update<<<cell_grid, 128, 0, stream>>>(bindings,
            descriptor.old_weight, descriptor.update_weight, density_floor, min_e, max_e);
        if (!record()) return result;
    }
    return result;
}

} // namespace arch::cuda
