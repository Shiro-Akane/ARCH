/**
 * @file CudaBackendDiffusion.h
 * @brief Narrow runtime interface to CUDA diffusion and RKL instantiations.
 *
 * Launches borrow stage slots, metric views and scratch storage. The shared
 * scheduler supplies stage descriptors; runtime control orders ghost exchange,
 * checks completed status and publishes output-slot validity.
 */

#pragma once

#include "cuda/runtime/hydro/CudaBackendHydro.h"
#include "numerics/diffusion/DiffusionTypes.h"

namespace arch::cuda {

struct CudaBackendDiffusionWorkspace {
    DeviceStateView face_flux{};
    double* candidates = nullptr;
    double* result = nullptr;
    int* status = nullptr;
    SpeciesWorkspaceView species_workspace{};
};

cudaError_t copy_cuda_backend_state_slot(
    DeviceStateView source, DeviceStateView destination,
    cudaStream_t stream);

#define ARCH_DECLARE_BACKEND_DIFFUSION(EOS) \
    CudaBackendLaunchResult launch_cuda_backend_diffusion_dt( \
        DeviceStateView state, EOS eos, SpeciesPODView species, \
        DeviceGridView grid, DiffFlux::DiffusionConfigView config, \
        CudaBackendDiffusionWorkspace workspace, cudaStream_t stream); \
    CudaBackendLaunchResult launch_cuda_backend_diffusion_stage( \
        const scheduler::RklPlan& plan, \
        const scheduler::RklStageDescriptor& descriptor, \
        DeviceStateView state_n, DeviceStateView previous, \
        DeviceStateView older, DeviceStateView output, \
        DeviceStateView delta, DeviceStateView initial_delta, \
        EOS eos, SpeciesPODView species, DeviceGridView grid, \
        DiffFlux::DiffusionConfigView config, \
        CudaBackendDiffusionWorkspace workspace, \
        const CudaAmrFluxDirectionRouteView* amr_routes, double dt, \
        cudaStream_t stream)

ARCH_DECLARE_BACKEND_DIFFUSION(IdealGasView);
ARCH_DECLARE_BACKEND_DIFFUSION(HelmEosView);
ARCH_DECLARE_BACKEND_DIFFUSION(Tabular3DEOSView);
ARCH_DECLARE_BACKEND_DIFFUSION(Tabular4DEOSView);

#undef ARCH_DECLARE_BACKEND_DIFFUSION

} // namespace arch::cuda
