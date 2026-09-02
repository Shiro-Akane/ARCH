/**
 * @file CudaBackendAmrFlux.cu
 * @brief Launch control for compact AMR flux surfaces.
 */

#include "cuda/runtime/CudaBackendAmrFlux.h"

#include "cuda/amr/AmrFluxSurfaceKernels.cuh"

#include <cmath>
#include <cstddef>
#include <limits>

namespace arch::cuda {
namespace {

constexpr int kThreads = 128;

int launch_blocks(int count) noexcept
{
    return (count + kThreads - 1) / kThreads;
}

bool valid_count(int count) noexcept
{
    return count > 0
        && count <= std::numeric_limits<int>::max() - (kThreads - 1);
}

} // namespace

cudaError_t launch_cuda_amr_flux_surface_clear(
    DeviceAmrFluxSurfaceView surface, cudaStream_t stream)
{
    if (!valid_amr_flux_surface(surface)) return cudaErrorInvalidValue;
    const std::size_t scalar_count = amr_flux_surface_scalar_count(
        surface.cell_count, surface.species_count);
    if (scalar_count == 0
        || scalar_count > static_cast<std::size_t>(
            std::numeric_limits<int>::max()))
        return cudaErrorInvalidValue;
    const int count = static_cast<int>(scalar_count);
    amr_flux_kernel_detail::clear_surface_kernel
        <<<launch_blocks(count), kThreads, 0, stream>>>(surface, count);
    return cudaGetLastError();
}

cudaError_t launch_cuda_amr_flux_surface_capture(
    const DeviceAmrFluxBlockView* device_blocks, int block_count,
    int source_block, int source_face, int face_cells,
    cudaStream_t stream)
{
    if (device_blocks == nullptr || block_count <= 0
        || source_block < 0 || source_block >= block_count
        || source_face < 0 || source_face >= 6
        || !valid_count(face_cells))
        return cudaErrorInvalidValue;
    amr_flux_kernel_detail::capture_surface_kernel
        <<<launch_blocks(face_cells), kThreads, 0, stream>>>(
            device_blocks, source_block, source_face, face_cells);
    return cudaGetLastError();
}

cudaError_t launch_cuda_amr_flux_register_route(
    const DeviceAmrFluxBlockView* device_blocks, int block_count,
    int source_block,
    const amr::AmrFluxRegistrationTarget* device_targets,
    int target_count,
    const amr::AmrFluxRegistrationTerm* device_terms,
    int term_count, AmrFluxSource source, double stage_weight,
    cudaStream_t stream)
{
    if (device_blocks == nullptr || block_count <= 0
        || source_block < 0 || source_block >= block_count
        || device_targets == nullptr || !valid_count(target_count)
        || device_terms == nullptr || !valid_count(term_count)
        || (source != AmrFluxSource::StageScratch
            && source != AmrFluxSource::InitialSurface)
        || !std::isfinite(stage_weight))
        return cudaErrorInvalidValue;
    if (stage_weight == 0.0) return cudaSuccess;
    amr_flux_kernel_detail::register_route_kernel
        <<<launch_blocks(target_count), kThreads, 0, stream>>>(
            device_blocks, source_block, device_targets, device_terms,
            target_count, source, stage_weight);
    return cudaGetLastError();
}

CudaAmrFluxLaunchResult launch_cuda_amr_flux_capture_initial(
    CudaAmrFluxDirectionRouteView route, DeviceGridView grid,
    cudaStream_t stream)
{
    CudaAmrFluxLaunchResult result{};
    if (route.target_count == 0) return result;
    if (route.direction < 0 || route.direction >= grid.dim
        || route.source_face_mask == 0) {
        result.error = cudaErrorInvalidValue;
        return result;
    }
    const int face_cells = route.direction == 0
        ? (grid.je - grid.js) * (grid.ke - grid.ks)
        : (route.direction == 1
            ? (grid.ie - grid.is) * (grid.ke - grid.ks)
            : (grid.ie - grid.is) * (grid.je - grid.js));
    for (int side = 0; side < 2; ++side) {
        const int face = 2 * route.direction + side;
        if ((route.source_face_mask & (1U << face)) == 0) continue;
        result.error = launch_cuda_amr_flux_surface_capture(
            route.device_blocks, route.block_count, route.source_block,
            face, face_cells, stream);
        if (result.error != cudaSuccess) return result;
        ++result.kernels_launched;
    }
    return result;
}

CudaAmrFluxLaunchResult launch_cuda_amr_flux_register(
    CudaAmrFluxDirectionRouteView route, AmrFluxSource source,
    double stage_weight, cudaStream_t stream)
{
    CudaAmrFluxLaunchResult result{};
    if (route.target_count == 0 || stage_weight == 0.0) return result;
    result.error = launch_cuda_amr_flux_register_route(
        route.device_blocks, route.block_count, route.source_block,
        route.device_targets, route.target_count, route.device_terms,
        route.term_count, source, stage_weight, stream);
    if (result.error == cudaSuccess) result.kernels_launched = 1;
    return result;
}

cudaError_t launch_cuda_amr_reflux(
    const DeviceAmrFluxBlockView* device_blocks, int block_count,
    const amr::AmrRefluxTarget* device_targets, int target_count,
    const amr::AmrRefluxContribution* device_contributions,
    int contribution_count, double dt, cudaStream_t stream)
{
    if (device_blocks == nullptr || block_count <= 0
        || device_targets == nullptr || !valid_count(target_count)
        || device_contributions == nullptr
        || !valid_count(contribution_count)
        || !std::isfinite(dt) || dt < 0.0)
        return cudaErrorInvalidValue;
    if (dt == 0.0) return cudaSuccess;
    amr_flux_kernel_detail::reflux_kernel
        <<<launch_blocks(target_count), kThreads, 0, stream>>>(
            device_blocks, device_targets, device_contributions,
            target_count, dt);
    return cudaGetLastError();
}

} // namespace arch::cuda
