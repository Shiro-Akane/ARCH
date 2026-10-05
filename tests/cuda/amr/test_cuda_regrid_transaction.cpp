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
#include "physics/eos/IdealGas.h"
#include "fixtures/amr/regrid_migration_fixture.h"

#include <cuda_runtime_api.h>
#include <bit>
#include <cmath>
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
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
