/**
 * @file CudaBackendHydro.h
 * @brief Narrow runtime ABI for CUDA Hydro kernel instantiations.
 */

#pragma once

#include "cuda/common/CudaCommon.cuh"
#include "driver/StageScheduler.h"
#include "driver/dispatch/ResolvedExecutionPlan.h"
#include "physics/eos/HelmEos.h"
#include "physics/eos/IdealGas.h"
#include "physics/eos/Tabular3DEOS.h"
#include "physics/eos/Tabular4DEOS.h"

#include <cuda_runtime.h>

namespace arch::cuda {

struct CudaBackendLaunchResult {
    cudaError_t error = cudaSuccess;
    int kernels_launched = 0;
    bool route_found = true;
};

#define ARCH_DECLARE_BACKEND_HYDRO(EOS) \
    cudaError_t launch_cuda_backend_hydro_dt( \
        DeviceStateView state, DeviceGridView grid, EOS eos, double cfl, \
        CudaHydroWorkspaceView workspace, cudaStream_t stream); \
    CudaBackendLaunchResult launch_cuda_backend_hydro_stage( \
        const dispatch::ResolvedExecutionPlan& plan, DeviceStateView old_state, \
        DeviceStateView input, DeviceStateView output, DeviceStateView delta, \
        DeviceStateView face_flux, DeviceGridView grid, EOS eos, \
        double entropy_fix_coefficient, double density_floor, \
        double minimum_internal_energy, \
        double maximum_internal_energy, \
        const scheduler::StageDescriptor& descriptor, double dt, \
        cudaStream_t stream)

ARCH_DECLARE_BACKEND_HYDRO(IdealGasView);
ARCH_DECLARE_BACKEND_HYDRO(HelmEosView);
ARCH_DECLARE_BACKEND_HYDRO(Tabular3DEOSView);
ARCH_DECLARE_BACKEND_HYDRO(Tabular4DEOSView);

#undef ARCH_DECLARE_BACKEND_HYDRO

} // namespace arch::cuda
