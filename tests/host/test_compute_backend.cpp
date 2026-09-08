/**
 * @file test_compute_backend.cpp
 * @brief Test backend-neutral storage and transfer contracts.
 *
 * Mock-backed cases check generation ownership, device-store identities,
 * transfer completion and topology rejection without requiring a CUDA device.
 */
#include "driver/ComputeBackend.h"
#include "cuda/common/CudaLaunchConfig.h"
#include "cuda/runtime/DeviceBlockStore.h"
#include "amr/ExchangePlan.h"
#include "amr/AmrFluxPlan.h"

#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(std::is_same_v<
              decltype(std::declval<const arch::backend::ComputeBackend&>()
                           .trace_snapshot()),
              std::span<const arch::backend::BackendTraceRecord>>,
              "backend trace getter must be allocation-free");

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template <class Function>
void require_failure(Function&& function, const char* message)
{
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

void test_generation_authority()
{
    arch::backend::StorageGenerationIssuer issuer;
    const auto first = issuer.issue();
    const auto second = issuer.issue();
    require(first.value == 1 && second.value == 2,
            "storage generations are not monotonic");
    require_failure(
        [] {
            arch::backend::StorageGenerationIssuer exhausted(
                std::numeric_limits<std::uint64_t>::max());
            require(exhausted.issue().value
                        == std::numeric_limits<std::uint64_t>::max(),
                    "last storage generation changed");
            (void)exhausted.issue();
        },
        "storage generation exhaustion wrapped");
}

void test_device_block_store_identity()
{
    using arch::backend::StorageGeneration;
    using arch::cuda::DeviceBlockRecord;
    using arch::cuda::DeviceBlockStoreIndex;
    const std::array records{
        DeviceBlockRecord{amr::BlockHandle{{41}, {7}},
                          StorageGeneration{101}, {1}},
        DeviceBlockRecord{amr::BlockHandle{{42}, {7}},
                          StorageGeneration{102}, {2}}};
    const DeviceBlockStoreIndex store(records);
    require(store.size() == 2
                && store.index_of({records[0].handle, records[0].storage,
                                   arch::state::StateSlot::Current}) == 0
                && store.index_of({records[1].handle, records[1].storage,
                                   arch::state::StateSlot::Scratch}) == 1,
            "device block store identity lowering drifted");
    require(!store.contains({records[0].handle, StorageGeneration{999},
                             arch::state::StateSlot::Current}),
            "stale storage generation accepted");
    require_failure(
        [&] {
            (void)store.index_of({
                amr::BlockHandle{{41}, {8}}, records[0].storage,
                arch::state::StateSlot::Current});
        },
        "stale topology epoch accepted by device block store");
    require_failure(
        [&] {
            const std::array duplicate{
                records[0], DeviceBlockRecord{
                    records[0].handle, StorageGeneration{103}, {3}}};
            (void)DeviceBlockStoreIndex(duplicate);
        },
        "duplicate BlockHandle accepted by device block store");
}

void test_host_transfer_view()
{
    std::array<double, 8> rho{}, mom_u{}, mom_v{}, mom_w{}, eng{}, enuc{};
    arch::backend::HostStateTransferView empty_species{
        rho.data(), mom_u.data(), mom_v.data(), mom_w.data(), eng.data(),
        enuc.data(), nullptr, rho.size(), 0, 0};
    arch::backend::validate_host_state_transfer_view(empty_species);

    std::array<double, 16> species{};
    auto populated = empty_species;
    populated.species = species.data();
    populated.species_count = 2;
    populated.species_stride = rho.size();
    arch::backend::validate_host_state_transfer_view(populated);

    auto invalid = empty_species;
    invalid.species_stride = 1;
    require_failure(
        [&] { arch::backend::validate_host_state_transfer_view(invalid); },
        "noncanonical zero-species view accepted");
    invalid = populated;
    invalid.species = nullptr;
    require_failure(
        [&] { arch::backend::validate_host_state_transfer_view(invalid); },
        "positive species count accepted a null pointer");
    invalid = populated;
    invalid.species_stride = rho.size() - 1;
    require_failure(
        [&] { arch::backend::validate_host_state_transfer_view(invalid); },
        "undersized species stride accepted");
    invalid = populated;
    invalid.species_count = std::numeric_limits<std::size_t>::max();
    require_failure(
        [&] { arch::backend::validate_host_state_transfer_view(invalid); },
        "species extent overflow accepted");

    invalid = populated;
    invalid.mom_u = invalid.rho;
    require_failure(
        [&] { arch::backend::validate_host_state_transfer_view(invalid); },
        "aliased hydro transfer fields accepted");
    invalid = populated;
    invalid.species = invalid.eng;
    require_failure(
        [&] { arch::backend::validate_host_state_transfer_view(invalid); },
        "species transfer aliases a hydro field");
}

void test_cuda_launch_config()
{
    SimConfig config{};
    config.numerics.sml_rho = 3.25e-17;
    config.numerics.max_eint = 7.5e19;
    config.numerics.cfl = 0.4375;
    config.numerics.entropy_fix_coeff = 0.03125;
    config.physics.burn.use_burn = true;
    config.physics.burn.nuclearTempMin = 2.5e8;
    config.physics.burn.enucDtFactor = 17.0;
    config.physics.diffusion.use_diffusion = true;
    config.physics.diffusion.use_thermal_diffusion = true;
    config.physics.diffusion.nu_visc = 0.125;
    config.physics.diffusion.diff_cfl = 0.625;
    config.physics.diffusion.max_stages = 73;

    const arch::dispatch::ResolvedExecutionPlan plan{
        arch::dispatch::FluxId::Roe,
        arch::dispatch::ReconstructionId::Muscl,
        arch::dispatch::LimiterId::VanLeer,
        arch::dispatch::TimeIntegratorId::Rk3,
        arch::dispatch::EosId::Helmholtz,
        arch::dispatch::NetworkId::Aprox21,
        arch::dispatch::OdeSolverId::Ros4,
        arch::dispatch::LinearSolverId::DenseLu,
        arch::dispatch::DiffusionIntegratorId::Rkl2};
    const auto lowered = arch::cuda::make_cuda_launch_config(plan, config);
    require(lowered.plan.flux == plan.flux
                && lowered.plan.reconstruction == plan.reconstruction
                && lowered.plan.limiter == plan.limiter
                && lowered.plan.time_integrator == plan.time_integrator
                && lowered.plan.eos == plan.eos
                && lowered.plan.network == plan.network
                && lowered.plan.ode_solver == plan.ode_solver
                && lowered.plan.linear_solver == plan.linear_solver
                && lowered.plan.diffusion_integrator
                    == plan.diffusion_integrator,
            "CUDA launch policy IDs drifted");
    require(lowered.burn.use_burn
                && lowered.burn.nuclearTempMin == 2.5e8
                && lowered.burn.enucDtFactor == 17.0,
            "CUDA burn controls drifted");
    require(lowered.diffusion.use_diffusion
                && lowered.diffusion.use_thermal_diffusion
                && lowered.diffusion.nu_visc == 0.125,
            "CUDA diffusion controls drifted");
    require(lowered.density_floor == 3.25e-17
                && lowered.minimum_internal_energy == config.numerics.min_eint
                && lowered.maximum_internal_energy == 7.5e19
                && lowered.cfl == 0.4375
                && lowered.entropy_fix_coefficient == 0.03125
                && lowered.diffusion_cfl == 0.625
                && lowered.diffusion_max_stages == 73,
            "CUDA numeric launch controls drifted");
}

class FakeBackend final : public arch::backend::ComputeBackend {
public:
    arch::state::ExecutionSide side() const noexcept override
    {
        return arch::state::ExecutionSide::Device;
    }
    amr::BlockHandle block_handle() const noexcept override
    {
        return block;
    }
    arch::backend::StorageGeneration storage_generation() const noexcept override
    {
        return storage;
    }
    bool contains(
        arch::backend::BackendStateAccess access) const noexcept override
    {
        return (access.block == block && access.storage == storage)
            || (access.block == second_block
                && access.storage == second_storage);
    }
    double compute_hydro_dt(arch::backend::BackendStateAccess, double) override
    {
        return 1.0;
    }
    arch::state::CompletionToken execute_hydro_stage(
        arch::backend::BackendStateAccess,
        const arch::scheduler::StageDescriptor&, double,
        arch::state::CompletionToken token) override { return token; }
    arch::state::CompletionToken execute_physical_boundary(
        arch::backend::BackendStateAccess, arch::state::StateVersion,
        arch::state::CompletionToken token) override { return token; }
    void rotate_slots(arch::backend::BackendStateAccess,
                      arch::state::SlotRotation) override {}
    double compute_diffusion_dt(arch::backend::BackendStateAccess) override
    {
        return 1.0;
    }

    arch::state::CompletionToken execute_same_level_exchange(
        std::span<const arch::backend::BackendStateAccess> accesses,
        const amr::SameLevelExchangePlan&, arch::state::StateSlot slot,
        arch::state::StateVersion source_version,
        arch::state::CompletionToken expected) override
    {
        if (!arch::state::is_valid(source_version)
            || !arch::state::is_complete(expected))
            throw std::invalid_argument("invalid fake exchange contract");
        for (const auto& access : accesses) {
            if (!contains(access) || access.slot != slot)
                throw std::invalid_argument("stale fake exchange access");
        }
        ++counters_value.kernel_count;
        return expected;
    }
    void copy_state_slot(arch::backend::BackendStateAccess,
                         arch::backend::BackendStateAccess) override {}
    arch::state::CompletionToken execute_diffusion_stage(
        arch::backend::BackendStateAccess, const arch::scheduler::RklPlan&,
        const arch::scheduler::RklStageDescriptor&, double, double,
        arch::state::CompletionToken token) override { return token; }
    arch::backend::BurnExecutionResult execute_burn(
        arch::backend::BackendStateAccess, double dt,
        arch::state::CompletionToken token) override
    {
        return {dt, 0, 0, token};
    }
    void enqueue_materialize_host_current(
        arch::backend::BackendStateAccess, arch::state::StateRegion region,
        arch::backend::HostStateTransferView) override
    {
        record(region, false);
    }
    void enqueue_upload_slot(
        arch::backend::BackendStateAccess, arch::state::StateRegion region,
        arch::backend::HostStateTransferView) override
    {
        record(region, true);
    }
    void quiesce() override
    {
        calls.push_back(30);
        if (fail_quiesce) throw std::runtime_error("injected quiesce failure");
        ++counters_value.stream_sync_count;
    }
    arch::backend::BackendCounters counters() const noexcept override
    {
        ++getter_count;
        auto result = counters_value;
        result.getter_count = getter_count;
        return result;
    }
    void append_trace(arch::backend::BackendTraceRecord record) override
    {
        if (fail_trace) throw std::runtime_error("injected trace failure");
        trace.push_back(record);
    }
    std::span<const arch::backend::BackendTraceRecord>
    trace_snapshot() const noexcept override
    {
        ++getter_count;
        return trace;
    }

    void record(arch::state::StateRegion region, bool upload)
    {
        calls.push_back((upload ? 10 : 20)
                        + static_cast<int>(region));
        ++enqueue_count;
        if (fail_enqueue == enqueue_count)
            throw std::runtime_error("injected enqueue failure");
        if (upload)
            counters_value.bytes_h2d += 64;
        else
            counters_value.bytes_d2h += 64;
    }

    amr::BlockHandle block{{7}, {3}};
    arch::backend::StorageGeneration storage{9};
    amr::BlockHandle second_block{{8}, {3}};
    arch::backend::StorageGeneration second_storage{10};
    std::vector<int> calls;
    int enqueue_count = 0;
    int fail_enqueue = 0;
    bool fail_quiesce = false;
    bool fail_trace = false;
    mutable std::uint64_t getter_count = 0;
    arch::backend::BackendCounters counters_value{};
    std::vector<arch::backend::BackendTraceRecord> trace;
};

void test_transfer_transaction()
{
    using namespace arch::state;
    FakeBackend backend;
    StateResidencyLedger ledger({3});
    const StateKey key{backend.block, StateSlot::Current};
    ledger.register_block(
        backend.block, {1}, {1, CompletionState::Complete});
    ledger.publish_ghost(
        key, ExecutionSide::Host, {1}, {2, CompletionState::Complete});
    arch::scheduler::MonotonicSchedulerClock clock(2, 1);

    std::array<double, 8> rho{}, mom_u{}, mom_v{}, mom_w{}, eng{}, enuc{};
    arch::backend::HostStateTransferView view{
        rho.data(), mom_u.data(), mom_v.data(), mom_w.data(), eng.data(),
        enuc.data(), nullptr, rho.size(), 0, 0};
    const arch::backend::BackendStateAccess access{
        backend.block, backend.storage, StateSlot::Current};
    const auto upload = arch::backend::transfer_state_regions(
        backend, ledger, clock, access, view,
        PendingTransferPhase::PendingH2D, 0,
        arch::backend::BackendOperation::InitialUpload);
    require(upload.value == 3 && upload.state == CompletionState::Complete,
            "initial upload token drifted");
    const auto synchronized = ledger.inspect(key);
    require(synchronized.interior.residency == StateResidency::Synchronized
                && synchronized.ghost.residency == StateResidency::Synchronized
                && backend.calls == std::vector<int>({10, 11, 30}),
            "initial upload transaction order drifted");
    require(backend.trace.size() == 1
                && backend.trace.front().macro_step == 0
                && backend.trace.front().operation
                    == arch::backend::BackendOperation::InitialUpload
                && backend.trace.front().coherence.interior.residency
                    == synchronized.interior.residency
                && backend.trace.front().coherence.interior.version
                    == synchronized.interior.version
                && backend.trace.front().coherence.ghost.residency
                    == synchronized.ghost.residency
                && backend.trace.front().coherence.ghost.version
                    == synchronized.ghost.version
                && backend.trace.front().coherence.ghost_source_version
                    == synchronized.ghost_source_version
                && backend.trace.front().coherence.interior.completion
                    == synchronized.interior.completion
                && backend.trace.front().coherence.ghost.completion
                    == synchronized.ghost.completion
                && backend.trace.front().bytes_h2d == 128
                && backend.trace.front().bytes_d2h == 0
                && backend.trace.front().stream_sync_count == 1,
            "initial upload trace drifted");

    ledger.publish_interior(
        key, ExecutionSide::Device, {2}, {4, CompletionState::Complete});
    ledger.publish_ghost(
        key, ExecutionSide::Device, {2}, {5, CompletionState::Complete});
    clock = arch::scheduler::MonotonicSchedulerClock(5, 2);
    backend.calls.clear();
    backend.enqueue_count = 0;
    const auto materialized = arch::backend::transfer_state_regions(
        backend, ledger, clock, access, view,
        PendingTransferPhase::PendingD2H, 7,
        arch::backend::BackendOperation::Materialize);
    require(materialized.value == 6
                && backend.calls == std::vector<int>({20, 21, 30}),
            "materialization transaction order drifted");
    require(backend.trace.size() == 2
                && backend.trace.back().macro_step == 7
                && backend.trace.back().operation
                    == arch::backend::BackendOperation::Materialize
                && backend.trace.back().bytes_h2d == 0
                && backend.trace.back().bytes_d2h == 128
                && backend.trace.back().stream_sync_count == 1,
            "materialization trace drifted");
    const auto before_getters = backend.counters_value;
    const auto snapshot = backend.trace_snapshot();
    const auto after_getters = backend.counters();
    require(snapshot.size() == backend.trace.size()
                && snapshot.back().macro_step
                    == backend.trace.back().macro_step
                && snapshot.back().operation
                    == backend.trace.back().operation
                && after_getters.kernel_count == before_getters.kernel_count
                && after_getters.bytes_h2d == before_getters.bytes_h2d
                && after_getters.bytes_d2h == before_getters.bytes_d2h
                && after_getters.stream_sync_count
                    == before_getters.stream_sync_count
                && after_getters.getter_count >= 2,
            "pure getters changed backend work counters");

    StateResidencyLedger failing({3});
    failing.register_block(
        backend.block, {1}, {1, CompletionState::Complete});
    failing.publish_ghost(
        key, ExecutionSide::Host, {1}, {2, CompletionState::Complete});
    arch::scheduler::MonotonicSchedulerClock failing_clock(2, 1);
    backend.calls.clear();
    backend.enqueue_count = 0;
    backend.fail_enqueue = 2;
    require_failure(
        [&] {
            (void)arch::backend::transfer_state_regions(
                backend, failing, failing_clock, access, view,
                PendingTransferPhase::PendingH2D);
        },
        "second-region enqueue failure was accepted");
    const auto pending = failing.inspect(key);
    require(pending.interior.pending_transfer
                    == PendingTransferPhase::PendingH2D
                && pending.ghost.pending_transfer
                    == PendingTransferPhase::PendingH2D,
            "failed transfer falsely completed or rolled back");

    StateResidencyLedger sync_failing({3});
    sync_failing.register_block(
        backend.block, {1}, {1, CompletionState::Complete});
    sync_failing.publish_ghost(
        key, ExecutionSide::Host, {1}, {2, CompletionState::Complete});
    arch::scheduler::MonotonicSchedulerClock sync_failing_clock(2, 1);
    backend.calls.clear();
    backend.enqueue_count = 0;
    backend.fail_enqueue = 0;
    backend.fail_quiesce = true;
    require_failure(
        [&] {
            (void)arch::backend::transfer_state_regions(
                backend, sync_failing, sync_failing_clock, access, view,
                PendingTransferPhase::PendingH2D);
        },
        "transfer quiesce failure was accepted");
    const auto sync_pending = sync_failing.inspect(key);
    require(sync_pending.interior.pending_transfer
                    == PendingTransferPhase::PendingH2D
                && sync_pending.ghost.pending_transfer
                    == PendingTransferPhase::PendingH2D
                && backend.calls == std::vector<int>({10, 11, 30}),
            "failed quiesce falsely completed or rolled back transfer");
    backend.fail_quiesce = false;

    StateResidencyLedger trace_failing({3});
    trace_failing.register_block(
        backend.block, {1}, {1, CompletionState::Complete});
    trace_failing.publish_ghost(
        key, ExecutionSide::Host, {1}, {2, CompletionState::Complete});
    arch::scheduler::MonotonicSchedulerClock trace_failing_clock(2, 1);
    backend.calls.clear();
    backend.enqueue_count = 0;
    backend.fail_trace = true;
    const std::size_t trace_count_before = backend.trace.size();
    require_failure(
        [&] {
            (void)arch::backend::transfer_state_regions(
                backend, trace_failing, trace_failing_clock, access, view,
                PendingTransferPhase::PendingH2D);
        },
        "transfer trace failure was accepted");
    const auto completed_without_trace = trace_failing.inspect(key);
    require(completed_without_trace.interior.pending_transfer
                    == PendingTransferPhase::None
                && completed_without_trace.ghost.pending_transfer
                    == PendingTransferPhase::None
                && completed_without_trace.interior.residency
                    == StateResidency::Synchronized
                && completed_without_trace.ghost.residency
                    == StateResidency::Synchronized
                && backend.trace.size() == trace_count_before,
            "trace failure did not preserve truthful completed state");
    backend.fail_trace = false;

    StateResidencyLedger begin_failing({3});
    begin_failing.register_block(
        backend.block, {1}, {1, CompletionState::Complete});
    begin_failing.publish_ghost(
        key, ExecutionSide::Host, {1}, {50, CompletionState::Complete});
    arch::scheduler::MonotonicSchedulerClock replaying_clock(2, 1);
    backend.calls.clear();
    backend.enqueue_count = 0;
    backend.fail_enqueue = 0;
    const auto preflight_counters = backend.counters_value;
    require_failure(
        [&] {
            (void)arch::backend::transfer_state_regions(
                backend, begin_failing, replaying_clock, access, view,
                PendingTransferPhase::PendingH2D);
        },
        "second-region begin replay failure was accepted");
    const auto partially_pending = begin_failing.inspect(key);
    require(partially_pending.interior.pending_transfer
                    == PendingTransferPhase::None
                && partially_pending.ghost.pending_transfer
                    == PendingTransferPhase::None
                && partially_pending.interior.residency
                    == StateResidency::HostValid
                && partially_pending.ghost.residency
                    == StateResidency::HostValid
                && backend.calls.empty()
                && backend.counters_value == preflight_counters
                && replaying_clock.last_token() == 2,
            "batch preflight changed ledger/backend before rejection");
}

void test_multiblock_exchange_contract()
{
    FakeBackend backend;
    const std::array accesses{
        arch::backend::BackendStateAccess{
            backend.block, backend.storage,
            arch::state::StateSlot::Next},
        arch::backend::BackendStateAccess{
            backend.second_block, backend.second_storage,
            arch::state::StateSlot::Next}};
    amr::SameLevelExchangePlan plan{};
    const arch::state::CompletionToken token{
        17, arch::state::CompletionState::Complete};
    require(backend.execute_same_level_exchange(
                accesses, plan, arch::state::StateSlot::Next, {4}, token)
                == token
                && backend.counters_value.kernel_count == 1,
            "multi-block exchange did not route all backend identities");
    auto stale = accesses;
    stale[1].storage.value += 1;
    require_failure(
        [&] {
            (void)backend.execute_same_level_exchange(
                stale, plan, arch::state::StateSlot::Next, {4}, token);
        },
        "multi-block exchange accepted stale storage");
}

void test_dynamic_topology_store_is_fail_closed_by_default()
{
    class DummyTopologyTransaction final
        : public arch::backend::BackendTopologyStoreTransaction {};

    FakeBackend backend;
    require(!backend.supports_dynamic_topology_store(),
            "generic backend unexpectedly enabled dynamic topology storage");
    const amr::AmrPlanScope scope{1, {3}, {4}};
    require_failure(
        [&] {
            (void)backend.begin_topology_store_transaction(
                scope,
                std::span<const arch::backend::BackendTopologyBinding>{});
        },
        "backend without a topology store accepted a transaction");
    require_failure(
        [&] {
            backend.publish_topology_store_transaction(nullptr);
        },
        "backend without a topology store accepted publication");

    amr::AmrFluxTopologyPlan flux_plan;
    amr::RefluxPlan reflux_plan;
    DummyTopologyTransaction transaction;
    const arch::state::CompletionToken completed{
        7, arch::state::CompletionState::Complete};
    require_failure(
        [&] { backend.prepare_amr_flux_plan(flux_plan, reflux_plan); },
        "backend without AMR flux support accepted an active plan");
    require_failure(
        [&] {
            backend.stage_amr_flux_plan(
                transaction, flux_plan, reflux_plan);
        },
        "backend without AMR flux support accepted a staged plan");
    require_failure(
        [&] { (void)backend.clear_amr_flux_register(completed); },
        "backend without AMR flux support accepted a clear");
    require_failure(
        [&] {
            (void)backend.execute_amr_reflux(
                arch::state::StateSlot::Current, 1.0, completed);
        },
        "backend without AMR flux support accepted reflux");
}

} // namespace

int main()
{
    static_assert(!std::is_copy_constructible_v<arch::backend::ComputeBackend>);
    static_assert(!std::is_move_constructible_v<arch::backend::ComputeBackend>);
    static_assert(!std::is_copy_constructible_v<
                  arch::backend::StorageGenerationIssuer>);
    static_assert(!std::is_copy_constructible_v<
                  arch::backend::BackendTopologyStoreTransaction>);
    test_generation_authority();
    test_device_block_store_identity();
    test_host_transfer_view();
    test_cuda_launch_config();
    test_transfer_transaction();
    test_multiblock_exchange_contract();
    test_dynamic_topology_store_is_fail_closed_by_default();
    return 0;
}
