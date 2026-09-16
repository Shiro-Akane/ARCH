/**
 * @file RefinementIndicators.h
 * @brief Borrowed device workspace and typed launches for shared AMR indicators.
 *
 * The runtime supplies state, selected fields, scratch arrays and a stream.
 * The launch produces one block error; its owner fences the stream and checks
 * EOS status before using that scalar to make host-side topology decisions.
 */

#pragma once

#include <algorithm>
#include <limits>
#include <span>
#include <stdexcept>
#include <type_traits>

#include "amr/RefinementIndicatorMath.h"
#include "cuda/common/CudaCommon.cuh"
#include "cuda/runtime/hydro/CudaBackendHydro.h"
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

struct DeviceIndicatorBatchBlock {
    DeviceStateView state;
    DeviceGridView grid;
    DeviceIndicatorWorkspace workspace;
};
static_assert(std::is_trivially_copyable_v<DeviceIndicatorBatchBlock>);

// Limit optional cross-block scratch, not the already-required single-block
// allocation. Layout/stencil ownership remains with the Host runtime.
inline std::size_t indicator_wave_capacity(std::size_t cells, int species,
                                          bool thermodynamics, std::size_t blocks)
{
    if (!cells || species < 0 || !blocks)
        throw std::invalid_argument("invalid indicator batch extent");
    const std::size_t fields = 1 + (thermodynamics ? 3 + static_cast<std::size_t>(species) : 0);
    if (cells > std::numeric_limits<std::size_t>::max() / fields / sizeof(double))
        throw std::overflow_error("indicator scratch extent overflow");
    constexpr std::size_t scratch_budget = 64 * 1024 * 1024;
    const auto capacity = std::max(std::size_t{1}, scratch_budget / (cells * fields * sizeof(double)));
    return std::min({blocks, std::size_t{1024}, capacity});
}

#define ARCH_DECLARE_REFINEMENT_INDICATORS(EOS) \
cudaError_t launch_cuda_refinement_indicators( \
    DeviceStateView state, DeviceGridView grid, EOS eos, \
    DeviceIndicatorWorkspace workspace, cudaStream_t stream)
ARCH_DECLARE_REFINEMENT_INDICATORS(IdealGasView);
ARCH_DECLARE_REFINEMENT_INDICATORS(HelmEosView);
ARCH_DECLARE_REFINEMENT_INDICATORS(Tabular3DEOSView);
ARCH_DECLARE_REFINEMENT_INDICATORS(Tabular4DEOSView);
#undef ARCH_DECLARE_REFINEMENT_INDICATORS

#define ARCH_DECLARE_BATCH_REFINEMENT_INDICATORS(EOS) \
CudaBackendLaunchResult launch_cuda_refinement_indicators_batch( \
    std::span<const DeviceIndicatorBatchBlock> host, const DeviceIndicatorBatchBlock* device, \
    std::size_t blocks_per_wave, EOS eos, cudaStream_t stream)
ARCH_DECLARE_BATCH_REFINEMENT_INDICATORS(IdealGasView);
ARCH_DECLARE_BATCH_REFINEMENT_INDICATORS(HelmEosView);
ARCH_DECLARE_BATCH_REFINEMENT_INDICATORS(Tabular3DEOSView);
ARCH_DECLARE_BATCH_REFINEMENT_INDICATORS(Tabular4DEOSView);
#undef ARCH_DECLARE_BATCH_REFINEMENT_INDICATORS

} // namespace arch::cuda
