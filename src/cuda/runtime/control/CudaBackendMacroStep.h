/**
 * @file CudaBackendMacroStep.h
 * @brief Launch the existing physical/EOS acceptance on resident Current cells.
 */
#pragma once
#include "cuda/common/CudaCommon.cuh"
#include "numerics/state/StateAdmissibility.h"
#include "physics/eos/eos.h"

namespace arch::cuda {
#define ARCH_DECLARE_CURRENT_VALIDATION(EOS) \
    cudaError_t launch_cuda_validate_current(DeviceStateView, DeviceGridView, EOS, \
        state::Bounds, SpeciesWorkspaceView, int*, cudaStream_t)
ARCH_DECLARE_CURRENT_VALIDATION(IdealGasView);
ARCH_DECLARE_CURRENT_VALIDATION(HelmEosView);
ARCH_DECLARE_CURRENT_VALIDATION(Tabular3DEOSView);
ARCH_DECLARE_CURRENT_VALIDATION(Tabular4DEOSView);
#undef ARCH_DECLARE_CURRENT_VALIDATION
} // namespace arch::cuda
