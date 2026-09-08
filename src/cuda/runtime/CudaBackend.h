/**
 * @file CudaBackend.h
 * @brief Ordinary-C++ declaration of the uniform multi-block CUDA backend.
 */

#pragma once

#include "cuda/common/CudaLaunchConfig.h"
#include "cuda/runtime/DeviceBlockStore.h"
#include "driver/ComputeBackend.h"

#include <cstdint>
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

/**
 * @brief Diagnostic snapshot of the CUDA allocation namespace.
 *
 * This reports storage-lifecycle state only.  It is deliberately not an AMR
 * capability signal.
 */
struct CudaStoreSnapshot {
    std::uint64_t active_blocks = 0;
    std::uint64_t staged_blocks = 0;
    std::uint64_t retirement_batches = 0;
    std::uint64_t bytes_h2d = 0;
    std::uint64_t immutable_owner_constructions = 0;
};

class CudaBackend final : public backend::ComputeBackend {
public:
    struct Impl;

    /**
     * @brief Move-only owner of one unpublished device block namespace.
     *
     * Destruction synchronizes and aborts an unconsumed transaction.  The
     * transaction retains the backend implementation so it cannot dangle if
     * the public CudaBackend owner is destroyed first.  Device selection or
     * synchronization failure is fail-fast: staged allocations are never
     * released without a successful quiescence witness.
     */
    class StoreTransaction {
    public:
        struct Impl;

        ~StoreTransaction();
        StoreTransaction(const StoreTransaction&) = delete;
        StoreTransaction& operator=(const StoreTransaction&) = delete;
        StoreTransaction(StoreTransaction&&) noexcept;
        StoreTransaction& operator=(StoreTransaction&&) = delete;

        std::span<const DeviceStoreEntry> entries() const;
        amr::AmrPlanScope scope() const;

    private:
        friend class CudaBackend;
        explicit StoreTransaction(std::unique_ptr<Impl> implementation);
        std::unique_ptr<Impl> impl_;
    };

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
    state::CompletionToken execute_coarse_fine_exchange(
        std::span<const backend::BackendStateAccess> accesses,
        const amr::CoarseFineTransferPlan& plan, state::StateSlot slot,
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
    bool supports_dynamic_topology_store() const noexcept override;
    std::unique_ptr<backend::BackendTopologyStoreTransaction>
    begin_topology_store_transaction(
        const amr::AmrPlanScope& scope,
        std::span<const backend::BackendTopologyBinding> bindings) override;
    void enqueue_upload_staged_current(
        backend::BackendTopologyStoreTransaction& transaction,
        backend::BackendStateAccess access, state::StateRegion region,
        backend::HostStateTransferView host) override;
    void prepare_amr_flux_plan(
        const amr::AmrFluxTopologyPlan& topology,
        const amr::RefluxPlan& reflux) override;
    void migrate_staged_current(
        backend::BackendTopologyStoreTransaction&,
        std::span<const backend::BackendStateAccess>,
        const amr::ProlongationPlan&, const amr::RestrictionPlan&) override;
    void complete_staged_current_ghosts(
        backend::BackendTopologyStoreTransaction&,
        std::span<const amr::SameLevelExchangePlan>,
        const amr::CoarseFineTransferPlan&) override;
    void stage_amr_flux_plan(
        backend::BackendTopologyStoreTransaction& transaction,
        const amr::AmrFluxTopologyPlan& topology,
        const amr::RefluxPlan& reflux) override;
    state::CompletionToken clear_amr_flux_register(
        state::CompletionToken expected) override;
    state::CompletionToken execute_amr_reflux(
        state::StateSlot slot, double dt,
        state::CompletionToken expected) override;
    void publish_topology_store_transaction(
        std::unique_ptr<backend::BackendTopologyStoreTransaction>
            transaction) override;
    void quiesce() override;
    backend::BackendCounters counters() const noexcept override;
    void append_trace(backend::BackendTraceRecord record) override;
    std::span<const backend::BackendTraceRecord>
    trace_snapshot() const noexcept override;

    std::vector<double> evaluate_refinement_indicators(
        std::span<const backend::BackendStateAccess>, const AmrConfig&, double,
        std::span<const int>) override;

    StoreTransaction begin_store_transaction(
        amr::AmrPlanScope scope,
        std::span<const CudaBlockBinding> bindings);
    bool contains_migration(
        const StoreTransaction& transaction,
        DeviceMigrationAccess access) const noexcept;
    void enqueue_upload_staged_current(
        StoreTransaction& transaction, DeviceMigrationAccess access,
        state::StateRegion region, backend::HostStateTransferView host);
    void migrate_staged_current(
        StoreTransaction&, std::span<const backend::BackendStateAccess>,
        const amr::ProlongationPlan&, const amr::RestrictionPlan&);
    void complete_staged_current_ghosts(
        StoreTransaction&, std::span<const amr::SameLevelExchangePlan>,
        const amr::CoarseFineTransferPlan&);
    void abort_store_transaction(StoreTransaction&& transaction);
    void publish_store_transaction(
        StoreTransaction&& transaction, DeviceRetirementFence fence);
    bool retirement_ready(DeviceRetirementFence fence) const;
    void complete_store_retirement(DeviceRetirementFence fence);
    CudaStoreSnapshot store_snapshot() const noexcept;

    // Dynamic AMR keeps topology/Morton authority on the Host. Numerical
    // migration and ghosts use shared mathematical leaves on device storage.
    static constexpr bool cuda_amr_execution_available() noexcept
    {
        return true;
    }

private:
    std::shared_ptr<Impl> impl_;
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
