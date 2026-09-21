/**
 * @file CudaBackendBurn.h
 * @brief Narrow runtime interface to typed CUDA burn instantiations.
 *
 * Launches borrow state, EOS views, scratch arrays and the runtime stream.
 * The optional owner slot retains immutable network tables across calls;
 * successful enqueueing does not make the device summary host-visible.
 */

#pragma once

#include "cuda/runtime/CudaBackendTypes.h"
#include "cuda/common/CudaCommon.cuh"
#include "data/GlobalDefs.h"
#include "numerics/state/StateAdmissibility.h"
#include "driver/dispatch/capability/ResolvedExecutionPlan.h"
#include "driver/schedule/ReductionSpec.h"
#include "physics/eos/eos.h"

#include <cuda_runtime.h>

#include <cstddef>
#include <memory>
#include <span>
#include <type_traits>

namespace arch::cuda {

class DeviceNetworkOwner;
// Borrowed block-local storage, lowered anew after every slot/topology change.
// A wave shares immutable EOS/network data, never a cell's mutable workspace.
struct DeviceBurnBatchBlock {
    DeviceStateView state;
    DeviceGridView grid;
    std::byte* workspace_storage;
    reduction::ReductionCandidate* candidates;
    int* statuses;
    DeviceBurnSummary* summary;
    state::Bounds bounds{};
};
static_assert(std::is_trivially_copyable_v<DeviceBurnBatchBlock>);
inline constexpr std::size_t BURN_BATCH_WAVE_LIMIT = 1024;
// Host-only launch request. The typed factory may populate the persistent
// backend-owned slot once; kernels receive only numeric controls and a bound
// immutable network value. Stateless networks need no owner slot.
struct CudaBurnArguments {
    BurnConfigView controls;
    std::unique_ptr<DeviceNetworkOwner>* network_owner = nullptr;
    std::span<const DeviceBurnBatchBlock> host_blocks{};
    const DeviceBurnBatchBlock* device_blocks = nullptr;
    CudaBurnArguments(BurnConfigView config,
        std::unique_ptr<DeviceNetworkOwner>* owner = nullptr,
        std::span<const DeviceBurnBatchBlock> host = {},
        const DeviceBurnBatchBlock* device = nullptr)
        : controls(config), network_owner(owner), host_blocks(host), device_blocks(device) {}
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
