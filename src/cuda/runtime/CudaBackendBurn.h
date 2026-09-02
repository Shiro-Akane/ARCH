/**
 * @file CudaBackendBurn.h
 * @brief Narrow runtime ABI between the CUDA backend and burn instantiations.
 */

#pragma once

#include "CudaBackendTypes.h"
#include "cuda/common/CudaCommon.cuh"
#include "cuda/common/CudaLaunchConfig.h"
#include "cuda/microphysics/common.h"
#include "driver/ReductionSpec.h"
#include "physics/eos/HelmEos.h"
#include "physics/eos/IdealGas.h"
#include "physics/eos/Tabular3DEOS.h"
#include "physics/eos/Tabular4DEOS.h"

#include <cuda_runtime.h>

#include <cstddef>

namespace arch::cuda {

#define ARCH_DECLARE_BURN_LAUNCH(EOS) \
    cudaError_t launch_cuda_burn_route( \
        const dispatch::ResolvedExecutionPlan& plan, DeviceStateView state, \
        DeviceGridView grid, std::byte* workspace_storage, \
        reduction::ReductionCandidate* candidates, int* statuses, \
        DeviceBurnSummary* summary, double burn_dt, EOS eos, \
        BurnConfigView config, cudaStream_t stream)

ARCH_DECLARE_BURN_LAUNCH(IdealGasView);
ARCH_DECLARE_BURN_LAUNCH(HelmEosView);
ARCH_DECLARE_BURN_LAUNCH(Tabular3DEOSView);
ARCH_DECLARE_BURN_LAUNCH(Tabular4DEOSView);

#undef ARCH_DECLARE_BURN_LAUNCH

} // namespace arch::cuda
