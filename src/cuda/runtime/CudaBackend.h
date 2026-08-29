/**
 * @file CudaBackend.h
 * @brief Ordinary-C++ declaration of the uniform multi-block CUDA backend.
 */

#pragma once

#include "cuda/common/CudaLaunchConfig.h"
#include "driver/ComputeBackend.h"

#include <memory>
#include <span>

struct IdealGas;
class HelmEos;
struct Tabular3DEOSHostView;
struct Tabular4DEOSHostView;
struct SpeciesManager;

namespace amr { struct Block; struct SameLevelExchangePlan; }
namespace arch::boundary { class BoundaryPlan; }

namespace arch::cuda {

struct CudaBlockBinding {
    const amr::Block* block = nullptr;
    amr::BlockHandle handle{};
    backend::StorageGeneration storage{};
    const boundary::BoundaryPlan* physical_boundary = nullptr;
};

class CudaBackend final : public backend::ComputeBackend {
public:
    struct Impl;

    explicit CudaBackend(std::unique_ptr<Impl> implementation);
    ~CudaBackend() override;

    CudaBackend(const CudaBackend&) = delete;
    CudaBackend& operator=(const CudaBackend&) = delete;
    CudaBackend(CudaBackend&&) = delete;
    CudaBackend& operator=(CudaBackend&&) = delete;

    state::ExecutionSide side() const noexcept override;
    amr::BlockHandle block_handle() const noexcept override;
    backend::StorageGeneration storage_generation() const noexcept override;
    bool contains(backend::BackendStateAccess access) const noexcept override;
    double compute_hydro_dt(backend::BackendStateAccess current,
                            double cfl) override;
    state::CompletionToken execute_hydro_stage(
        backend::BackendStateAccess current,
        const scheduler::StageDescriptor& descriptor,
        double dt, state::CompletionToken expected) override;
    state::CompletionToken execute_physical_boundary(
        backend::BackendStateAccess access, state::StateVersion version,
        state::CompletionToken expected) override;
    state::CompletionToken execute_same_level_exchange(
        std::span<const backend::BackendStateAccess> accesses,
        const amr::SameLevelExchangePlan& plan, state::StateSlot slot,
        state::StateVersion source_version,
        state::CompletionToken expected) override;
    void rotate_slots(backend::BackendStateAccess current,
                      state::SlotRotation rotation) override;
    double compute_diffusion_dt(
        backend::BackendStateAccess current) override;
    void copy_state_slot(backend::BackendStateAccess source,
                         backend::BackendStateAccess destination) override;
    state::CompletionToken execute_diffusion_stage(
        backend::BackendStateAccess current, const scheduler::RklPlan& plan,
        const scheduler::RklStageDescriptor& descriptor,
        double dt, double dt_fe,
        state::CompletionToken expected) override;
    backend::BurnExecutionResult execute_burn(
        backend::BackendStateAccess current, double dt,
        state::CompletionToken expected) override;
    void enqueue_materialize_host_current(
        backend::BackendStateAccess current, state::StateRegion region,
        backend::HostStateTransferView host) override;
    void enqueue_upload_slot(
        backend::BackendStateAccess access, state::StateRegion region,
        backend::HostStateTransferView host) override;
    void quiesce() override;
    backend::BackendCounters counters() const noexcept override;
    void append_trace(backend::BackendTraceRecord record) override;
    std::span<const backend::BackendTraceRecord>
    trace_snapshot() const noexcept override;

private:
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<CudaBackend> make_cuda_backend(
    const amr::Block& block, amr::BlockHandle handle,
    backend::StorageGeneration storage, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const boundary::BoundaryPlan& boundary, const IdealGas& eos);
std::unique_ptr<CudaBackend> make_cuda_backend(
    const amr::Block& block, amr::BlockHandle handle,
    backend::StorageGeneration storage, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const boundary::BoundaryPlan& boundary, const HelmEos& eos);
std::unique_ptr<CudaBackend> make_cuda_backend(
    const amr::Block& block, amr::BlockHandle handle,
    backend::StorageGeneration storage, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const boundary::BoundaryPlan& boundary,
    const Tabular3DEOSHostView& eos);
std::unique_ptr<CudaBackend> make_cuda_backend(
    const amr::Block& block, amr::BlockHandle handle,
    backend::StorageGeneration storage, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const boundary::BoundaryPlan& boundary,
    const Tabular4DEOSHostView& eos);

std::unique_ptr<CudaBackend> make_cuda_backend(
    std::span<const CudaBlockBinding> blocks, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const IdealGas& eos);
std::unique_ptr<CudaBackend> make_cuda_backend(
    std::span<const CudaBlockBinding> blocks, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const HelmEos& eos);
std::unique_ptr<CudaBackend> make_cuda_backend(
    std::span<const CudaBlockBinding> blocks, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const Tabular3DEOSHostView& eos);
std::unique_ptr<CudaBackend> make_cuda_backend(
    std::span<const CudaBlockBinding> blocks, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const Tabular4DEOSHostView& eos);

} // namespace arch::cuda
