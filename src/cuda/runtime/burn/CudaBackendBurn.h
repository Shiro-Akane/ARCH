/**
 * @file CudaBackendBurn.h
 * @brief Narrow runtime ABI between the CUDA backend and burn instantiations.
 */

#pragma once

#include "cuda/runtime/CudaBackendTypes.h"
#include "cuda/common/CudaCommon.cuh"
#include "data/GlobalDefs.h"
#include "driver/dispatch/ResolvedExecutionPlan.h"
#include "driver/ReductionSpec.h"
#include "physics/eos/eos.h"

#include <cuda_runtime.h>

#include <cstddef>
#include <memory>

namespace arch::cuda {

class DeviceNetworkOwner;
// Host-only launch request. The typed factory may populate the persistent
// backend-owned slot once; kernels receive only numeric controls and a bound
// immutable network value. Stateless networks need no owner slot.
struct CudaBurnArguments {
    BurnConfigView controls;
    std::unique_ptr<DeviceNetworkOwner>* network_owner = nullptr;
    CudaBurnArguments(BurnConfigView config,
        std::unique_ptr<DeviceNetworkOwner>* owner = nullptr)
        : controls(config), network_owner(owner) {}
};

#define ARCH_DECLARE_BURN_LAUNCH(EOS) \
    cudaError_t launch_cuda_burn_route( \
        const dispatch::ResolvedExecutionPlan& plan, DeviceStateView state, \
        DeviceGridView grid, std::byte* workspace_storage, \
        reduction::ReductionCandidate* candidates, int* statuses, \
        DeviceBurnSummary* summary, double burn_dt, EOS eos, \
        CudaBurnArguments config, cudaStream_t stream)

ARCH_DECLARE_BURN_LAUNCH(IdealGasView);
ARCH_DECLARE_BURN_LAUNCH(HelmEosView);
ARCH_DECLARE_BURN_LAUNCH(Tabular3DEOSView);
ARCH_DECLARE_BURN_LAUNCH(Tabular4DEOSView);

#undef ARCH_DECLARE_BURN_LAUNCH

} // namespace arch::cuda
