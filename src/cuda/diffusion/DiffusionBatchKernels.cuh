/**
 * @file DiffusionBatchKernels.cuh
 * @brief Batch block traversal without changing the shared diffusion/RKL math.
 *
 * The runtime supplies borrowed state and workspace bindings. Kernels evaluate
 * the existing cell/face operators; the common scheduler still owns each RKL
 * stage's ghost, flux-registration, reflux and publication boundaries. Global
 * species scratch is serialized by selecting one block per wave. Returned
 * launch status is not a completion witness; the caller drains the stream.
 */
#pragma once

#include "cuda/diffusion/DiffusionKernels.cuh"
#include <algorithm>

namespace arch::cuda {
namespace detail {

inline DiffusionWorkspaceView diffusion_workspace(CudaBackendDiffusionWorkspace w)
{
    return {w.face_flux, w.candidates, w.result, w.status, w.species_workspace};
}

static __global__ void diffusion_batch_reset(const DeviceDiffusionBatchBlock* blocks, bool reset_dt = true)
{
    const auto& b = blocks[blockIdx.y];
    if (threadIdx.x == 0) {
        *b.workspace.status = 0;
        if (reset_dt) *b.workspace.result = DiffFlux::diffusion_dt_sentinel();
    }
}

static __global__ void diffusion_batch_clear(const DeviceDiffusionBatchBlock* blocks, bool faces)
{
    const auto& b = blocks[blockIdx.y];
    const int cell = blockIdx.x * blockDim.x + threadIdx.x;
    if (cell == 0 && !faces) *b.workspace.status = 0;
    const auto view = faces ? b.workspace.face_flux : b.delta;
    if (cell >= view.total_size) return;
    view.store(cell, {0.0, 0.0, 0.0, 0.0, 0.0});
    for (int s = 0; s < view.n_species; ++s) view.set_species(s, cell, 0.0);
}

static __global__ void diffusion_batch_divergence(const DeviceDiffusionBatchBlock* blocks, int direction)
{
    const auto& b = blocks[blockIdx.y];
    if (direction < b.grid.dim)
        hydro_divergence_kernel_work(b.workspace.face_flux, b.delta, b.grid, 1.0, direction);
}

static __global__ void state_copy_batch_kernel(const DeviceStateCopyBlock* blocks)
{
    const auto& b = blocks[blockIdx.y];
    const int cell = blockIdx.x * blockDim.x + threadIdx.x;
    if (cell >= b.source.total_size) return;
    b.destination.store(cell, b.source.load(cell));
    b.destination.enuc_rate[cell] = b.source.enuc_rate[cell];
    for (int s = 0; s < b.source.n_species; ++s)
        b.destination.set_species(s, cell, b.source.species(s, cell));
}

// A global species arena is deliberately restricted to one block per wave.
// Local scratch has no cross-block aliases and can use all blocks in the wave.
inline std::size_t diffusion_wave_limit(std::span<const DeviceDiffusionBatchBlock> host)
{
    for (const auto& b : host) if (b.workspace.species_workspace.values) return 1;
    return 1024;
}

struct DiffusionWaveExtent { int cells=0, storage=0, faces=0, dimension=0; bool curved=false; };
inline DiffusionWaveExtent diffusion_wave_extent(std::span<const DeviceDiffusionBatchBlock> wave)
{
    DiffusionWaveExtent extent;
    for (const auto& b : wave) {
        extent.cells = std::max(extent.cells, b.grid.active_cell_count());
        extent.storage = std::max(extent.storage, b.grid.total_size);
        extent.dimension = std::max(extent.dimension, b.grid.dim);
        extent.curved |= b.grid.geometry != static_cast<int>(DeviceGeometry::Cartesian);
        for (int d = 0; d < b.grid.dim; ++d)
            extent.faces = std::max(extent.faces, diffusion_face_count(b.grid, d));
    }
    return extent;
}

} // namespace detail

template<class Eos>
CudaBackendLaunchResult launch_diffusion_dt_batch(
    std::span<const DeviceDiffusionBatchBlock> host, const DeviceDiffusionBatchBlock* device,
    Eos eos, SpeciesPODView species, DiffFlux::DiffusionConfigView config, cudaStream_t stream)
{
    CudaBackendLaunchResult result{};
    if (host.empty()) return result;
    if (!device) return {cudaErrorInvalidValue, 0, true};
    for (const auto& b : host)
        if (!detail::valid_dt_views(b.input, species, b.grid, detail::diffusion_workspace(b.workspace)))
            return {cudaErrorInvalidValue, 0, true};
    const auto limit = detail::diffusion_wave_limit(host);
    const auto record = [&] {
        result.error = cudaGetLastError();
        if (result.error == cudaSuccess) ++result.kernels_launched;
        return result.error == cudaSuccess;
    };
    for (std::size_t first = 0; first < host.size(); first += limit) {
        const auto count = std::min(limit, host.size() - first);
        const auto wave = host.subspan(first, count);
        const auto extent = detail::diffusion_wave_extent(wave);
        const auto& b = wave.front();
        const auto scratch = b.workspace.species_workspace;
        const auto* bindings = device + first;
        const dim3 reduction_grid(1, static_cast<unsigned>(count));
        detail::diffusion_batch_reset<<<reduction_grid, 1, 0, stream>>>(bindings);
        if (!record()) return result;
        if (!config.use_diffusion || !DiffFlux::diffusion_routes_enabled(config)) continue;
        detail::diffusion_dt_candidates_kernel<<<
            dim3(detail::species_launch_blocks(extent.cells, scratch), static_cast<unsigned>(count)),
            detail::species_launch_threads(scratch), 0, stream>>>(
                b.input, b.grid, eos, species, config, b.workspace.candidates,
                b.workspace.status, scratch, bindings);
        if (!record()) return result;
        detail::diffusion_dt_reduce_kernel<<<reduction_grid, 1, 0, stream>>>(
            b.workspace.candidates, 0, b.workspace.result, bindings);
        if (!record()) return result;
    }
    return result;
}

template<class Eos>
CudaBackendLaunchResult launch_diffusion_stage_batch(
    const scheduler::RklPlan& plan, const scheduler::RklStageDescriptor& descriptor,
    std::span<const DeviceDiffusionBatchBlock> host, const DeviceDiffusionBatchBlock* device,
    Eos eos, SpeciesPODView species, DiffFlux::DiffusionConfigView config,
    double dt, cudaStream_t stream)
{
    CudaBackendLaunchResult result{};
    if (host.empty()) return result;
    if (!device) return {cudaErrorInvalidValue, 0, true};
    for (const auto& b : host) {
        if (!detail::valid_operator_views(b.input, b.delta, species, b.grid,
                detail::diffusion_workspace(b.workspace))
            || (descriptor.stage == 1
                ? !detail::valid_first_stage_views(b.state_n, b.delta, b.output, b.grid)
                : !detail::valid_recursive_stage_views(b.state_n, b.previous, b.older,
                    b.delta, b.initial_delta, b.output, b.grid)))
            return {cudaErrorInvalidValue, 0, true};
    }
    const auto coefficients = DiffFunction::get_rkl_coeffs(
        plan.second_order ? DiffFunction::RKLOrder::Second : DiffFunction::RKLOrder::First,
        descriptor.stage, static_cast<int>(plan.stages.size()));
    const auto limit = detail::diffusion_wave_limit(host);
    const auto record = [&] {
        result.error = cudaGetLastError();
        if (result.error == cudaSuccess) ++result.kernels_launched;
        return result.error == cudaSuccess;
    };
    for (std::size_t first = 0; first < host.size(); first += limit) {
        const auto count = std::min(limit, host.size() - first);
        const auto wave = host.subspan(first, count);
        const auto extent = detail::diffusion_wave_extent(wave);
        const auto& b = wave.front();
        const auto scratch = b.workspace.species_workspace;
        const auto* bindings = device + first;
        if (!config.use_diffusion || !DiffFlux::diffusion_routes_enabled(config)) {
            detail::diffusion_batch_reset<<<dim3(1, static_cast<unsigned>(count)), 1, 0, stream>>>(bindings, false);
            if (!record()) return result;
            continue;
        }
        const dim3 storage_grid(detail::hydro_launch_blocks(extent.storage, 128), static_cast<unsigned>(count));
        const dim3 cell_grid(detail::hydro_launch_blocks(extent.cells, 128), static_cast<unsigned>(count));
        const dim3 face_grid(detail::species_launch_blocks(extent.faces, scratch), static_cast<unsigned>(count));
        const int threads = detail::species_launch_threads(scratch);
        detail::diffusion_batch_clear<<<storage_grid, 128, 0, stream>>>(bindings, false);
        if (!record()) return result;
        for (int direction = 0; direction < extent.dimension; ++direction) {
            detail::diffusion_batch_clear<<<storage_grid, 128, 0, stream>>>(bindings, true);
            if (!record()) return result;
            detail::diffusion_face_kernel<<<face_grid, threads, 0, stream>>>(
                b.input, b.workspace.face_flux, b.grid, eos, species, config,
                direction, b.workspace.status, scratch, bindings);
            if (!record()) return result;
            detail::diffusion_batch_divergence<<<cell_grid, 128, 0, stream>>>(bindings, direction);
            if (!record()) return result;
            // Do not regroup current and initial contributions: for a shared
            // destination retain block order and each block's tilde_mu -> gamma
            // order, including negative gamma, before this scratch is reused.
            for (const auto& block : wave) {
                if (direction >= block.grid.dim) continue;
                const auto& route = block.routes[direction];
                if (descriptor.stage == 1 && plan.second_order) {
                    const auto captured = launch_cuda_amr_flux_capture_initial(route, block.grid, stream);
                    result.kernels_launched += captured.kernels_launched;
                    if ((result.error = captured.error) != cudaSuccess) return result;
                }
                const auto registered = launch_cuda_amr_flux_register(
                    route, AmrFluxSource::StageScratch, coefficients.tilde_mu, stream);
                result.kernels_launched += registered.kernels_launched;
                if ((result.error = registered.error) != cudaSuccess) return result;
                if (descriptor.stage > 1 && plan.second_order) {
                    const auto initial = launch_cuda_amr_flux_register(
                        route, AmrFluxSource::InitialSurface, coefficients.gamma, stream);
                    result.kernels_launched += initial.kernels_launched;
                    if ((result.error = initial.error) != cudaSuccess) return result;
                }
            }
        }
        if (config.use_viscous_diffusion && extent.curved) {
            detail::diffusion_geometric_source_kernel<<<
                dim3(detail::species_launch_blocks(extent.cells, scratch), static_cast<unsigned>(count)),
                threads, 0, stream>>>(b.input, b.delta, b.grid, eos, species, config,
                    b.workspace.status, scratch, bindings);
            if (!record()) return result;
        }
        if (descriptor.stage == 1) {
            detail::first_rkl_stage_kernel<<<cell_grid, 128, 0, stream>>>(
                b.state_n, b.delta, b.output, b.grid, coefficients.tilde_mu * dt, bindings);
        } else {
            detail::recursive_rkl_stage_kernel<<<cell_grid, 128, 0, stream>>>(
                b.state_n, b.previous, b.older, b.delta, b.initial_delta, b.output,
                b.grid, coefficients, plan.second_order, dt, false, bindings);
        }
        if (!record()) return result;
    }
    return result;
}

} // namespace arch::cuda
