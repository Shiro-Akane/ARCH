/**
 * @file test_cuda_regrid_transaction.cpp
 * @brief Exercise transactional regridding through the CUDA backend.
 *
 * Check topology changes and field restoration for both compact and larger
 * species storage, using the shared transaction and transfer machinery.
 */
#include "amr/AMRControl.h"
#include "amr/transfer/RegridExecutionPlan.h"
#include "cuda/runtime/CudaBackend.h"
#include "driver/DriverUtils.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "physics/boundary/PhysicalBoundaryHandler.h"
#include "physics/boundary/UserBoundary.h"
#include "physics/eos/IdealGas.h"
#include "fixtures/amr/regrid_migration_fixture.h"

#include <cuda_runtime_api.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

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

// Actual Runtime ledger -> real CudaBackend -> original device Jeans owner.
// Host arrays are poisoned after upload: this is residency/routing evidence,
// not a replacement for the independent EOS/numeric science tests.
void run_jeans_runtime_lease()
{
    auto config=configuration();
    SpeciesManager species;
    species.add_species("gas",1.,1.,1.4,1.);
    IdealGas eos(1.4,species);
    amr::AMRControl control(32,1);
    control.tree->InitRootGrid(config,1);
    for(int id:control.tree->GetActiveBlocks())
        amr::test::seed_regrid_parent(control.pool->GetBlock(id));
    BCHandler boundary(config);
    RunState start{};
    SimulationController counters(config,start);
    arch::driver::DriverRuntime runtime(control,boundary,config,species,counters);
    runtime.initialize_topology();
    runtime.ensure_fluid_ghosts();
    const auto topology=runtime.prepare_backend_bindings();
    std::vector<arch::cuda::CudaBlockBinding> bindings;
    for(const auto& b:topology)
        bindings.push_back({b.block,b.handle,b.storage,b.physical_boundary});
    runtime.install_backend(arch::cuda::make_cuda_backend(bindings,0,
        launch_config(config),species,eos));
    runtime.upload_initial_state();
    const auto initial=runtime.evaluate_current_jeans_resolution();
    require(initial.size()==2,"Runtime JENS initial extent mismatch");
    auto context=runtime.stage_context();
    arch::scheduler::publish_completed_interior(context,runtime.handles(),Slot::Current);
    for(int id:control.tree->GetActiveBlocks())
        for(double& rho:control.pool->GetBlock(id).fluid_state.rho)
            rho=std::numeric_limits<double>::quiet_NaN();
    const auto before=runtime.backend()->counters();
    require(runtime.evaluate_current_jeans_resolution()==initial,
        "Runtime JENS read stale Host arrays");
    const auto after=runtime.backend()->counters();
    require(after.bytes_h2d==before.bytes_h2d
        && after.bytes_d2h-before.bytes_d2h==2*sizeof(double)
        && after.kernel_count-before.kernel_count==4
        && after.stream_sync_count-before.stream_sync_count==1,
        "Runtime JENS materialized fields or used extra execution");
    const auto unchanged=[&] {
        const auto c=runtime.backend()->counters();
        return c.kernel_count==after.kernel_count && c.bytes_h2d==after.bytes_h2d
            && c.bytes_d2h==after.bytes_d2h && c.stream_sync_count==after.stream_sync_count;
    };
    const auto witness=context.clock.next_publication();
    context.ledger.publish_interior({runtime.handles().back(),Slot::Current},
        arch::state::ExecutionSide::Device,witness.version,witness.completion);
    bool rejected=false;
    try {(void)runtime.evaluate_current_jeans_resolution();}
    catch(const std::logic_error&) {rejected=true;}
    require(rejected && unchanged(),"nonfirst Current mismatch reached device JENS");
    arch::scheduler::publish_completed_interior(context,runtime.handles(),Slot::Current);
    const auto token=context.clock.next_completion();
    const auto last=arch::state::StateKey{runtime.handles().back(),Slot::Current};
    context.ledger.begin_transfer(last,Region::Interior,
        arch::state::PendingTransferPhase::PendingD2H,
        {token.value,arch::state::CompletionState::Pending});
    rejected=false;
    try {(void)runtime.evaluate_current_jeans_resolution();}
    catch(const std::logic_error&) {rejected=true;}
    require(rejected && unchanged(),"pending nonfirst Current reached device JENS");
    // Complete a real explicit D2H before declaring the injected transfer
    // complete. Do not mark poisoned Host storage synchronized by token alone.
    auto& host=control.pool->GetBlock(control.tree->GetActiveBlocks().back()).fluid_state;
    runtime.backend()->enqueue_materialize_host_current(
        runtime.backend_access(runtime.handles().size()-1,Slot::Current),
        Region::Interior,transfer(host));
    runtime.backend()->quiesce();
    context.ledger.complete_transfer(last,Region::Interior,token);
    require(runtime.evaluate_current_jeans_resolution()==initial,
        "Runtime JENS did not recover from a rejected lease");
    require(counters.t_current==0. && counters.step_count==0,"lease test ran simulation");
    std::cout<<"CUDA_JEANS_RUNTIME_LEASE_PASS blocks=2 stale_host=1 version_reject=1"
        <<" pending_reject=1 recovery=1 time=0 steps=0\n";
}

// Internal Runtime transaction qualification from the frozen uniform state's
// geometry/material. No public configuration/startup capability is opened here;
// no gravity solve, timestep or checkpoint is claimed.
void run_jeans_runtime_transactions(int dimension,bool capacity_failure=false)
{
    auto config=configuration();
    config.grid.dim=dimension;
    config.grid.nblockx1=4;
    config.grid.nblockx2=dimension>=2?1:0;
    config.grid.nblockx3=dimension==3?1:0;
    config.grid.x1_min=config.grid.x2_min=config.grid.x3_min=0.;
    config.grid.x1_max=1.;config.grid.x2_max=config.grid.x3_max=.25;
    config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="periodic";
    config.grid.x2l_boundary_type=config.grid.x2r_boundary_type="periodic";
    config.grid.x3l_boundary_type=config.grid.x3r_boundary_type="periodic";
    config.grid.amr_max_blocks=capacity_failure?8:64;
    config.physics.gravity.type="self";
    config.execution.compute_backend="cuda";
    config.amr.refine_on_jeans=true;config.amr.jeans_cells=160.;
    config.amr.refine_on_rho=true;
    config.amr.refine_threshold=.1;config.amr.derefine_threshold=.05;
    constexpr double gamma=1.6666666666666667;
    SpeciesManager species;species.add_species("gas",1.,1.,gamma,1.);
    IdealGas eos(gamma,species);
    amr::AMRControl control(config.grid.amr_max_blocks,dimension);
    control.tree->InitRootGrid(config,1);
    for(int id:control.tree->GetActiveBlocks()) {
        auto& b=control.pool->GetBlock(id);
        for(auto* state:{&b.fluid_state,&b.state_next,&b.state_scratch}) {
            state->InitSpecies(1);
            for(int cell=0;cell<b.grid.GetTotalSize();++cell) {
                state->set(cell,{1.e7,0.,0.,0.,1.e7});
                state->enuc_rate[cell]=0.;state->X(0,cell)=1.;
            }
        }
    }
    // A Host EOS fallback would fail before being misreported as device work.
    control.tree->SetJeansEvaluator([](const FluidVector&,const double*,
        const GridMetrics::GeometryView&,int,int)->JeansDiagnostics::Resolution {
        throw std::logic_error("stale Host Jeans evaluator was called");
    });
    BCHandler boundary(config);RunState start{};
    SimulationController counters(config,start);
    arch::driver::DriverRuntime runtime(control,boundary,config,species,counters);
    runtime.initialize_topology();runtime.ensure_fluid_ghosts();
    const auto topology=runtime.prepare_backend_bindings();
    std::vector<arch::cuda::CudaBlockBinding> bindings;
    for(const auto& b:topology)
        bindings.push_back({b.block,b.handle,b.storage,b.physical_boundary});
    runtime.install_backend(arch::cuda::make_cuda_backend(bindings,0,
        launch_config(config),species,eos));
    runtime.upload_initial_state();
    const auto root_minima=runtime.evaluate_current_jeans_resolution();
    require(root_minima.size()==4 && root_minima.front()<160. && root_minima.front()>64.,
        "frozen uniform geometry did not bracket the requested target");
    const auto poison=[&] {
        for(int id:control.tree->GetActiveBlocks()) {
            auto& state=control.pool->GetBlock(id).fluid_state;
            for(double& rho:state.rho)rho=std::numeric_limits<double>::quiet_NaN();
            for(double& x:state.mass_fractions)x=std::numeric_limits<double>::quiet_NaN();
        }
    };
    poison();
    const auto original_handles=runtime.handles();
    if(capacity_failure) {
        bool rejected=false;
        try {runtime.ensure_jeans_resolution(0,0.);}
        catch(const std::runtime_error& error) {
            rejected=std::string(error.what())=="AMR MemoryPool exhausted! Increase MaxBlocks.";
        }
        require(rejected && runtime.handles()==original_handles
            && control.tree->GetActiveBlocks().size()==4,
            "capacity failure partially published JENS topology");
        require(runtime.evaluate_current_jeans_resolution()==root_minima,
            "capacity failure changed accepted device source");
        const auto* backend=dynamic_cast<const arch::cuda::CudaBackend*>(runtime.backend());
        require(backend && backend->store_snapshot().staged_blocks==0,
            "capacity failure leaked a staged device namespace");
        require(counters.t_current==0. && counters.step_count==0,
            "capacity failure advanced simulation");
        std::cout<<"CUDA_JEANS_RUNTIME_CAPACITY_PASS roots=4 final_leaves=8 peak_needed=12 capacity=8 rollback=1 time=0 steps=0\n";
        return;
    }
    runtime.ensure_jeans_resolution(0,0.);
    const auto refined_count=std::size_t(4)<<dimension;
    require(runtime.handles().size()==refined_count && runtime.handles()!=original_handles,
        "real JENS repair did not publish complete refinement");
    poison();
    const auto refined_handles=runtime.handles();
    std::vector<Access> refined_accesses;
    for(std::size_t i=0;i<refined_handles.size();++i)
        refined_accesses.push_back(runtime.backend_access(i,Slot::Current));
    const auto refined_minima=runtime.evaluate_current_jeans_resolution();
    for(double x:refined_minima)require(x>=160.,"published leaf remains underresolved");
    require(!runtime.perform_regrid(0,0.) && runtime.handles()==refined_handles,
        "underresolved candidate parent was not vetoed");
    config.amr.jeans_cells=root_minima.front();
    require(runtime.perform_regrid(0,0.) && runtime.handles().size()==4,
        "candidate-parent equality was not allowed to coarsen");
    poison();
    require(runtime.evaluate_current_jeans_resolution()==root_minima,
        "real JENS coarsen changed the uniform accepted state");
    for(const auto access:refined_accesses)
        require(!runtime.backend()->contains(access),
            "retired refined identity remained consumable");
    const auto coarse_handles=runtime.handles();
    config.amr.jeans_cells=std::nextafter(root_minima.front(),std::numeric_limits<double>::infinity());
    config.amr.lrefinemax=0;
    bool rejected=false;
    try {runtime.ensure_jeans_resolution(0,0.);}
    catch(const std::runtime_error& error) {
        rejected=std::string(error.what()).find("lrefinemax")!=std::string::npos;
    }
    require(rejected && runtime.handles()==coarse_handles
        && runtime.evaluate_current_jeans_resolution()==root_minima,
        "finest-level deficit did not reject before publication");
    config.amr.lrefinemax=1;
    runtime.ensure_jeans_resolution(0,0.);
    require(runtime.handles().size()==refined_count,"nextafter deficit did not refine");
    poison();
    // The frozen package explicitly requests an allowed-parent target of 64;
    // equality above is a separate exact FP64 boundary check.
    config.amr.jeans_cells=64.;
    require(runtime.perform_regrid(0,0.) && runtime.handles().size()==4,
        "frozen target 64 did not permit real parent coarsening");
    poison();
    require(runtime.evaluate_current_jeans_resolution()==root_minima,
        "target 64 coarsen changed the uniform accepted state");
    require(counters.t_current==0. && counters.step_count==0,"transaction fixture advanced simulation");
    std::cout<<"CUDA_JEANS_RUNTIME_TRANSACTIONS_PASS dimension="<<dimension
        <<" root_blocks=4 refined_blocks="<<refined_count
        <<" parent_veto=1 equality_coarsen=1 target64_coarsen=1 nextafter_refine=1 finest_reject=1 stale_host=1"
        <<" time=0 steps=0\n";
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
        std::map<amr::BlockHandle,double> parent_minima;
        for (const auto& group : groups.restrictions) {
            auto& destination = control.pool->GetBlock(proposed_by_handle.at(group.destination.handle));
            auto oracle = destination;
            const amr::Block* children[8]{};
            for (int child = 0; child < 2; ++child)
                children[child] = &control.pool->GetBlock(old_by_handle.at(group.children[child].handle));
            oracle.AverageToCoarse(children, 1, config.numerics.sml_rho, config.numerics.min_eint);
            destination.state_next = oracle.fluid_state;
            double minimum=std::numeric_limits<double>::infinity();
            std::vector<double> fractions(species_count);
            const auto geometry=GridMetrics::make_geometry_view(oracle.grid);
            for(int i=oracle.grid.Is();i<oracle.grid.Ie();++i) {
                const auto cell=oracle.grid.GetIndex(i,0,0);
                for(int sp=0;sp<species_count;++sp) fractions[sp]=oracle.fluid_state.X(sp,cell);
                const auto u=oracle.fluid_state.get(cell);
                const auto p=eos.get_pressure(u,fractions.data());
                const auto c=eos.get_sound_speed(u,p,fractions.data());
                const auto value=JeansDiagnostics::evaluate_cell(u.rho,c*c,geometry,i,0);
                require(value.status==JeansDiagnostics::Status::valid,"invalid parent routing control");
                minimum=std::min(minimum,value.cells);
            }
            parent_minima.emplace(group.destination.handle,minimum);
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
        for(const auto& group:groups.restrictions) {
            std::array<Access,2> children;
            for(int child=0;child<2;++child) {
                const auto id=old_by_handle.at(group.children[child].handle);
                const auto position=std::find(old_ids.begin(),old_ids.end(),id)-old_ids.begin();
                children[child]=active[position];
            }
            const auto& parent=control.pool->GetBlock(proposed_by_handle.at(group.destination.handle));
            const auto before_parent=backend->counters();
            const auto before_store=backend->store_snapshot();
            const auto minimum=backend->evaluate_jeans_parent(children,parent);
            const auto after_parent=backend->counters();
            const auto after_store=backend->store_snapshot();
            const auto reference=parent_minima.at(group.destination.handle);
            require(minimum && std::abs(*minimum-reference)
                <=16*std::numeric_limits<double>::epsilon()*std::abs(reference),
                "device JENS did not consume the same restricted parent EOS");
            require(after_parent.kernel_count-before_parent.kernel_count==4
                && after_parent.bytes_d2h-before_parent.bytes_d2h==sizeof(int)+sizeof(double)
                && after_parent.bytes_h2d==before_parent.bytes_h2d
                && after_parent.stream_sync_count-before_parent.stream_sync_count==2,
                "JENS parent materialized fields or skipped transfer/EOS fences");
            require(after_store.active_blocks==before_store.active_blocks
                && after_store.staged_blocks==before_store.staged_blocks,
                "JENS parent scratch was published into the active/staged namespace");
            auto invalid_children=children;
            ++invalid_children.back().storage.value;
            bool rejected=false;
            try {(void)backend->evaluate_jeans_parent(invalid_children,parent);}
            catch(const std::invalid_argument&) {rejected=true;}
            const auto after_invalid=backend->counters();
            require(rejected && after_invalid.kernel_count==after_parent.kernel_count
                && after_invalid.bytes_h2d==after_parent.bytes_h2d
                && after_invalid.bytes_d2h==after_parent.bytes_d2h
                && after_invalid.stream_sync_count==after_parent.stream_sync_count,
                "late invalid parent child partially enqueued work");
            // Explicit raw-source fault injection tests shared status routing.
            // It is not a physically valid-leaf/nonconvex-parent science witness.
            const auto last_id=old_by_handle.at(children.back().block);
            for(int fault=0;fault<2;++fault) {
                auto damaged=old_states.at(last_id);
                if(fault==0)
                    for(double& e:damaged.eng)e=-100.*std::abs(e);
                else
                    for(double& e:damaged.enuc_rate)e=std::numeric_limits<double>::quiet_NaN();
                upload(children.back(),damaged);
                const auto before_fault=backend->counters();
                bool correctly_rejected=false;
                try {
                    const auto value=backend->evaluate_jeans_parent(children,parent);
                    correctly_rejected=fault==0 && !value.has_value();
                } catch(const std::runtime_error& error) {
                    correctly_rejected=fault==1 && std::string(error.what())
                        ==amr::regrid_math::status_message(amr::regrid_math::Status::RestrictionEnuc);
                }
                const auto after_fault=backend->counters();
                require(correctly_rejected
                    && after_fault.kernel_count-before_fault.kernel_count==2
                    && after_fault.bytes_d2h-before_fault.bytes_d2h==sizeof(int)
                    && after_fault.bytes_h2d==before_fault.bytes_h2d
                    // Fatal propagation executes the existing fail-safe cleanup
                    // fence after the checked restriction fence; veto returns directly.
                    && after_fault.stream_sync_count-before_fault.stream_sync_count
                        ==static_cast<std::uint64_t>(fault==0?1:2),
                    "parent transfer failure reached EOS or lost veto/fatal semantics");
                compare(damaged,download(children.back(),damaged),true);
                upload(children.back(),old_states.at(last_id));
                const auto recovered=backend->evaluate_jeans_parent(children,parent);
                require(recovered && std::abs(*recovered-reference)
                    <=16*std::numeric_limits<double>::epsilon()*std::abs(reference),
                    "parent status/EOS latch or source did not recover");
            }
            std::cout<<"CUDA_JEANS_PARENT_STATUS_PASS species="<<species_count
                <<" coarse_veto=1 restriction_enuc_fatal=1 eos_not_called=1 recovery=1\n";
            std::cout<<"CUDA_JEANS_CANDIDATE_PARENT_PASS species="<<species_count
                <<" children=2 private=1 stale_host=1 late_invalid_reject=1\n";
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

// Native Runtime ownership, not public startup or evolved gravity qualification.
// Uniform physical e gives an independent JENS oracle even with nonzero J/W.
constexpr auto native_regrid_chart = GridMetrics::GeometrySemantics::AxisymmetricRz;
constexpr double native_regrid_rho = 1.e7, native_regrid_e = 1.;
constexpr double native_regrid_gamma = 1.6666666666666667, native_regrid_omega = .25;

std::vector<FluidState> materialize_native_regrid_current(
    arch::driver::DriverRuntime& runtime, const amr::AMRControl& control)
{
    std::vector<FluidState> result;
    const auto& ids = control.tree->GetActiveBlocks();
    require(ids.size() == runtime.handles().size(), "Native materialization domain changed");
    for (int id : ids) result.push_back(control.pool->GetBlock(id).fluid_state);
    for (std::size_t n = 0; n < result.size(); ++n)
        for (const auto region : {Region::Interior, Region::Ghost})
            runtime.backend()->enqueue_materialize_host_current(
                runtime.backend_access(n, Slot::Current), region, transfer(result[n]));
    runtime.backend()->quiesce(); // explicit observer; no ledger publication
    return result;
}

void poison_native_regrid_host(amr::AMRControl& control, bool check_only = false)
{
    constexpr std::array<std::vector<double> FluidState::*, 6> fields{
        &FluidState::rho, &FluidState::mom_u, &FluidState::mom_v,
        &FluidState::mom_w, &FluidState::eng, &FluidState::enuc_rate};
    for (int id : control.tree->GetActiveBlocks()) {
        auto& b = control.pool->GetBlock(id); const auto& g = b.grid;
        // Poison real logical cells only; padded species remain legitimate zero.
        for (int j = 0; j < g.GetTotalY(); ++j) for (int i = 0; i < g.GetTotalX(); ++i) {
            const int c = g.GetIndex(i, j, 0);
            for (auto field : fields) {
                auto& value = (b.fluid_state.*field)[c];
                if (check_only) require(std::isnan(value), "Native regrid materialized a Host fluid field");
                else value = std::numeric_limits<double>::quiet_NaN();
            }
            for (int s = 0; s < b.fluid_state.GetNumSpecies(); ++s) {
                auto& value = b.fluid_state.X(s, c);
                if (check_only) require(std::isnan(value), "Native regrid materialized Host composition");
                else value = std::numeric_limits<double>::quiet_NaN();
            }
        }
    }
}

std::vector<long double> native_regrid_integrals(
    const amr::AMRControl& control, const std::vector<FluidState>& values)
{
    const auto& ids = control.tree->GetActiveBlocks();
    require(values.size() == ids.size(), "Native integral domain changed");
    std::vector<long double> sum(6);
    const long double pi = std::acos(-1.L);
    for (std::size_t n = 0; n < ids.size(); ++n) {
        const auto& g = control.pool->GetBlock(ids[n]).grid; const auto& u = values[n];
        for (int j = g.Js(); j < g.Je(); ++j) for (int i = g.Is(); i < g.Ie(); ++i) {
            const int c = g.GetIndex(i, j, 0);
            const long double l = g.GetFacePosL(i), h = g.GetFacePosR(i), dz = g.dx2;
            const long double v = pi * (h*h - l*l) * dz;
            const long double w = 2.L * pi * (h*h*h - l*l*l) * dz / 3.L;
            sum[0] += u.rho[c] * v; sum[1] += u.eng[c] * v;
            sum[2] += u.mom_w[c] * w; sum[3] += u.enuc_rate[c] * v;
            for (int s = 0; s < 2; ++s) sum[4+s] += u.rho[c] * u.X(s, c) * v;
        }
    }
    return sum;
}

void compare_native_regrid_integrals(const std::vector<long double>& expected,
                                    const std::vector<long double>& actual)
{
    require(expected.size() == actual.size(), "Native integral extent changed");
    for (std::size_t n = 0; n < actual.size(); ++n)
        require(std::isfinite(actual[n]) && expected[n] != 0.L
            && std::abs((actual[n] - expected[n]) / expected[n]) <= 2.e-12L,
            "Native regrid violated independent V/W integral budget");
}

void compare_native_regrid_host(const amr::AMRControl& host, const amr::AMRControl& device,
                               const std::vector<FluidState>& actual)
{
    const auto& host_ids = host.tree->GetActiveBlocks();
    const auto& ids = device.tree->GetActiveBlocks();
    require(host_ids.size() == ids.size() && actual.size() == ids.size(),
        "Native Host/device migration topology extent differs");
    constexpr std::array<std::vector<double> FluidState::*, 6> fields{
        &FluidState::rho, &FluidState::mom_u, &FluidState::mom_v,
        &FluidState::mom_w, &FluidState::eng, &FluidState::enuc_rate};
    for (std::size_t n = 0; n < ids.size(); ++n) {
        const auto& reference = host.pool->GetBlock(host_ids[n]);
        const auto& b = device.pool->GetBlock(ids[n]); const auto& g = b.grid;
        b.RequireNativeGeometryIdentity(); reference.RequireNativeGeometryIdentity();
        require(b.level == reference.level && b.logical_x1 == reference.logical_x1
            && b.logical_x2 == reference.logical_x2 && b.logical_x3 == reference.logical_x3
            && GridMetrics::equal_identity(g.dyadic_identity, reference.grid.dyadic_identity),
            "Native Host/device migration logical chart differs");
        // Interior migration parity; completed real ghosts have their own EOS/ledger gate.
        for (int j = g.Js(); j < g.Je(); ++j) for (int i = g.Is(); i < g.Ie(); ++i) {
            const int c = g.GetIndex(i, j, 0), hc = reference.grid.GetIndex(i, j, 0);
            const auto close = [](double a, double b) {
                return std::isfinite(a) && std::isfinite(b)
                    && std::abs(a - b) <= 3.e-13 * std::max(1., std::abs(a));
            };
            for (auto field : fields) require(close((reference.fluid_state.*field)[hc],
                (actual[n].*field)[c]), "Native migration differs from shared Host transfer");
            for (int s = 0; s < 2; ++s)
                require(close(reference.fluid_state.X(s, hc), actual[n].X(s, c)),
                    "Native migration composition differs from shared Host transfer");
        }
    }
}

double native_uniform_jeans_reference(const Grid& grid)
{
    // Independent uniform-e ideal-gas formula, using only the physical constant
    // and actual RZ dr/dz. No effective closure, EOS call or JENS leaf is reused.
    const long double cs2 = static_cast<long double>(native_regrid_gamma)
        * (native_regrid_gamma - 1.L) * native_regrid_e;
    const long double spacing = std::max(grid.dx1, grid.dx2);
    return static_cast<double>(std::sqrt(std::acos(-1.L) * cs2
        / (arch::constants::gravity::cgs::gravitational_constant * native_regrid_rho)) / spacing);
}

void verify_native_regrid_jeans(arch::driver::DriverRuntime& runtime,
                                const amr::AMRControl& control)
{
    const auto actual = runtime.evaluate_current_jeans_resolution();
    const auto& ids = control.tree->GetActiveBlocks();
    require(actual.size() == ids.size(), "Native JENS lost actual private block order");
    auto context = runtime.stage_context();
    for (std::size_t n = 0; n < ids.size(); ++n) {
        const double expected = native_uniform_jeans_reference(control.pool->GetBlock(ids[n]).grid);
        require(std::isfinite(actual[n]) && std::abs(actual[n] - expected) <= 3.e-13 * expected,
            "Native resident JENS differs from independent uniform-e reference");
        const auto coherence = context.ledger.inspect({runtime.handles()[n], Slot::Current});
        context.ledger.require_readable({runtime.handles()[n], Slot::Current},
            {arch::state::ExecutionSide::Device, coherence.interior.version, true, true});
    }
}

void run_native_regrid_runtime(bool axis)
{
    using namespace arch;
    SimConfig config{}; config.grid.dim = 2; config.grid.geometry = "cylindrical";
    config.grid.nblockx1 = config.grid.nblockx2 = 1; config.grid.nblockx3 = 0;
    config.grid.x1_min = axis ? 0. : 1.; config.grid.x1_max = config.grid.x1_min + 1.;
    config.grid.x2_min = -.5; config.grid.x2_max = .5; config.grid.amr_max_blocks = 16;
    config.grid.x1l_boundary_type = config.grid.x1r_boundary_type = "reflecting";
    config.grid.x2l_boundary_type = config.grid.x2r_boundary_type = "reflecting";
    config.amr.lrefinemin = 0; config.amr.lrefinemax = 1; config.amr.regrid_interval = 1;
    config.amr.refine_on_jeans = true; config.amr.refine_on_rho = true;
    config.amr.refine_var = "DENS"; config.amr.refine_threshold = .1; config.amr.derefine_threshold = .05;
    config.numerics.sml_rho = config.numerics.min_eint = 1.e-14; config.numerics.max_eint = 1.e10;
    config.numerics.solver_name = "HLLC"; config.numerics.reconstruction = "ppm";
    config.numerics.limiter = "minmod"; config.numerics.time_integrator = "euler";
    config.physics.eos_type = "ideal"; config.physics.gamma = native_regrid_gamma;
    config.physics.burn.use_burn = false; config.physics.diffusion.use_diffusion = false;
    config.physics.gravity.type = "self"; config.execution.compute_backend = "cuda";
    SpeciesManager species;
    species.add_species("a", 1., 1., native_regrid_gamma, 1.);
    species.add_species("b", 2., 1., native_regrid_gamma, 1.);
    IdealGas eos(native_regrid_gamma, species);
    amr::AMRControl host_control(16, 2), control(16, 2);
    const auto seed = [&](amr::AMRControl& owner) {
        owner.tree->InitRootGrid(config, 2, native_regrid_chart);
        for (int id : owner.tree->GetActiveBlocks()) {
            auto& b = owner.pool->GetBlock(id); const auto& g = b.grid;
            b.RequireNativeGeometryIdentity();
            for (auto* u : {&b.fluid_state, &b.state_next, &b.state_scratch}) {
                u->stage_repairs.reset(2, state::RepairSemantics::RzVolumeAngular);
                for (int j = 0; j < g.GetTotalY(); ++j) for (int i = 0; i < g.GetTotalX(); ++i) {
                    const int c = g.GetIndex(i, j, 0);
                    const long double left = g.GetFacePosL(i), right = g.GetFacePosR(i);
                    const long double l = std::min(std::abs(left), std::abs(right));
                    const long double h = std::max(std::abs(left), std::abs(right));
                    const long double v = (h*h-l*l)/2.L, w = (h*h*h-l*l*l)/3.L;
                    const long double inertia = (h*h*h*h-l*l*l*l)/4.L;
                    u->set(c, {native_regrid_rho, 0., 0.,
                        double((right <= 0. ? -1. : 1.) * native_regrid_rho * native_regrid_omega * inertia / w),
                        double(native_regrid_rho * native_regrid_e
                            + .5L * native_regrid_rho * native_regrid_omega * native_regrid_omega * inertia / v)});
                    u->enuc_rate[c] = -.125; u->X(0, c) = .6; u->X(1, c) = .4;
                }
            }
        }
    };
    seed(host_control); seed(control);
    host_control.tree->SetJeansEvaluator([&](const FluidVector& u, const double* x,
        const GridMetrics::GeometryView& g, int i, int j) {
        const double p = eos.get_pressure(u, x), cs = eos.get_sound_speed(u, p, x);
        return JeansDiagnostics::evaluate_cell(u.rho, cs*cs, g, i, j);
    });
    BCHandler host_boundary(config, native_regrid_chart), boundary(config, native_regrid_chart);
    host_boundary.bind(eos, species); boundary.bind(eos, species);
    host_boundary.configure_stage(0., boundary::BoundaryPurpose::Hydro);
    boundary.configure_stage(0., boundary::BoundaryPurpose::Hydro);
    RunState start{}; start.repairs.reset(2, state::RepairSemantics::RzVolumeAngular);
    SimulationController host_counters(config, start), counters(config, start);
    driver::DriverRuntime host_runtime(host_control, host_boundary, config, species, host_counters);
    driver::DriverRuntime runtime(control, boundary, config, species, counters);
    host_runtime.bind_native_rz_eos(eos); runtime.bind_native_rz_eos(eos);
    host_runtime.initialize_topology(); runtime.initialize_topology();
    const double root_jeans = native_uniform_jeans_reference(
        control.pool->GetBlock(control.tree->GetActiveBlocks().front()).grid);
    config.amr.jeans_cells = 1.5 * root_jeans; // root deficient, all four fine leaves resolved
    const auto topology = runtime.prepare_backend_bindings();
    std::vector<cuda::CudaBlockBinding> bindings;
    for (const auto& b : topology) bindings.push_back({b.block, b.handle, b.storage, b.physical_boundary});
    runtime.install_backend(cuda::make_cuda_backend(bindings, 0, launch_config(config), species, eos));
    runtime.upload_initial_state(); runtime.ensure_fluid_ghosts();
    auto* const backend = dynamic_cast<cuda::CudaBackend*>(runtime.backend());
    require(backend && runtime.handles().size() == 1, "Native fixture lacks its real one-root backend");
    // Both real initial owners were EOS-accepted before any Host field poison.
    control.tree->SetJeansEvaluator([](const FluidVector&, const double*,
        const GridMetrics::GeometryView&, int, int) -> JeansDiagnostics::Resolution {
        throw std::logic_error("Native Device regrid called the stale Host Jeans evaluator");
    });
    control.tree->SetThermodynamicEvaluator([](const FluidState&, const Grid&,
        GridMetrics::GeometrySemantics, const state::Bounds&, std::vector<double>*,
        std::vector<double>*, std::vector<double>*, std::array<std::vector<double>, 3>*) {
        throw std::logic_error("Native Device regrid called the stale Host thermodynamic evaluator");
    });
    poison_native_regrid_host(control);
    const auto initial = materialize_native_regrid_current(runtime, control);
    const auto initial_totals = native_regrid_integrals(control, initial);
    const long double l = config.grid.x1_min, h = config.grid.x1_max, pi = std::acos(-1.L);
    const long double volume = pi*(h*h-l*l), inertia = pi*(h*h*h*h-l*l*l*l)/2.L;
    compare_native_regrid_integrals({native_regrid_rho*volume,
        native_regrid_rho*native_regrid_e*volume
            + .5L*native_regrid_rho*native_regrid_omega*native_regrid_omega*inertia,
        native_regrid_rho*native_regrid_omega*inertia, -.125L*volume,
        .6L*native_regrid_rho*volume, .4L*native_regrid_rho*volume}, initial_totals);
    verify_native_regrid_jeans(runtime, control);
    const auto roots = runtime.handles(); const auto root_ids = control.tree->GetActiveBlocks();
    const auto root_access = runtime.backend_access(0, Slot::Current);
    const auto before_cold = backend->counters(); int forbidden_callback = 0; bool cold_rejected = false;
    try { (void)runtime.regrid_native_rz_candidate(0, 0., [&] { ++forbidden_callback; }); }
    catch (const std::logic_error&) { cold_rejected = true; }
    const auto after_cold = backend->counters();
    require(cold_rejected && forbidden_callback == 0 && runtime.handles() == roots
        && control.tree->GetActiveBlocks() == root_ids && backend->contains(root_access)
        && backend->store_snapshot().staged_blocks == 0
        && after_cold.kernel_count == before_cold.kernel_count
        && after_cold.bytes_h2d == before_cold.bytes_h2d
        && after_cold.bytes_d2h == before_cold.bytes_d2h
        && after_cold.stream_sync_count == before_cold.stream_sync_count,
        "Device after_host_finalization was not rejected cold");
    const auto cold_state = materialize_native_regrid_current(runtime, control);
    compare(initial.front(), cold_state.front(), true);
    require(host_runtime.perform_regrid(0, 0.) && runtime.perform_regrid(0, 0.)
        && runtime.handles().size() == 4 && control.pool->GetNumActiveBlocks() == 4,
        "Native Runtime did not publish the complete four-child family");
    require(!backend->contains(root_access) && backend->store_snapshot().staged_blocks == 0,
        "Native refinement retained old identity or leaked staged namespace");
    // Prime the committed new-epoch boundary stamp before freezing the
    // source that a private parent veto must preserve. Its original regrid
    // preflight may legitimately refresh a stale ghost completion once.
    runtime.ensure_fluid_ghosts();
    poison_native_regrid_host(control);
    verify_native_regrid_jeans(runtime, control);
    const auto refined = materialize_native_regrid_current(runtime, control);
    compare_native_regrid_host(host_control, control, refined);
    compare_native_regrid_integrals(initial_totals, native_regrid_integrals(control, refined));
    const auto refined_handles = runtime.handles(); const auto refined_ids = control.tree->GetActiveBlocks();
    std::vector<Access> refined_accesses; std::vector<state::SlotCoherence> coherence;
    const auto* const refined_ledger = &runtime.stage_context().ledger;
    for (std::size_t n = 0; n < refined_handles.size(); ++n) {
        refined_accesses.push_back(runtime.backend_access(n, Slot::Current));
        coherence.push_back(runtime.stage_context().ledger.inspect({refined_handles[n], Slot::Current}));
    }
    // Fine leaves resolve 1.5*N_root; a real restricted parent does not.
    require(!host_runtime.perform_regrid(0, 0.) && !runtime.perform_regrid(0, 0.),
        "Native underresolved candidate parent escaped the finite veto loop");
    const auto& vetoes = runtime.native_coarsening_veto_records();
    require(vetoes.size() == 1 && vetoes.front().kind == driver::NativeCoarseningVetoKind::JeansResolution
        && vetoes.front().parent == amr::LogicalBlockKey{2, 0, 0, 0, 0}
        && vetoes.front().scope.from_epoch == refined_handles.front().epoch
        && vetoes.front().scope.to_epoch != vetoes.front().scope.from_epoch
        && vetoes.front().target.epoch == vetoes.front().scope.to_epoch
        && amr::is_valid(vetoes.front().target) && state::is_valid(vetoes.front().version)
        && std::abs(vetoes.front().jeans_minimum - root_jeans) <= 3.e-13 * root_jeans,
        "Native parent veto lacks actual completed staged JENS identity/reference");
    require(runtime.handles() == refined_handles && control.tree->GetActiveBlocks() == refined_ids
        && &runtime.stage_context().ledger == refined_ledger
        && control.pool->GetNumActiveBlocks() == 4 && backend->store_snapshot().active_blocks == 4
        && backend->store_snapshot().staged_blocks == 0, "Native parent veto leaked/published candidate topology");
    bool target_rejected = false;
    try { (void)runtime.stage_context().ledger.inspect({vetoes.front().target, Slot::Current}); }
    catch (const std::logic_error&) { target_rejected = true; }
    require(target_rejected, "Native veto published the rejected private target in the accepted ledger");
    poison_native_regrid_host(control, true);
    const auto restored = materialize_native_regrid_current(runtime, control);
    for (std::size_t n = 0; n < refined_handles.size(); ++n) {
        require(backend->contains(refined_accesses[n]), "Native veto retired an accepted source lease");
        compare(refined[n], restored[n], true);
        const auto after = runtime.stage_context().ledger.inspect({refined_handles[n], Slot::Current});
        require(after.interior.residency == coherence[n].interior.residency
            && after.interior.version == coherence[n].interior.version
            && after.interior.completion == coherence[n].interior.completion
            && after.interior.pending_transfer == coherence[n].interior.pending_transfer
            && after.ghost.residency == coherence[n].ghost.residency
            && after.ghost.version == coherence[n].ghost.version
            && after.ghost.completion == coherence[n].ghost.completion
            && after.ghost.pending_transfer == coherence[n].ghost.pending_transfer
            && after.ghost_source_version == coherence[n].ghost_source_version,
            "Native parent veto changed accepted Current publication");
    }
    config.amr.jeans_cells = .75 * root_jeans;
    require(host_runtime.perform_regrid(0, 0.) && runtime.perform_regrid(0, 0.)
        && runtime.handles().size() == 1 && control.pool->GetNumActiveBlocks() == 1,
        "Native resolved staged parent was not allowed to coarsen");
    for (const auto access : refined_accesses)
        require(!backend->contains(access), "Native coarsening retained an old refined lease");
    require(backend->store_snapshot().staged_blocks == 0, "Native coarsening leaked staged resources");
    verify_native_regrid_jeans(runtime, control);
    const auto coarse = materialize_native_regrid_current(runtime, control);
    compare_native_regrid_host(host_control, control, coarse);
    compare_native_regrid_integrals(initial_totals, native_regrid_integrals(control, coarse));
    require(counters.t_current == 0. && counters.step_count == 0
        && host_counters.t_current == 0. && host_counters.step_count == 0
        && !runtime.active_runtime_state_transaction() && !runtime.native_macro_retry_attempt(),
        "Native regrid fixture advanced a macro or retained a boundary transaction");
    std::cout << "CUDA_NATIVE_REGRID_RUNTIME_PASS axis=" << axis
        << " species=2 root=1 children=4 coarsen=1 completed_parent_jens_veto=1"
        << " stale_host=1 host_transfer_reference=1 cold_callback_reject=1 time=0 steps=0\n";
}

/** Independent quartic physical integrals; common 2*pi*dz cancels in U.
 * M=int rho |r|dr, J=int rho r^3dr, E=e0*M+|J|/2 for Omega=1.
 * Negative logical ghosts have odd m_phi and even rho/E; padding is not read.
 */
static FluidVector quartic_device_regrid_cell(const Grid& grid,int i){
    const long double lo=grid.GetFacePosL(i),hi=grid.GetFacePosR(i);
    require((lo>=0||hi<=0)&&hi>lo,"quartic reference cell straddles the axis");
    const auto power=[](long double x,int n){long double v=1.;for(int k=0;k<n;++k)v*=x;return v;};
    const auto integral=[&](int n){return (power(hi,n+1)-power(lo,n+1))/(n+1);};
    const long double sign=hi<=0?-1.L:1.L;
    const long double volume=sign*integral(1),mass=sign*(32.L*integral(1)-integral(5));
    const long double angular_measure=integral(2),angular=32.L*integral(3)-integral(7);
    constexpr long double internal=1.L/33554432.L;
    return {double(mass/volume),0.,0.,double(angular/angular_measure),
        double((internal*mass+.5L*std::abs(angular))/volume)};
}
/** Genuine fine-source -> private restricted parent -> early typed veto.
 * Workflow: accept analytic fine leaves with the real EOS/BC -> upload and
 * complete real Device ghosts -> freeze source fields/ledger/leases -> poison
 * Host fields -> request coarsening through empty original indicators -> check
 * exact rollback and the one authentic all-active parent thermal diagnostic.
 * JENS is disabled; no bad-energy injection or alternative closure is used.
 */
void run_native_quartic_parent_veto()
{
    using namespace arch;
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    SimConfig config;config.grid.geometry="cylindrical";config.grid.dim=2;
    config.grid.nblockx1=1;config.grid.nblockx2=1;config.grid.nblockx3=0;
    config.grid.x1_min=0.;config.grid.x1_max=1.;config.grid.x2_min=-.5;config.grid.x2_max=.5;
    config.grid.amr_max_blocks=16;config.amr.lrefinemin=0;config.amr.lrefinemax=1;
    config.amr.refine_on_rho=false;config.amr.refine_on_jeans=false;
    config.amr.refine_threshold=.001;config.amr.derefine_threshold=.0005;
    config.amr.regrid_interval=1;
    config.grid.x1r_boundary_type="user";
    config.grid.x2l_boundary_type="user";config.grid.x2r_boundary_type="user";
    config.physics.burn.use_burn=false;config.physics.diffusion.use_diffusion=false;
    config.execution.compute_backend="cuda";
    SpeciesManager species;species.add_species("a",1.,1.,1.4,1.);species.add_species("b",2.,1.,1.4,1.);
    IdealGas eos(1.4,species);
    boundary::ResolvedUserBoundaries callbacks;
    callbacks.identity="quartic-native-coarsening-reference";
    callbacks.physical=[](const boundary::PhysicalBoundaryContext& context){
        const double r=context.ghost_point.x;
        require(std::isfinite(r)&&r>=0.,"quartic physical callback received invalid radius");
        PrimitiveData point;point.rho=32.-r*r*r*r;point.w=r;
        point.SetTemperature(1./33554432.); // Original Cv=1 and physical e0=2^-25.
        point.mass_fractions={.6,.4};
        boundary::PhysicalBoundaryData data;data.hydro=std::move(point);return data;
    };
    boundary::ScopedUserBoundarySelection selected(std::move(callbacks),config,species);
    amr::AMRControl control(16,2);
    control.tree->LoadLeafGrid(config,2,{1,1,1,1},{0,1,0,1},{0,0,1,1},{0,0,0,0},rz);
    require(amr::indicator::make_selection(config.amr,2,control.tree->RefinementSpecies()).empty(),
        "quartic Device fixture did not retain an empty physical indicator selection");
    for(int id:control.tree->GetActiveBlocks()){
        auto& block=control.pool->GetBlock(id);const auto& g=block.grid;
        require(g.Ie()-g.Is()==16&&g.Je()-g.Js()==16
            &&g.GetTotalX()==24&&g.GetTotalY()==24&&g.dx1==1./32.,
            "quartic source is not four genuine 16-active/24-logical fine patches");
        block.RequireNativeGeometryIdentity();
        for(auto* u:{&block.fluid_state,&block.state_next,&block.state_scratch}){
            u->InitSpecies(2);
            u->stage_repairs.reset(2,state::RepairSemantics::RzVolumeAngular);
            for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i){
                const int cell=g.GetIndex(i,j,0);u->set(cell,quartic_device_regrid_cell(g,i));
                u->enuc_rate[cell]=0.;u->X(0,cell)=.6;u->X(1,cell)=.4;
            }
        }
    }
    RunState start{};start.repairs.reset(2,state::RepairSemantics::RzVolumeAngular);
    SimulationController counters(config,start);
    BCHandler boundary_handler(config,rz);boundary_handler.bind(eos,species);
    boundary_handler.configure_stage(0.,boundary::BoundaryPurpose::Hydro);
    driver::DriverRuntime runtime(control,boundary_handler,config,species,counters);
    runtime.bind_native_rz_eos(eos);runtime.initialize_topology();
    const auto topology=runtime.prepare_backend_bindings();
    std::vector<cuda::CudaBlockBinding> bindings;
    for(const auto& b:topology)bindings.push_back({b.block,b.handle,b.storage,b.physical_boundary});
    runtime.install_backend(cuda::make_cuda_backend(bindings,0,launch_config(config),species,eos));
    runtime.upload_initial_state();runtime.ensure_fluid_ghosts();
    auto* const backend=dynamic_cast<cuda::CudaBackend*>(runtime.backend());
    require(backend&&runtime.handles().size()==4&&control.pool->GetNumActiveBlocks()==4
        &&backend->store_snapshot().active_blocks==4&&backend->store_snapshot().staged_blocks==0,
        "quartic source lacks its actual four-fine Device store");

    // Explicit observers use independent destination copies; they do not
    // materialize Runtime Host fields or publish fake synchronized metadata.
    const auto initial=materialize_native_regrid_current(runtime,control);
    const auto totals=native_regrid_integrals(control,initial);
    constexpr long double pi=3.141592653589793238462643383279502884L;
    const auto relative=[](long double value,long double expected){return std::abs((value-expected)/expected);};
    require(relative(totals[0],95.L*pi/3.L)<=1.e-12L
        &&relative(totals[2],63.L*pi/4.L)<=1.e-12L
        &&relative(totals[1],(95.L/3.L/33554432.L+63.L/8.L)*pi)<=1.e-12L,
        "quartic Device source M/J/E differ from original independent antiderivatives");
    const auto source_ids=control.tree->GetActiveBlocks();
    const auto source_handles=runtime.handles();
    const auto* const handles_address=runtime.handles().data();
    const auto* const source_ledger=&runtime.stage_context().ledger;
    const auto source_epoch=source_ledger->active_epoch();
    const auto clock_version=runtime.stage_context().clock.last_version();
    const auto parent_geometry=control.tree->CandidateParentGeometry(source_ids);
    const auto boundary_frame=boundary_handler.snapshot_stage_context();
    std::vector<Access> source_accesses;
    std::vector<state::SlotCoherence> source_coherence;
    std::vector<std::array<const double*,7>> source_addresses;
    const auto addresses=[](const FluidState& u){
        return std::array<const double*,7>{u.rho.data(),u.mom_u.data(),u.mom_v.data(),
            u.mom_w.data(),u.eng.data(),u.enuc_rate.data(),u.mass_fractions.data()};
    };
    for(std::size_t n=0;n<source_ids.size();++n){
        source_accesses.push_back(runtime.backend_access(n,Slot::Current));
        const auto coherence=source_ledger->inspect({source_handles[n],Slot::Current});
        source_ledger->require_readable({source_handles[n],Slot::Current},
            {state::ExecutionSide::Device,coherence.interior.version,true,true});
        source_coherence.push_back(coherence);
        source_addresses.push_back(addresses(control.pool->GetBlock(source_ids[n]).fluid_state));
    }
    control.tree->SetJeansEvaluator([](const FluidVector&,const double*,
        const GridMetrics::GeometryView&,int,int)->JeansDiagnostics::Resolution{
        throw std::logic_error("quartic Device JENS-off regrid called the Host Jeans evaluator");
    });
    control.tree->SetThermodynamicEvaluator([](const FluidState&,const Grid&,
        GridMetrics::GeometrySemantics,const state::Bounds&,std::vector<double>*,
        std::vector<double>*,std::vector<double>*,std::array<std::vector<double>,3>*){
        throw std::logic_error("quartic Device regrid called the Host thermodynamic evaluator");
    });
    poison_native_regrid_host(control);
    require(!runtime.perform_regrid(0,0.),"inadmissible quartic Device parent was published");
    const auto& records=runtime.native_coarsening_veto_records();
    require(records.size()==1,"quartic Device restriction did not yield one exact early parent veto");
    const auto& record=records.front();
    require(record.parent==amr::LogicalBlockKey{2,0,0,0,0}
        &&record.kind==driver::NativeCoarseningVetoKind::EffectiveThermal
        &&record.scope.from_epoch==source_epoch&&record.scope.to_epoch!=source_epoch
        &&record.scope.transaction_id!=0&&record.target.epoch==record.scope.to_epoch
        &&amr::is_valid(record.target)&&state::is_valid(record.version)
        &&record.version.value>clock_version
        &&record.version.value==runtime.stage_context().clock.last_version()
        &&std::find(source_handles.begin(),source_handles.end(),record.target)==source_handles.end(),
        "quartic Device veto lost actual restricted parent/scope/migrated version identity");
    require(record.diagnostic.has_value(),"quartic Device veto omitted its actual closure diagnostic");
    const auto& d=*record.diagnostic;const auto& pg=parent_geometry.grid;
    require(d.phase==RzThermodynamics::AcceptancePhase::effective_thermal&&d.inertia_mapping_valid
        &&d.status==state::Status::unresolved_energy&&d.node==-1
        &&d.i>=pg.Is()+1&&d.i<pg.Ie()-1&&d.j>=pg.Js()&&d.j<pg.Je()
        &&d.index==pg.GetIndex(d.i,d.j,0),
        "quartic Device veto did not identify an authentic all-active three-rho parent closure");
    require(runtime.backend()==backend&&runtime.handles()==source_handles
        &&runtime.handles().data()==handles_address&&control.tree->GetActiveBlocks()==source_ids
        && &runtime.stage_context().ledger==source_ledger&&source_ledger->active_epoch()==source_epoch
        &&control.pool->GetNumActiveBlocks()==4&&backend->store_snapshot().active_blocks==4
        &&backend->store_snapshot().staged_blocks==0&&boundary_handler.stage_context_matches(boundary_frame),
        "quartic Device veto changed source leases/topology/ledger/BC or leaked a staged namespace");
    bool target_rejected=false;
    try{(void)source_ledger->inspect({record.target,Slot::Current});}
    catch(const std::logic_error&){target_rejected=true;}
    require(target_rejected,"quartic Device veto published its private target in the accepted ledger");
    poison_native_regrid_host(control,true);
    const auto restored=materialize_native_regrid_current(runtime,control);
    for(std::size_t n=0;n<source_ids.size();++n){
        const auto access=runtime.backend_access(n,Slot::Current);
        require(access.block==source_accesses[n].block&&access.storage==source_accesses[n].storage
            &&access.slot==source_accesses[n].slot&&backend->contains(source_accesses[n])
            &&addresses(control.pool->GetBlock(source_ids[n]).fluid_state)==source_addresses[n],
            "quartic Device veto changed an accepted storage generation or borrowed Host array lease");
        compare(initial[n],restored[n],true);
        const auto after=source_ledger->inspect({source_handles[n],Slot::Current});
        const auto& before=source_coherence[n];
        const auto same_region=[](const state::RegionCoherence& a,const state::RegionCoherence& b){
            return a.residency==b.residency&&a.version==b.version&&a.completion==b.completion
                &&a.pending_transfer==b.pending_transfer;
        };
        require(same_region(before.interior,after.interior)&&same_region(before.ghost,after.ghost)
            &&after.ghost_source_version==before.ghost_source_version,
            "quartic Device veto changed actual Current interior/ghost publication");
        source_ledger->require_readable({source_handles[n],Slot::Current},
            {state::ExecutionSide::Device,before.interior.version,true,true});
    }
    poison_native_regrid_host(control,true);
    require(!config.amr.refine_on_jeans&&counters.t_current==0.&&counters.step_count==0
        &&!runtime.active_runtime_state_transaction()&&!runtime.native_macro_retry_attempt(),
        "quartic Device JENS-off fixture advanced a macro or retained a boundary owner");
    std::cout<<"CUDA_NATIVE_QUARTIC_PARENT_VETO_PASS analytic_quartic=1 source_fine_eos=1"
        <<" jens_off=1 exact_parent=1 early_thermal=1 active_three_rho=1 valid_migrated_version=1"
        <<" active_blocks=4 source_device_bits_ledger_leases=1 host_poison=1 staged_namespace=0"
        <<" physical_e0=2^-25 time=0 steps=0\n";
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
        run_jeans_runtime_lease();
        for(int dimension=1;dimension<=3;++dimension)run_jeans_runtime_transactions(dimension);
        run_jeans_runtime_transactions(1,true);
        run(4);
        run(41);
        run_native_regrid_runtime(true);
        run_native_regrid_runtime(false);
        run_native_quartic_parent_veto();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
