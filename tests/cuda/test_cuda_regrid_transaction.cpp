#include "amr/AMRControl.h"
#include "amr/RegridExecutionPlan.h"
#include "cuda/runtime/CudaBackend.h"
#include "driver/DriverUtils.h"
#include "physics/eos/IdealGas.h"
#include "../regrid_migration_fixture.h"

#include <cuda_runtime_api.h>
#include <bit>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>

namespace {

using Access = arch::backend::BackendStateAccess;
using Slot = arch::state::StateSlot;
using Region = arch::state::StateRegion;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

arch::backend::HostStateTransferView transfer(FluidState& state)
{
    return {state.rho.data(), state.mom_u.data(), state.mom_v.data(),
        state.mom_w.data(), state.eng.data(), state.enuc_rate.data(),
        state.mass_fractions.empty() ? nullptr : state.mass_fractions.data(),
        state.rho.size(), static_cast<std::size_t>(state.GetNumSpecies()), state.rho.size()};
}

void compare(const FluidState& expected, const FluidState& actual, bool exact = false)
{
    const auto check = [exact](const auto& left, const auto& right) {
        require(left.size() == right.size(), "regrid field size differs");
        for (std::size_t i = 0; i < left.size(); ++i) {
            if (exact)
                require(std::bit_cast<std::uint64_t>(left[i])
                            == std::bit_cast<std::uint64_t>(right[i]),
                        "device migration changed an accepted source bit");
            else
                require(std::isfinite(right[i]) && std::abs(left[i] - right[i])
                    <= 3.0e-13 * std::max(1.0, std::abs(left[i])),
                    "device staged regrid differs from CPU mathematical authority");
        }
    };
    check(expected.rho, actual.rho); check(expected.mom_u, actual.mom_u);
    check(expected.mom_v, actual.mom_v); check(expected.mom_w, actual.mom_w);
    check(expected.eng, actual.eng); check(expected.enuc_rate, actual.enuc_rate);
    check(expected.mass_fractions, actual.mass_fractions);
    for (double value : actual.mass_fractions)
        require(value >= 0.0 && value <= 1.0, "migration produced invalid species");
}

SimConfig configuration()
{
    SimConfig config{};
    config.grid.dim = 1;
    config.grid.nblockx1 = 2;
    config.grid.nblockx2 = config.grid.nblockx3 = 0;
    config.grid.x1_min = 1.0; config.grid.x1_max = 3.0;
    config.grid.amr_max_blocks = 32;
    config.grid.x1l_boundary_type = config.grid.x1r_boundary_type = "outflow";
    config.amr.lrefinemin = 0; config.amr.lrefinemax = 1;
    config.physics.burn.use_burn = false;
    config.physics.diffusion.use_diffusion = false;
    return config;
}

arch::cuda::CudaLaunchConfig launch_config(const SimConfig& config)
{
    using namespace arch::dispatch;
    return arch::cuda::make_cuda_launch_config({FluxId::Hllc,
        ReconstructionId::Ppm, LimiterId::MinMod, TimeIntegratorId::Euler,
        EosId::Ideal, NetworkId::None, OdeSolverId::None, LinearSolverId::None,
        DiffusionIntegratorId::None}, config);
}

void run(int species_count)
{
    auto config = configuration();
    BCHandler boundary(config);
    amr::AMRControl control(32, 1);
    control.tree->InitRootGrid(config, species_count);
    SpeciesManager species;
    for (int s = 0; s < species_count; ++s)
        species.add_species("passive" + std::to_string(s), s + 1.0, 1.0, 1.4, 1.0);
    IdealGas eos(1.4, species);
    arch::backend::StorageGenerationIssuer issuer(10);
    std::uint64_t next_uid = 100, next_transaction = 1;
    std::vector<Access> active;
    std::vector<arch::cuda::CudaBlockBinding> initial;
    for (const int id : control.tree->GetActiveBlocks()) {
        auto& block = control.pool->GetBlock(id);
        amr::test::seed_regrid_parent(block);
        boundary.apply(block.fluid_state, block.grid);
        active.push_back({{{next_uid++}, {1}}, issuer.issue(), Slot::Current});
        initial.push_back({&block, active.back().block, active.back().storage, &boundary.logical_plan()});
    }
    std::vector<amr::BlockHandle> initial_handles;
    for (const auto& access : active) initial_handles.push_back(access.block);
    control.ghost_exchange.ExecuteExchange(control.pool, control.tree, 1,
                                          &amr::Block::fluid_state, initial_handles);
    auto backend = arch::cuda::make_cuda_backend(initial, 0, launch_config(config), species, eos);
    const auto upload = [&](Access access, FluidState& state) {
        for (const auto region : {Region::Interior, Region::Ghost})
            backend->enqueue_upload_slot(access, region, transfer(state));
        backend->quiesce();
    };
    const auto download = [&](Access access, FluidState shape) {
        for (const auto region : {Region::Interior, Region::Ghost})
            backend->enqueue_materialize_host_current(access, region, transfer(shape));
        backend->quiesce();
        return shape;
    };
    for (std::size_t i = 0; i < active.size(); ++i)
        upload(active[i], control.pool->GetBlock(control.tree->GetActiveBlocks()[i]).fluid_state);

    const auto migrate = [&](bool refine, bool inject_failure) {
        const auto old_ids = control.tree->GetActiveBlocks();
        std::map<amr::BlockHandle, int> old_by_handle;
        std::map<int, FluidState> old_states;
        std::vector<amr::BlockHandle> old_handles;
        for (std::size_t i = 0; i < active.size(); ++i) {
            old_handles.push_back(active[i].block);
            old_by_handle.emplace(active[i].block, old_ids[i]);
            old_states.emplace(old_ids[i], control.pool->GetBlock(old_ids[i]).fluid_state);
        }
        auto prepared = control.tree->PrepareRegrid(config, {}, {}, [&] {
            for (const int id : old_ids) {
                auto& block = control.pool->GetBlock(id);
                block.refine_flag = refine ? (block.level == 0 && block.logical_x1 == 0 ? 1 : 0)
                                           : (block.level == 1 ? -1 : 0);
            }
        });
        require(prepared.topology_changed(), "fixture did not change topology");
        const std::vector<int> proposed_ids(prepared.proposed_active_blocks().begin(),
                                            prepared.proposed_active_blocks().end());
        const amr::AmrPlanScope scope{next_transaction++, active.front().block.epoch,
                                     {active.front().block.epoch.value + 1}};
        std::vector<Access> next;
        std::vector<amr::BlockHandle> next_handles;
        std::vector<arch::backend::BackendTopologyBinding> bindings;
        std::map<amr::BlockHandle, int> proposed_by_handle;
        for (const int id : proposed_ids) {
            auto& block = control.pool->GetBlock(id);
            const auto old = std::find(old_ids.begin(), old_ids.end(), id);
            const amr::BlockUid uid = old != old_ids.end()
                ? active[static_cast<std::size_t>(old - old_ids.begin())].block.uid
                : amr::BlockUid{next_uid++};
            next.push_back({{uid, scope.to_epoch}, issuer.issue(), Slot::Current});
            next_handles.push_back(next.back().block);
            proposed_by_handle.emplace(next.back().block, id);
            bindings.push_back({&block, next.back().block, next.back().storage, &boundary.logical_plan()});
            if (old != old_ids.end()) block.state_next = block.fluid_state;
        }
        prepared.BuildMigrationPlans(old_handles, next_handles, scope);
        const auto groups = amr::compile_regrid_execution_plan(
            prepared.prolongation_plan(), prepared.restriction_plan(), species_count);
        for (const auto& group : groups.prolongations) {
            auto& destination = control.pool->GetBlock(proposed_by_handle.at(group.destination.handle));
            auto oracle = destination;
            oracle.InterpolateFromCoarse(control.pool->GetBlock(old_by_handle.at(group.source.handle)),
                group.child_index, 1, config.numerics.sml_rho, config.numerics.min_eint);
            destination.state_next = oracle.fluid_state;
        }
        for (const auto& group : groups.restrictions) {
            auto& destination = control.pool->GetBlock(proposed_by_handle.at(group.destination.handle));
            auto oracle = destination;
            const amr::Block* children[8]{};
            for (int child = 0; child < 2; ++child)
                children[child] = &control.pool->GetBlock(old_by_handle.at(group.children[child].handle));
            oracle.AverageToCoarse(children, 1, config.numerics.sml_rho, config.numerics.min_eint);
            destination.state_next = oracle.fluid_state;
        }
        prepared.ActivateForDeviceMigration();
        for (const int id : proposed_ids) {
            auto& block = control.pool->GetBlock(id);
            boundary.apply(block.state_next, block.grid);
        }
        control.ghost_exchange.ExecuteExchange(control.pool, control.tree, 1,
                                              &amr::Block::state_next, next_handles);
        std::vector<FluidState> expected;
        for (const int id : proposed_ids) expected.push_back(control.pool->GetBlock(id).state_next);
        std::set<int> poisoned(old_ids.begin(), old_ids.end());
        poisoned.insert(proposed_ids.begin(), proposed_ids.end());
        for (const int id : poisoned) {
            auto& state = control.pool->GetBlock(id).fluid_state;
            std::fill(state.rho.begin(), state.rho.end(), std::numeric_limits<double>::quiet_NaN());
            std::fill(state.mass_fractions.begin(), state.mass_fractions.end(), std::numeric_limits<double>::quiet_NaN());
        }
        FluidState invalid = old_states.at(old_ids.front());
        if (inject_failure) {
            std::fill(invalid.rho.begin(), invalid.rho.end(), -1.0);
            upload(active.front(), invalid);
        }
        const auto before_staging = backend->counters();
        auto transaction = backend->begin_topology_store_transaction(scope, bindings);
        const auto staged = backend->store_snapshot();
        require(staged.active_blocks == old_ids.size() && staged.staged_blocks == proposed_ids.size(),
                "staged regrid did not retain both storage generations");
        std::cout << "REGRID_STORAGE species=" << species_count << " refine=" << refine
                  << " rollback=" << inject_failure << " old_blocks=" << staged.active_blocks
                  << " new_blocks=" << staged.staged_blocks << '\n';
        const auto before = backend->counters();
        require(before.bytes_h2d > before_staging.bytes_h2d
                    && before.kernel_count - before_staging.kernel_count == bindings.size(),
                "staged boundary uploads or metric kernels are missing from counters");
        bool rejected = false;
        try {
            backend->migrate_staged_current(*transaction, active,
                prepared.prolongation_plan(), prepared.restriction_plan());
        } catch (const std::runtime_error& error) {
            require(inject_failure && std::string(error.what())
                == amr::regrid_math::status_message(amr::regrid_math::Status::ParentFluid),
                "runtime did not propagate the shared migration guard");
            rejected = true;
        }
        require(backend->counters().bytes_d2h - before.bytes_d2h == sizeof(int),
                "migration materialized fields instead of one compact status");
        if (inject_failure) {
            require(rejected, "invalid migration was accepted");
            transaction.reset();
            prepared.AbortNoexcept();
            require(control.tree->GetActiveBlocks() == old_ids, "failed device migration changed Host topology");
            require(backend->store_snapshot().staged_blocks == 0, "failed migration leaked staged resources");
            compare(invalid, download(active.front(), invalid), true);
            for (const auto& access : next) require(!backend->contains(access), "failed migration published a destination");
            upload(active.front(), old_states.at(old_ids.front()));
            for (const int id : old_ids) control.pool->GetBlock(id).fluid_state = old_states.at(id);
            return;
        }
        backend->complete_staged_current_ghosts(*transaction,
            control.ghost_exchange.BuildSameLevelPlans(control.pool, control.tree, 1, next_handles),
            control.ghost_exchange.BuildCoarseFinePlan(control.pool, control.tree, 1, next_handles));
        // Accepted old device sources are byte-for-byte mathematically intact,
        // while the Host Current arrays remain deliberately poisoned.
        for (std::size_t i = 0; i < active.size(); ++i)
            compare(old_states.at(old_ids[i]), download(active[i], old_states.at(old_ids[i])), true);
        for (const int id : poisoned)
            require(std::isnan(control.pool->GetBlock(id).fluid_state.rho.front()),
                    "device regrid wrote a Host Current field");
        const auto flux = amr::build_amr_flux_topology_plan(*control.pool,
            control.tree->GetActiveBlocks(), next_handles, 1, species_count);
        const auto before_flux = backend->counters();
        backend->stage_amr_flux_plan(*transaction, flux,
            amr::build_amr_reflux_topology_plan(*control.pool, flux));
        const auto after_flux = backend->counters();
        require(after_flux.bytes_h2d > before_flux.bytes_h2d
                    && after_flux.stream_sync_count - before_flux.stream_sync_count == 1,
                "staged AMR flux metadata uploads or completion fence are missing from counters");
        prepared.CompleteDeviceMigration();
        backend->publish_topology_store_transaction(std::move(transaction));
        prepared.PublishNoexcept();
        prepared.ReleaseRetiredNoexcept();
        for (const auto& access : active) require(!backend->contains(access), "old epoch remained visible after publication");
        active = next;
        for (std::size_t i = 0; i < active.size(); ++i) {
            const auto actual = download(active[i], expected[i]);
            compare(expected[i], actual);
            control.pool->GetBlock(proposed_ids[i]).fluid_state = actual; // explicit oracle consumer
        }
    };
    migrate(true, true);
    migrate(true, false);
    migrate(false, false);
    require(control.tree->GetActiveBlocks().size() == 2, "device refine/restrict did not round trip");
    std::cout << "CUDA_REGRID_TRANSACTION_PASS species=" << species_count
              << " survivor=1 refine=1 restrict=1 rollback=1 stale_host=1\n";
}

} // namespace

int main()
{
    int count = 0;
    const auto probe = cudaGetDeviceCount(&count);
    if (probe == cudaErrorNoDevice || probe == cudaErrorInsufficientDriver
        || (probe == cudaSuccess && count == 0)) return 77;
    try {
        require(probe == cudaSuccess, "CUDA device probe failed");
        run(4);
        run(41);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
