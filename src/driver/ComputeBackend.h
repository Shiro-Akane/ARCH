/**
 * @file ComputeBackend.h
 * @brief Ordinary-C++ contract between the shared scheduler and compute backends.
 */

#pragma once

#include "amr/AmrTransferPlans.h"
#include "amr/BlockHandle.h"
#include "data/GlobalDefs.h"
#include "driver/StageScheduler.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>

namespace amr { struct AmrFluxTopologyPlan; struct Block; }
namespace arch::boundary { class BoundaryPlan; }

namespace arch::backend {

struct StorageGeneration {
    std::uint64_t value = 0;
    friend constexpr auto operator<=>(const StorageGeneration&,
                                      const StorageGeneration&) = default;
};

constexpr bool is_valid(StorageGeneration generation) noexcept
{
    return generation.value != 0;
}

class StorageGenerationIssuer {
public:
    explicit constexpr StorageGenerationIssuer(std::uint64_t next = 1)
        : next_(next)
    {
        if (next_ == 0)
            throw std::invalid_argument(
                "storage generation counter must be nonzero");
    }

    StorageGenerationIssuer(const StorageGenerationIssuer&) = delete;
    StorageGenerationIssuer& operator=(const StorageGenerationIssuer&) = delete;
    StorageGenerationIssuer(StorageGenerationIssuer&&) = delete;
    StorageGenerationIssuer& operator=(StorageGenerationIssuer&&) = delete;

    StorageGeneration issue()
    {
        if (next_ == 0)
            throw std::overflow_error("storage generation counter exhausted");
        const StorageGeneration result{next_};
        next_ = next_ == std::numeric_limits<std::uint64_t>::max()
            ? 0 : next_ + 1;
        return result;
    }

private:
    std::uint64_t next_;
};

struct BackendStateAccess {
    amr::BlockHandle block{};
    StorageGeneration storage{};
    state::StateSlot slot = state::StateSlot::Current;
};

/**
 * Backend-neutral description of one block in an unpublished topology.
 * Topology remains Host-owned; numerical reconstruction executes the shared
 * AMR leaves on the selected backend inside its private staged namespace.
 */
struct BackendTopologyBinding {
    const amr::Block* block = nullptr;
    amr::BlockHandle handle{};
    StorageGeneration storage{};
    const boundary::BoundaryPlan* physical_boundary = nullptr;
};

class BackendTopologyStoreTransaction {
public:
    BackendTopologyStoreTransaction() = default;
    virtual ~BackendTopologyStoreTransaction() = default;
    BackendTopologyStoreTransaction(
        const BackendTopologyStoreTransaction&) = delete;
    BackendTopologyStoreTransaction& operator=(
        const BackendTopologyStoreTransaction&) = delete;
    BackendTopologyStoreTransaction(
        BackendTopologyStoreTransaction&&) = delete;
    BackendTopologyStoreTransaction& operator=(
        BackendTopologyStoreTransaction&&) = delete;
};

struct HostStateTransferView {
    double* rho = nullptr;
    double* mom_u = nullptr;
    double* mom_v = nullptr;
    double* mom_w = nullptr;
    double* eng = nullptr;
    double* enuc_rate = nullptr;
    double* species = nullptr;
    std::size_t cell_count = 0;
    std::size_t species_count = 0;
    std::size_t species_stride = 0;
};

namespace detail {

struct TransferByteRange {
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
};

inline TransferByteRange checked_transfer_range(
    const double* pointer, std::size_t elements)
{
    if (pointer == nullptr || elements == 0)
        throw std::invalid_argument("Host transfer range is empty");
    if (elements > std::numeric_limits<std::size_t>::max() / sizeof(double))
        throw std::overflow_error("Host transfer byte extent overflow");
    const std::size_t bytes = elements * sizeof(double);
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(pointer);
    if (begin > std::numeric_limits<std::uintptr_t>::max() - bytes)
        throw std::overflow_error("Host transfer address range overflow");
    return {begin, begin + bytes};
}

inline bool transfer_ranges_overlap(
    TransferByteRange left, TransferByteRange right) noexcept
{
    return left.begin < right.end && right.begin < left.end;
}

} // namespace detail

inline void validate_host_state_transfer_view(
    const HostStateTransferView& view)
{
    if (view.cell_count == 0)
        throw std::invalid_argument("Host state transfer requires cells");
    if (view.rho == nullptr || view.mom_u == nullptr
        || view.mom_v == nullptr || view.mom_w == nullptr
        || view.eng == nullptr || view.enuc_rate == nullptr) {
        throw std::invalid_argument("Host hydro transfer pointer is null");
    }

    std::array<detail::TransferByteRange, 7> ranges{};
    const std::array<const double*, 6> hydro{
        view.rho, view.mom_u, view.mom_v, view.mom_w, view.eng,
        view.enuc_rate};
    for (std::size_t field = 0; field < hydro.size(); ++field)
        ranges[field] = detail::checked_transfer_range(
            hydro[field], view.cell_count);

    if (view.species_count == 0) {
        if (view.species != nullptr || view.species_stride != 0)
            throw std::invalid_argument(
                "zero-species transfer view is not canonical");
    } else {
        if (view.species == nullptr)
            throw std::invalid_argument("Host species transfer pointer is null");
        if (view.species_stride < view.cell_count)
            throw std::invalid_argument("Host species stride is undersized");
        const std::size_t preceding_species = view.species_count - 1;
        if (preceding_species
            > (std::numeric_limits<std::size_t>::max() - view.cell_count)
                / view.species_stride) {
            throw std::overflow_error("Host species transfer extent overflow");
        }
        const std::size_t species_elements =
            preceding_species * view.species_stride + view.cell_count;
        ranges[6] = detail::checked_transfer_range(
            view.species, species_elements);
    }


    const std::size_t range_count = view.species_count == 0 ? 6 : 7;
    for (std::size_t left = 0; left < range_count; ++left) {
        for (std::size_t right = left + 1; right < range_count; ++right) {
            if (detail::transfer_ranges_overlap(ranges[left], ranges[right]))
                throw std::invalid_argument("Host transfer fields alias");
        }
    }
}

struct BurnExecutionResult {
    double dt_recommended = 0.0;
    std::uint64_t failed_cells = 0;
    int status = 0;
    state::CompletionToken completion{};
};

struct BackendCounters {
    std::uint64_t kernel_count = 0;
    std::uint64_t bytes_h2d = 0;
    std::uint64_t bytes_d2h = 0;
    std::uint64_t stream_sync_count = 0;
    std::uint64_t getter_count = 0;
    friend constexpr auto operator<=>(const BackendCounters&,
                                      const BackendCounters&) = default;
};

enum class BackendOperation : std::uint8_t {
    InitialUpload,
    Upload,
    Materialize,
    PhysicalBoundary,
    HydroStage,
    SlotRotation,
    DiffusionCopy,
    DiffusionStage,
    Burn,
};

struct BackendTraceRecord {
    std::uint64_t macro_step = 0;
    BackendOperation operation = BackendOperation::InitialUpload;
    amr::BlockHandle block{};
    StorageGeneration storage{};
    state::StateSlot slot = state::StateSlot::Current;
    state::SlotCoherence coherence{};
    std::uint64_t bytes_h2d = 0;
    std::uint64_t bytes_d2h = 0;
    std::uint64_t kernel_count = 0;
    std::uint64_t stream_sync_count = 0;
};

class ComputeBackend {
public:
    ComputeBackend() = default;
    virtual ~ComputeBackend() = default;
    ComputeBackend(const ComputeBackend&) = delete;
    ComputeBackend& operator=(const ComputeBackend&) = delete;
    ComputeBackend(ComputeBackend&&) = delete;
    ComputeBackend& operator=(ComputeBackend&&) = delete;

    virtual state::ExecutionSide side() const noexcept = 0;
    virtual amr::BlockHandle block_handle() const noexcept = 0;
    virtual StorageGeneration storage_generation() const noexcept = 0;
    virtual bool contains(BackendStateAccess access) const noexcept = 0;

    virtual double compute_hydro_dt(BackendStateAccess current,
                                    double cfl) = 0;
    virtual state::CompletionToken execute_hydro_stage(
        BackendStateAccess current,
        const scheduler::StageDescriptor& descriptor,
        double dt, state::CompletionToken expected) = 0;
    virtual state::CompletionToken execute_physical_boundary(
        BackendStateAccess access, state::StateVersion version,
        state::CompletionToken expected) = 0;
    virtual state::CompletionToken execute_same_level_exchange(
        std::span<const BackendStateAccess> accesses,
        const amr::SameLevelExchangePlan& plan, state::StateSlot slot,
        state::StateVersion source_version,
        state::CompletionToken expected) = 0;
    virtual state::CompletionToken execute_coarse_fine_exchange(
        std::span<const BackendStateAccess>,
        const amr::CoarseFineTransferPlan&, state::StateSlot,
        state::StateVersion, state::CompletionToken)
    {
        throw std::logic_error(
            "backend coarse-fine exchange is unavailable");
    }
    virtual void rotate_slots(BackendStateAccess current,
                              state::SlotRotation rotation) = 0;
    virtual double compute_diffusion_dt(BackendStateAccess current) = 0;
    virtual void copy_state_slot(BackendStateAccess source,
                                 BackendStateAccess destination) = 0;
    virtual state::CompletionToken execute_diffusion_stage(
        BackendStateAccess current, const scheduler::RklPlan& plan,
        const scheduler::RklStageDescriptor& descriptor,
        double dt, double dt_fe, state::CompletionToken expected) = 0;
    virtual BurnExecutionResult execute_burn(
        BackendStateAccess current, double dt,
        state::CompletionToken expected) = 0;
    virtual void enqueue_materialize_host_current(
        BackendStateAccess current, state::StateRegion region,
        HostStateTransferView host) = 0;
    virtual void enqueue_upload_slot(
        BackendStateAccess access, state::StateRegion region,
        HostStateTransferView host) = 0;
    virtual bool supports_dynamic_topology_store() const noexcept
    {
        return false;
    }
    virtual std::vector<double> evaluate_refinement_indicators(
        std::span<const BackendStateAccess>, const AmrConfig&, double,
        std::span<const int>)
    {
        throw std::logic_error("backend AMR indicators are unavailable");
    }
    virtual std::unique_ptr<BackendTopologyStoreTransaction>
    begin_topology_store_transaction(
        const amr::AmrPlanScope&,
        std::span<const BackendTopologyBinding>)
    {
        throw std::logic_error(
            "backend dynamic topology store is unavailable");
    }
    virtual void enqueue_upload_staged_current(
        BackendTopologyStoreTransaction&, BackendStateAccess,
        state::StateRegion, HostStateTransferView)
    {
        throw std::logic_error(
            "backend staged Current upload is unavailable");
    }
    /** Reconstruct staged interiors from immutable old-device sources. */
    virtual void migrate_staged_current(
        BackendTopologyStoreTransaction&,
        std::span<const BackendStateAccess>, const amr::ProlongationPlan&,
        const amr::RestrictionPlan&)
    {
        throw std::logic_error("backend device regrid migration is unavailable");
    }
    /** Complete physical and shared logical ghost plans in the staged store. */
    virtual void complete_staged_current_ghosts(
        BackendTopologyStoreTransaction&,
        std::span<const amr::SameLevelExchangePlan>,
        const amr::CoarseFineTransferPlan&)
    {
        throw std::logic_error("backend staged device ghosts are unavailable");
    }
    /** Prepare all throwing AMR flux allocations for the active topology. */
    virtual void prepare_amr_flux_plan(
        const amr::AmrFluxTopologyPlan&, const amr::RefluxPlan&)
    {
        throw std::logic_error("backend AMR flux plans are unavailable");
    }
    /** Stage the next epoch's AMR flux resources before store publication. */
    virtual void stage_amr_flux_plan(
        BackendTopologyStoreTransaction&, const amr::AmrFluxTopologyPlan&,
        const amr::RefluxPlan&)
    {
        throw std::logic_error(
            "backend staged AMR flux plans are unavailable");
    }
    virtual state::CompletionToken clear_amr_flux_register(
        state::CompletionToken expected)
    {
        throw std::logic_error("backend AMR flux clear is unavailable");
    }
    virtual state::CompletionToken execute_amr_reflux(
        state::StateSlot, double, state::CompletionToken)
    {
        throw std::logic_error("backend AMR reflux is unavailable");
    }
    virtual void publish_topology_store_transaction(
        std::unique_ptr<BackendTopologyStoreTransaction>)
    {
        throw std::logic_error(
            "backend topology store publication is unavailable");
    }
    virtual void quiesce() = 0;
    virtual BackendCounters counters() const noexcept = 0;
    virtual void append_trace(BackendTraceRecord record) = 0;
    virtual std::span<const BackendTraceRecord> trace_snapshot() const noexcept = 0;
};

inline state::CompletionToken transfer_state_regions(
    ComputeBackend& backend, state::StateResidencyLedger& ledger,
    scheduler::MonotonicSchedulerClock& clock, BackendStateAccess access,
    HostStateTransferView host, state::PendingTransferPhase direction,
    std::uint64_t macro_step = 0,
    BackendOperation operation = BackendOperation::InitialUpload)
{
    if (backend.side() != state::ExecutionSide::Device
        || !backend.contains(access)) {
        throw std::invalid_argument("transfer targets a stale backend access");
    }
    if (direction != state::PendingTransferPhase::PendingH2D
        && direction != state::PendingTransferPhase::PendingD2H) {
        throw std::invalid_argument("invalid backend transfer direction");
    }
    validate_host_state_transfer_view(host);

    const state::StateKey key{access.block, access.slot};
    const state::SlotCoherence before = ledger.inspect(key);
    const auto require_source = [&](const state::RegionCoherence& region) {
        const state::StateResidency expected =
            direction == state::PendingTransferPhase::PendingH2D
            ? state::StateResidency::HostValid
            : state::StateResidency::DeviceValid;
        if (region.residency != expected
            || region.pending_transfer
                != state::PendingTransferPhase::None
            || !state::is_complete(region.completion)
            || !state::is_valid(region.version)) {
            throw std::logic_error("backend transfer source is not readable");
        }
    };
    require_source(before.interior);
    require_source(before.ghost);
    if (before.ghost.version != before.interior.version
        || before.ghost_source_version != before.interior.version) {
        throw std::logic_error("backend transfer ghost is stale");
    }

    if (clock.last_token() == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("scheduler completion token exhausted");
    const std::uint64_t proposed_token = clock.last_token() + 1;
    if (proposed_token <= before.interior.completion.value
        || proposed_token <= before.ghost.completion.value) {
        throw std::logic_error(
            "backend transfer token does not exceed region history");
    }
    const state::CompletionToken completed = clock.next_completion();
    const state::CompletionToken pending{
        completed.value, state::CompletionState::Pending};
    const BackendCounters counters_before = backend.counters();
    constexpr std::array<state::StateRegion, 2> regions{
        state::StateRegion::Interior, state::StateRegion::Ghost};
    for (const auto region : regions)
        ledger.begin_transfer(key, region, direction, pending);

    for (const auto region : regions) {
        if (direction == state::PendingTransferPhase::PendingH2D)
            backend.enqueue_upload_slot(access, region, host);
        else
            backend.enqueue_materialize_host_current(access, region, host);
    }
    backend.quiesce();
    for (const auto region : regions)
        ledger.complete_transfer(key, region, completed);
    const BackendCounters counters_after = backend.counters();
    backend.append_trace({
        macro_step,
        operation,
        access.block,
        access.storage,
        access.slot,
        ledger.inspect(key),
        counters_after.bytes_h2d - counters_before.bytes_h2d,
        counters_after.bytes_d2h - counters_before.bytes_d2h,
        counters_after.kernel_count - counters_before.kernel_count,
        counters_after.stream_sync_count - counters_before.stream_sync_count});
    return completed;
}

static_assert(std::is_trivially_copyable_v<StorageGeneration>);
static_assert(std::is_trivially_copyable_v<BackendStateAccess>);
static_assert(std::is_trivially_copyable_v<BackendTopologyBinding>);
static_assert(std::is_trivially_copyable_v<HostStateTransferView>);
static_assert(std::is_trivially_copyable_v<BackendTraceRecord>);

} // namespace arch::backend
