/** Host-only ABI for a typed, backend-owned sparse burn pool. */
#pragma once

#include "cuda/runtime/CudaBackendTypes.h"
#include "cuda/common/CudaCommon.cuh"
#include "driver/ReductionSpec.h"
#include "driver/dispatch/PolicyDescriptor.h"
#include "data/GlobalDefs.h"
#include "physics/eos/eos.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace arch::cuda {
struct SparseBurnLaunchCounters
{
    std::uint64_t kernels = 0;
    std::uint64_t bytes_d2h = 0;
    std::uint64_t bytes_h2d = 0;
    std::uint64_t synchronizations = 0;
    std::uint64_t immutable_owner_constructions = 0;
};

class CudaSparseBurnOwner
{
public:
    virtual ~CudaSparseBurnOwner() = default;
    virtual SparseBurnLaunchCounters execute(
        DeviceStateView state, DeviceGridView grid, double dt, BurnConfigView config,
        reduction::ReductionCandidate* candidates, int* statuses, DeviceBurnSummary* summary) = 0;
    virtual std::size_t workspace_bytes_per_lane() const = 0;
    virtual int capacity() const = 0;
    // Initial immutable metadata/table uploads are not per-execute work.
    virtual SparseBurnLaunchCounters construction_counters() const = 0;
};

// max_cells bounds useful parallel work, not a fixed GPU batch size. The typed
// owner derives its actual capacity from per-lane storage and device resources.
// Generated per-network/EOS TUs delegate to make_sparse_burn_owner_for_network;
// a lightweight registry defines these overloads without duplicating physics.
#define ARCH_DECLARE_SPARSE_BURN_FACTORY(EOS) \
    std::unique_ptr<CudaSparseBurnOwner> make_cuda_sparse_burn_owner( \
        const dispatch::ResolvedExecutionPlan& plan, EOS eos, int max_cells, cudaStream_t stream)
ARCH_DECLARE_SPARSE_BURN_FACTORY(IdealGasView);
ARCH_DECLARE_SPARSE_BURN_FACTORY(HelmEosView);
ARCH_DECLARE_SPARSE_BURN_FACTORY(Tabular3DEOSView);
ARCH_DECLARE_SPARSE_BURN_FACTORY(Tabular4DEOSView);
#undef ARCH_DECLARE_SPARSE_BURN_FACTORY
} // namespace arch::cuda
