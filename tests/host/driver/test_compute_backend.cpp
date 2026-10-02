/**
 * @file test_compute_backend.cpp
 * @brief Test backend-neutral storage and transfer contracts.
 *
 * Mock-backed cases check generation ownership, device-store identities,
 * transfer completion and topology rejection without requiring a CUDA device.
 */
#include "driver/runtime/ComputeBackend.h"
#include "cuda/common/CudaLaunchConfig.h"
#include "cuda/runtime/DeviceBlockStore.h"
#include "amr/exchange/ExchangePlan.h"
#include "amr/flux/AmrFluxPlan.h"
#include "physics/boundary/PhysicalBoundaryHandler.h"
#include "physics/eos/IdealGas.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif

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

class FakeBackend : public arch::backend::ComputeBackend {
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
    double compute_hydro_dt(arch::backend::BackendStateAccess access, double) override
    {
        ++hydro_dt_calls;
        return access.block == block ? 1.0 : 2.0;
    }
    arch::state::CompletionToken execute_hydro_stage(
        arch::backend::BackendStateAccess,
        const arch::scheduler::StageDescriptor&, double,
        arch::state::CompletionToken token) override
    {
        ++hydro_stage_calls;
        if (hydro_stage_calls == fail_hydro_stage)
            throw std::runtime_error("injected Hydro EOS failure");
        if (incomplete_hydro_stage)
            return {token.value, arch::state::CompletionState::Pending};
        if (wrong_hydro_token) ++token.value;
        return token;
    }
    arch::state::CompletionToken execute_physical_boundary(
        arch::backend::BackendStateAccess, arch::state::StateVersion,
        arch::state::CompletionToken token) override { return token; }
    void rotate_slots(arch::backend::BackendStateAccess,
                      arch::state::SlotRotation) override {}
    double compute_diffusion_dt(arch::backend::BackendStateAccess access) override
    {
        ++microphysics_calls;
        return access.block == block ? 1.0 : 2.0;
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
                         arch::backend::BackendStateAccess) override { ++microphysics_calls; }
    arch::state::CompletionToken execute_diffusion_stage(
        arch::backend::BackendStateAccess, const arch::scheduler::RklPlan&,
        const arch::scheduler::RklStageDescriptor&, double, double,
        arch::state::CompletionToken token) override {
        ++microphysics_calls;
        if (incomplete_microphysics) token.state = arch::state::CompletionState::Pending;
        return token;
    }
    arch::backend::BurnExecutionResult execute_burn(
        arch::backend::BackendStateAccess, double dt,
        arch::state::CompletionToken token) override
    {
        ++microphysics_calls;
        if (incomplete_microphysics) token.state = arch::state::CompletionState::Pending;
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
    int hydro_dt_calls = 0;
    int microphysics_calls = 0;
    bool incomplete_microphysics = false;
    int hydro_stage_calls = 0;
    int fail_hydro_stage = 0;
    bool incomplete_hydro_stage = false;
    bool wrong_hydro_token = false;
    int fail_enqueue = 0;
    bool fail_quiesce = false;
    bool fail_trace = false;
    mutable std::uint64_t getter_count = 0;
    arch::backend::BackendCounters counters_value{};
    std::vector<arch::backend::BackendTraceRecord> trace;
};

// Reuse the existing backend double; only boundary gather/scatter is implemented.
class BoundarySurfaceBackend final : public FakeBackend {
public:
    explicit BoundarySurfaceBackend(FluidState& value) : state(value) {}
    arch::backend::BoundaryCells read_boundary_cells(
        arch::backend::BackendStateAccess, std::span<const int> cells,
        arch::state::StateRegion) override
    {
#ifdef _OPENMP
        require(!omp_in_parallel(), "boundary gather entered the worker team");
#endif
        arch::backend::BoundaryCells result;
        result.species_count = state.GetNumSpecies();
        for (const int cell : cells) {
            result.conserved.push_back(state.get(cell));
            result.enuc.push_back(state.enuc_rate[cell]);
            for (int s = 0; s < state.GetNumSpecies(); ++s)
                result.composition.push_back(state.X(s, cell));
        }
        return result;
    }
    void write_boundary_cells(arch::backend::BackendStateAccess,
        std::span<const int> cells, const arch::backend::BoundaryCells& packed,
        const arch::boundary::DiffusionBoundaryStorage& controls) override
    {
#ifdef _OPENMP
        require(!omp_in_parallel(), "boundary scatter entered the worker team");
#endif
        ++writes;
        for (std::size_t i = 0; i < cells.size(); ++i) {
            state.set(cells[i], packed.conserved[i]);
            state.enuc_rate[cells[i]] = packed.enuc[i];
            for (int s = 0; s < state.GetNumSpecies(); ++s)
                state.X(s, cells[i]) = packed.composition[i * state.GetNumSpecies() + s];
        }
        state.diffusion_boundary =
            std::make_shared<arch::boundary::DiffusionBoundaryStorage>(controls);
    }
    FluidState& state;
    int writes = 0;
};

// Exercise the actual parallel Host surface evaluator, including repeated
// corner destinations, independent donor/inherited snapshots and transactional
// failure. The frozen serial Host path is the reference for exact field parity.
void test_parallel_boundary_surface()
{
    using namespace arch::boundary;
    SpeciesManager species;
    species.add_species("a", 1., 1., 5. / 3., 1.e8);
    species.add_species("b", 4., 2., 1.4, 2.e8);
    const IdealGas eos(1.4, species);
    SimConfig config;
    config.grid.dim = 3;
    config.grid.x1_min = config.grid.x2_min = config.grid.x3_min = 0.;
    config.grid.x1_max = config.grid.x2_max = config.grid.x3_max = 16.;
    config.grid.x1l_boundary_type = config.grid.x1r_boundary_type = "user";
    config.grid.x2l_boundary_type = config.grid.x2r_boundary_type = "user";
    config.grid.x3l_boundary_type = config.grid.x3r_boundary_type = "user";
    config.physics.diffusion.use_diffusion = true;
    config.physics.diffusion.use_thermal_diffusion = true;
    config.physics.diffusion.use_viscous_diffusion = true;
    config.physics.diffusion.use_species_diffusion = true;
    Grid grid(4, 0., 16., 0., 16., 0., 16.);
    grid.dim = 3; grid.geometry = "cartesian"; grid.InitializeTopology();
    std::atomic<std::size_t> calls{0};
    std::atomic<std::uint64_t> thread_mask{0};
    bool inject_failure = false; // Written only outside the joined sample loop.
    ResolvedUserBoundaries callbacks;
    callbacks.physical = [&](const PhysicalBoundaryContext& context) {
        ++calls;
#ifdef _OPENMP
        thread_mask.fetch_or(std::uint64_t{1} << omp_get_thread_num());
#else
        thread_mask.fetch_or(1);
#endif
        if (inject_failure && context.axis == BoundaryAxis::X2
            && context.side == BoundarySide::Lower && context.ghost_depth == 2)
            throw std::runtime_error("injected surface callback failure");
        require(context.time == .375, "parallel surface changed stage time");
        const int face = 2 * static_cast<int>(context.axis) + static_cast<int>(context.side);
        PhysicalBoundaryData data;
        if (context.purpose == BoundaryPurpose::Hydro) {
            auto primitive = context.interior;
            primitive.u += .125 * face;
            primitive.SetTemperature(1000. + context.point.x);
            data.hydro = primitive;
        } else {
            data.temperature = {ScalarBoundaryKind::OutwardFlux, 11. + face};
            data.velocity[static_cast<int>(context.axis)] =
                {ScalarBoundaryKind::Value, .25 * face};
            data.species = {{ScalarBoundaryKind::OutwardFlux, -.125},
                            {ScalarBoundaryKind::OutwardFlux, .125}};
        }
        return data;
    };
    ScopedUserBoundarySelection selected(callbacks, config, species);
    BCHandler handler(config); handler.bind(eos, species);
    FluidState initial;
    initial.Preallocate(grid.GetTotalSize()); initial.InitSpecies(2);
    for (int cell = 0; cell < grid.GetTotalSize(); ++cell) {
        PrimitiveData primitive;
        primitive.rho = .5 + cell * 1.e-5;
        primitive.u = 2.; primitive.v = -.5; primitive.w = .25;
        primitive.SetTemperature(1000.);
        primitive.mass_fractions = {.7, .3};
        initial.set(cell, ProblemHelper::detail::InitialConservedState(primitive, eos, config.numerics));
        initial.X(0, cell) = .7; initial.X(1, cell) = .3;
        initial.enuc_rate[cell] = cell * .125;
    }
    const auto logical = arch::boundary::host::compile(handler.logical_plan(),
        arch::boundary::host::make_layout(grid));
#ifdef _OPENMP
    const int old_threads = omp_get_max_threads();
    const int old_dynamic = omp_get_dynamic();
    omp_set_dynamic(0); omp_set_num_threads(4);
#endif
    for (const auto purpose : {BoundaryPurpose::Hydro, BoundaryPurpose::Diffusion}) {
        handler.configure_stage(.375, purpose);
        auto expected = initial, actual = initial;
        arch::boundary::host::execute(logical, actual); // CUDA built-in phase precedes gather.
        handler.apply(expected, grid);
        BoundarySurfaceBackend backend(actual);
        calls = 0; thread_mask = 0;
        handler.apply_device(backend, {backend.block, backend.storage, arch::state::StateSlot::Current}, grid);
        require(calls > 512 && backend.writes == 1, "surface did not exercise the parallel-size batch");
#ifdef _OPENMP
        require(std::popcount(thread_mask.load()) == 4, "surface samples did not use all four Host workers");
#endif
        require(actual.rho == expected.rho && actual.mom_u == expected.mom_u
            && actual.mom_v == expected.mom_v && actual.mom_w == expected.mom_w
            && actual.eng == expected.eng && actual.enuc_rate == expected.enuc_rate
            && actual.mass_fractions == expected.mass_fractions,
            "parallel surface changed immutable snapshots or corner precedence");
        for (int face = 0; face < 6; ++face) {
            const auto& a = actual.diffusion_boundary->faces[face];
            const auto& e = expected.diffusion_boundary->faces[face];
            require(a.size() == e.size(), "parallel face control extent changed");
            for (std::size_t i = 0; i < a.size(); ++i)
                require(a[i].kind == e[i].kind && a[i].value == e[i].value,
                    "parallel surface mixed face control rows");
        }
        auto failed = initial;
        arch::boundary::host::execute(logical, failed);
        const auto before = failed;
        BoundarySurfaceBackend rejected(failed);
        inject_failure = true;
        require_failure([&] { handler.apply_device(rejected,
            {rejected.block, rejected.storage, arch::state::StateSlot::Current}, grid); },
            "parallel callback failure escaped without rejection");
        inject_failure = false;
        require(rejected.writes == 0 && failed.rho == before.rho
            && failed.mom_u == before.mom_u && failed.mom_v == before.mom_v
            && failed.mom_w == before.mom_w && failed.eng == before.eng
            && failed.mass_fractions == before.mass_fractions
            && failed.enuc_rate == before.enuc_rate && !failed.diffusion_boundary,
            "failed parallel surface published partial ghosts or controls");
    }
#ifdef _OPENMP
    omp_set_num_threads(old_threads); omp_set_dynamic(old_dynamic);
#endif
}

void test_microphysics_batch_contract()
{
    using namespace arch::state;
    using arch::backend::BackendStateAccess;
    FakeBackend backend;
    const std::array<BackendStateAccess, 2> accesses{{
        {backend.second_block, backend.second_storage, StateSlot::Current},
        {backend.block, backend.storage, StateSlot::Current}}};
    const CompletionToken token{91, CompletionState::Complete};
    const arch::scheduler::RklPlan plan{};
    const arch::scheduler::RklStageDescriptor descriptor{};
    require(backend.compute_diffusion_dt_batch({}).empty()
        && backend.execute_burn_batch({}, 0.1, token).empty(), "Empty microphysics batch launched work");
    backend.copy_state_slot_batch({}, StateSlot::Next);
    require(backend.microphysics_calls == 0, "Empty copy launched work");
    require(backend.compute_diffusion_dt_batch(accesses) == std::vector<double>({2.0, 1.0}),
        "Diffusion batch changed request ordering");
    require(backend.execute_burn_batch(accesses, 0.1, token).size() == 2,
        "Burn batch lost a result");
    backend.copy_state_slot_batch(accesses, StateSlot::Scratch);
    require(backend.execute_diffusion_stage_batch(accesses, plan, descriptor, 0.1, 0.1, token) == token,
        "Diffusion batch lost completion");
    for (int fault = 0; fault < 4; ++fault) {
        auto invalid = accesses;
        if (fault == 0) ++invalid[1].storage.value;
        if (fault == 1) invalid[1].slot = StateSlot::Next;
        if (fault == 2) invalid[1] = invalid[0];
        if (fault == 3) ++invalid[1].block.epoch.value;
        const int before = backend.microphysics_calls;
        require_failure([&] { backend.compute_diffusion_dt_batch(invalid); }, "Invalid dt batch accepted");
        require_failure([&] { backend.copy_state_slot_batch(invalid, StateSlot::Next); }, "Invalid copy accepted");
        require_failure([&] { backend.execute_burn_batch(invalid, 0.1, token); }, "Invalid burn accepted");
        require_failure([&] { backend.execute_diffusion_stage_batch(invalid, plan, descriptor, 0.1, 0.1, token); },
            "Invalid stage accepted");
        require(before == backend.microphysics_calls, "Late invalid access allowed earlier writes");
    }
    const int before = backend.microphysics_calls;
    require_failure([&] { backend.copy_state_slot_batch(accesses, StateSlot::Current); }, "Aliased batch copy accepted");
    require_failure([&] { backend.execute_burn_batch(accesses, 0.1, {91, CompletionState::Pending}); },
        "Pending burn batch accepted");
    require(before == backend.microphysics_calls, "Invalid batch submitted work");
    backend.incomplete_microphysics = true;
    require_failure([&] { backend.execute_burn_batch(accesses, 0.1, token); }, "Incomplete burn accepted");
    require_failure([&] { backend.execute_diffusion_stage_batch(accesses, plan, descriptor, 0.1, 0.1, token); },
        "Incomplete diffusion accepted");
}

void test_hydro_batch_contract()
{
    using namespace arch::state;
    using arch::backend::BackendStateAccess;
    FakeBackend backend;
    const std::array<BackendStateAccess, 2> accesses{{
        {backend.second_block, backend.second_storage, StateSlot::Current},
        {backend.block, backend.storage, StateSlot::Current}}};
    const auto descriptor = arch::scheduler::make_hydro_plan(
        arch::scheduler::HydroMethod::Euler).stages.front();
    const CompletionToken token{91, CompletionState::Complete};
    require(backend.compute_hydro_dt_batch({}, 0.8).empty(),
            "empty local CFL batch must contribute no candidates");
    require(backend.execute_hydro_stage_batch({}, descriptor, 0.1, token) == token
                && backend.hydro_stage_calls == 0 && backend.hydro_dt_calls == 0,
            "empty Hydro batch launched work");
    require(backend.compute_hydro_dt_batch(accesses, 0.8) == std::vector<double>({2.0, 1.0}),
            "Hydro batch changed request ordering");
    require(backend.execute_hydro_stage_batch(accesses, descriptor, 0.1, token) == token
                && backend.hydro_stage_calls == 2,
            "Hydro batch did not complete all blocks");
    const std::array<BackendStateAccess, 1> single{{accesses[1]}};
    require(backend.compute_hydro_dt_batch(single, 0.8) == std::vector<double>({1.0}),
            "single-block batch changed result extent");

    for (int fault = 0; fault < 4; ++fault) {
        auto invalid = accesses;
        if (fault == 0) invalid[1].storage.value += 100;
        if (fault == 1) invalid[1].slot = StateSlot::Next;
        if (fault == 2) invalid[1] = invalid[0];
        if (fault == 3) invalid[1].block.epoch.value += 1;
        const int dt_calls = backend.hydro_dt_calls;
        const int stage_calls = backend.hydro_stage_calls;
        require_failure([&] { (void)backend.compute_hydro_dt_batch(invalid, 0.8); },
                        "invalid later access accepted by CFL batch");
        require_failure([&] {
            (void)backend.execute_hydro_stage_batch(invalid, descriptor, 0.1, token);
        }, "invalid later access accepted by Hydro batch");
        require(backend.hydro_dt_calls == dt_calls && backend.hydro_stage_calls == stage_calls,
                "batch validation submitted earlier work before rejecting later access");
    }
    const int before = backend.hydro_stage_calls;
    require_failure([&] {
        (void)backend.execute_hydro_stage_batch(accesses, descriptor, 0.1,
            {token.value, CompletionState::Pending});
    }, "Hydro batch accepted pending expected token");
    require(backend.hydro_stage_calls == before, "pending batch launched work");
    backend.incomplete_hydro_stage = true;
    require_failure([&] {
        (void)backend.execute_hydro_stage_batch(accesses, descriptor, 0.1, token);
    }, "Hydro batch manufactured completion from pending work");
    backend.incomplete_hydro_stage = false;
    backend.wrong_hydro_token = true;
    require_failure([&] {
        (void)backend.execute_hydro_stage_batch(accesses, descriptor, 0.1, token);
    }, "Hydro batch accepted a different completion token");
    backend.wrong_hydro_token = false;
    backend.fail_hydro_stage = backend.hydro_stage_calls + 2;
    require_failure([&] {
        (void)backend.execute_hydro_stage_batch(accesses, descriptor, 0.1, token);
    }, "failure in the second block returned batch success");

    for (const auto slot : {StateSlot::Current, StateSlot::Next, StateSlot::Scratch}) {
        auto selected = accesses;
        for (auto& access : selected) access.slot = slot;
        require(backend.execute_physical_boundary_batch(selected, {1}, token) == token,
                "boundary batch must accept each logical slot");
        selected[1].storage.value += 100;
        require_failure([&] {
            (void)backend.execute_physical_boundary_batch(selected, {1}, token);
        }, "boundary batch accepted a stale later access");
    }
    require_failure([&] {
        (void)backend.execute_physical_boundary_batch(accesses, {}, token);
    }, "boundary batch accepted an invalid state version");
}

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

/** Boundary-only updates preserve synchronized interiors through H2D/D2H.
 *  A failed fence must leave only the changed region pending, with the valid
 *  interior and its original completion token untouched. */
void test_mixed_region_transfer()
{
    using namespace arch::state;
    for(const auto direction:{PendingTransferPhase::PendingH2D,PendingTransferPhase::PendingD2H}) {
        FakeBackend backend;
        StateResidencyLedger ledger({3});
        const StateKey key{backend.block,StateSlot::Current};
        ledger.register_block(backend.block,{1},{1,CompletionState::Complete});
        ledger.publish_ghost(key,ExecutionSide::Host,{1},{2,CompletionState::Complete});
        arch::scheduler::MonotonicSchedulerClock clock(2,1);
        std::array<double,8> rho{},mom_u{},mom_v{},mom_w{},eng{},enuc{};
        arch::backend::HostStateTransferView view{rho.data(),mom_u.data(),mom_v.data(),mom_w.data(),
            eng.data(),enuc.data(),nullptr,rho.size(),0,0};
        const arch::backend::BackendStateAccess access{backend.block,backend.storage,StateSlot::Current};
        const auto initial=arch::backend::transfer_state_regions(backend,ledger,clock,access,view,
            PendingTransferPhase::PendingH2D);
        const auto side=direction==PendingTransferPhase::PendingH2D?ExecutionSide::Host:ExecutionSide::Device;
        ledger.publish_ghost(key,side,{1},{4,CompletionState::Complete});
        clock=arch::scheduler::MonotonicSchedulerClock(4,1);
        backend.calls.clear();backend.enqueue_count=0;
        const auto completed=arch::backend::transfer_state_regions(backend,ledger,clock,access,view,direction,
            0,arch::backend::BackendOperation::Materialize);
        const auto result=ledger.inspect(key);
        const bool upload=direction==PendingTransferPhase::PendingH2D;
        require(completed.value==5 && backend.calls==std::vector<int>({upload?11:21,30})
            && result.interior.residency==StateResidency::Synchronized
            && result.ghost.residency==StateResidency::Synchronized
            && result.interior.completion==initial && result.ghost.completion==completed,
            "boundary-only transfer rewrote the synchronized interior");
        require(backend.trace.back().bytes_h2d==(upload?64:0)
            && backend.trace.back().bytes_d2h==(upload?0:64),
            "boundary-only transfer copied a redundant interior region");
        require_failure([&]{arch::backend::transfer_state_regions(backend,ledger,clock,access,view,direction);},
            "a wholly synchronized state was accepted as a transfer source");
        ledger.publish_ghost(key,side,{1},{6,CompletionState::Complete});
        clock=arch::scheduler::MonotonicSchedulerClock(6,1);
        backend.fail_quiesce=true;
        require_failure([&]{arch::backend::transfer_state_regions(backend,ledger,clock,access,view,direction);},
            "mixed-region transfer accepted a failed fence");
        const auto pending=ledger.inspect(key);
        require(pending.interior.residency==StateResidency::Synchronized
            && pending.interior.completion==initial
            && pending.interior.pending_transfer==PendingTransferPhase::None
            && pending.ghost.pending_transfer==direction,
            "failed boundary transfer corrupted the synchronized interior");
    }
}

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
    test_hydro_batch_contract();
    test_microphysics_batch_contract();
    test_parallel_boundary_surface();
    test_transfer_transaction();
    test_mixed_region_transfer();
    test_multiblock_exchange_contract();
    test_dynamic_topology_store_is_fail_closed_by_default();
    return 0;
}
