/**
 * @file CudaBackendAmrFlux.h
 * @brief Local CUDA launch ABI for compact AMR flux surfaces.
 */

#pragma once

#include "cuda/amr/AmrFluxSurfaceTypes.cuh"

#include <cuda_runtime.h>

namespace amr {
struct AmrFluxRegistrationTarget;
struct AmrFluxRegistrationTerm;
struct AmrRefluxTarget;
struct AmrRefluxContribution;
}

namespace arch::cuda {

struct CudaAmrFluxDirectionRouteView {
    const DeviceAmrFluxBlockView* device_blocks = nullptr;
    int block_count = 0;
    int source_block = -1;
    int direction = -1;
    const amr::AmrFluxRegistrationTarget* device_targets = nullptr;
    int target_count = 0;
    const amr::AmrFluxRegistrationTerm* device_terms = nullptr;
    int term_count = 0;
    unsigned int source_face_mask = 0;
};

struct CudaAmrFluxLaunchResult {
    cudaError_t error = cudaSuccess;
    int kernels_launched = 0;
};

cudaError_t launch_cuda_amr_flux_surface_clear(
    DeviceAmrFluxSurfaceView surface, cudaStream_t stream);

/** Capture F(Y0) immediately after one direction overwrites stage_flux. */
cudaError_t launch_cuda_amr_flux_surface_capture(
    const DeviceAmrFluxBlockView* device_blocks, int block_count,
    int source_block, int source_face, int face_cells,
    cudaStream_t stream);

/**
 * Register one canonical source-block/direction route before stage_flux is
 * overwritten by the next direction.  targets and terms are device arrays
 * uploaded from AmrCompiledFluxRegistrationRoute.
 */
cudaError_t launch_cuda_amr_flux_register_route(
    const DeviceAmrFluxBlockView* device_blocks, int block_count,
    int source_block,
    const amr::AmrFluxRegistrationTarget* device_targets,
    int target_count,
    const amr::AmrFluxRegistrationTerm* device_terms,
    int term_count, AmrFluxSource source, double stage_weight,
    cudaStream_t stream);

CudaAmrFluxLaunchResult launch_cuda_amr_flux_capture_initial(
    CudaAmrFluxDirectionRouteView route, DeviceGridView grid,
    cudaStream_t stream);

CudaAmrFluxLaunchResult launch_cuda_amr_flux_register(
    CudaAmrFluxDirectionRouteView route, AmrFluxSource source,
    double stage_weight, cudaStream_t stream);

/** Reflux the selected block state slots using a topology base plan and dt. */
cudaError_t launch_cuda_amr_reflux(
    const DeviceAmrFluxBlockView* device_blocks, int block_count,
    const amr::AmrRefluxTarget* device_targets, int target_count,
    const amr::AmrRefluxContribution* device_contributions,
    int contribution_count, double dt, cudaStream_t stream);

} // namespace arch::cuda
