/**
 * @file RefinementIndicators.h
 * @brief Borrowed device workspace and typed launches for shared AMR indicators.
 *
 * The runtime supplies state, selected fields, scratch arrays and a stream.
 * The launch produces one block error; its owner fences the stream and checks
 * EOS status before using that scalar to make host-side topology decisions.
 */

#pragma once

#include "amr/RefinementIndicatorMath.h"
#include "cuda/common/CudaCommon.cuh"
#include "physics/eos/IdealGas.h"
#include "physics/eos/HelmEos.h"
#include "physics/eos/Tabular3DEOS.h"
#include "physics/eos/Tabular4DEOS.h"

namespace arch::cuda {

struct DeviceIndicatorWorkspace {
    const amr::indicator::Selection* selection = nullptr;
    int selection_count = 0;
    double density_floor = 0.0;
    bool pressure = false, temperature = false, gamma1 = false;
    double* thermodynamics = nullptr;
    double* composition = nullptr;
    double* cell_errors = nullptr;
    double* block_error = nullptr;
    int* eos_status = nullptr;
};

#define ARCH_DECLARE_REFINEMENT_INDICATORS(EOS) \
cudaError_t launch_cuda_refinement_indicators( \
    DeviceStateView state, DeviceGridView grid, EOS eos, \
    DeviceIndicatorWorkspace workspace, cudaStream_t stream)
ARCH_DECLARE_REFINEMENT_INDICATORS(IdealGasView);
ARCH_DECLARE_REFINEMENT_INDICATORS(HelmEosView);
ARCH_DECLARE_REFINEMENT_INDICATORS(Tabular3DEOSView);
ARCH_DECLARE_REFINEMENT_INDICATORS(Tabular4DEOSView);
#undef ARCH_DECLARE_REFINEMENT_INDICATORS

} // namespace arch::cuda
