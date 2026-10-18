/** @file test_host_hydro_transaction.cpp
 * @brief Ordinary Host boundary authority and selected Runtime Hydro routes.
 * Preserve actual fields, three slots and metadata on rejected preflight.
 */
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "amr/AMRControl.h"
#include "driver/schedule/DriverControl.h"
#include "driver/stages/DriverStages.h"
#include "numerics/flux/FluxHLLC.h"
#include "numerics/integrator/HydroSolverImpl.h"
#include "numerics/integrator/TimeIntegratorEuler.h"
#include "numerics/integrator/TimeIntegratorRK2.h"
#include "numerics/integrator/TimeIntegratorRK3.h"
#include "numerics/reconstruction/Reconstruction.h"
#include "physics/boundary/BoundaryDiagnostics.h"
#include "physics/boundary/UserBoundary.h"
#include "physics/eos/IdealGas.h"

namespace {
using namespace arch;
using state::StateSlot;
using scheduler::StageDescriptor;
using Field=std::vector<double> FluidState::*;
constexpr std::array<Field,7> fields{&FluidState::rho,&FluidState::mom_u,&FluidState::mom_v,
    &FluidState::mom_w,&FluidState::eng,&FluidState::enuc_rate,&FluidState::mass_fractions};
void require(bool okay,const char* text){if(!okay)throw std::runtime_error(text);}
bool bits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
bool bits(const std::vector<double>& a,const std::vector<double>& b){
    return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](double a,double b){return bits(a,b);});
}
bool repairs_equal(const state::RepairBudget& a,const state::RepairBudget& b){
    return bits(a.values,b.values)&&a.block_uid==b.block_uid&&a.stage==b.stage
        &&bits(a.time,b.time)&&std::equal(std::begin(a.position),std::end(a.position),std::begin(b.position),
            [](double a,double b){return bits(a,b);});
}
std::array<FluidState*,3> slots(amr::Block& b){return {&b.fluid_state,&b.state_next,&b.state_scratch};}
/** Preserve the pre-step allocation/alias identities independently of the transaction implementation. */
struct FieldWitness {
    int id;
    std::array<FluidState,3> values;
    std::array<std::array<const double*,7>,3> addresses{};
    std::array<std::optional<boundary::BoundaryFluxCaptureStorage>,3> captures;
    explicit FieldWitness(amr::Block& block,int pool_id)
        : id(pool_id),values{block.fluid_state,block.state_next,block.state_scratch} {
        auto live=slots(block);
        for(int s=0;s<3;++s) {
            for(int f=0;f<7;++f)addresses[s][f]=(live[s]->*fields[f]).data();
            if(live[s]->boundary_flux_capture)captures[s]=*live[s]->boundary_flux_capture;
        }
    }
    /** Check all seven arrays, original slot mapping, receipts and original mutable alias pointees. */
    void matches(amr::AMRControl& control,bool address_check=true) const {
        const auto live=slots(control.pool->GetBlock(id));
        for(int s=0;s<3;++s) {
            const auto& a=*live[s];const auto& b=values[s];
            require(a.n_species_==b.n_species_&&a.block_total_size_==b.block_total_size_,"slot scalar layout changed");
            for(int f=0;f<7;++f) {
                require(bits(a.*fields[f],b.*fields[f]),"slot field payload changed");
                if(address_check)require((a.*fields[f]).data()==addresses[s][f],"original field allocation/slot mapping changed");
            }
            require(repairs_equal(a.stage_repairs,b.stage_repairs),"slot repair receipt/provenance changed");
            if(address_check)require(a.diffusion_boundary==b.diffusion_boundary,"immutable diffusion pointer changed");
            if(address_check)require(a.boundary_flux_capture==b.boundary_flux_capture,"capture nullness/alias/pointee changed");
            require(bool(a.boundary_flux_capture)==bool(captures[s]),"capture presence changed");
            if(captures[s]) {
                const auto& actual=*a.boundary_flux_capture;const auto& saved=*captures[s];
                for(int face=0;face<6;++face) {
                    require(bits(actual.stage[face],saved.stage[face]),"capture stage planes changed");
                    require(bits(actual.initial[face],saved.initial[face]),"capture initial planes changed");
                }
                require(bits(actual.weight,saved.weight)&&bits(actual.initial_weight,saved.initial_weight)
                    &&actual.save_initial==saved.save_initial,"capture weights/flags changed");
            }
        }
    }
};
std::vector<FieldWitness> capture_fields(amr::AMRControl& c){
    std::vector<FieldWitness> out;
    for(int id:c.tree->GetActiveBlocks())out.emplace_back(c.pool->GetBlock(id),id);
    return out;
}

/** Execute production Euler with the actual Runtime-bound scheduler. */
void selected_euler(amr::AMRControl& c,double dt,BCHandler& bc,
    const Physical::Gravity::IGravityPolicy* gravity,const Numerics::IHydroSolver* hydro,const NumericsConfig& n){
    SolverEuler::solve(c,dt,bc,gravity,hydro,n);
}
/** Execute production SSPRK2 with the actual Runtime-bound scheduler. */
void selected_rk2(amr::AMRControl& c,double dt,BCHandler& bc,
    const Physical::Gravity::IGravityPolicy* gravity,const Numerics::IHydroSolver* hydro,const NumericsConfig& n){
    SolverRK2::solve(c,dt,bc,gravity,hydro,n);
}
void selected_rk3(amr::AMRControl& c,double dt,BCHandler& bc,
    const Physical::Gravity::IGravityPolicy* gravity,const Numerics::IHydroSolver* hydro,const NumericsConfig& n){
    SolverRK3::solve(c,dt,bc,gravity,hydro,n);
}
/** Freeze a method-specific configuration before the Runtime borrows it. */
SimConfig settings(dispatch::TimeIntegratorId method){
    SimConfig c;c.grid.dim=2;c.grid.geometry="cartesian";c.grid.nblockx1=2;c.grid.nblockx2=1;c.grid.nblockx3=0;
    c.grid.amr_max_blocks=8;c.grid.x1_min=1.;c.grid.x1_max=3.;c.grid.x2_min=-1.;c.grid.x2_max=1.;
    c.amr.lrefinemin=0;c.amr.lrefinemax=0;
    switch(method) {
        case dispatch::TimeIntegratorId::Euler:c.numerics.time_integrator="Euler";break;
        case dispatch::TimeIntegratorId::Rk2:c.numerics.time_integrator="RK2";break;
        case dispatch::TimeIntegratorId::Rk3:c.numerics.time_integrator="RK3";break;
        default:throw std::invalid_argument("unsupported fixture Hydro method");
    }
    c.numerics.sml_rho=1.e-20;c.numerics.min_eint=1.e-20;c.numerics.max_eint=1.e99;

    return c;
}
/** The closed metadata role cannot be confused with a surface or survive a move.
 * The ordinary frame borrows actual arrays and local geometry without granting
 * scheduler or wall authority.
 */
void hydro_input_frame_metadata_roles() {
    SimConfig ordinary=settings(dispatch::TimeIntegratorId::Euler);
    ordinary.grid.geometry="cartesian";
    amr::AMRControl control{8,2};control.tree->InitRootGrid(ordinary,1);
    auto& actual=control.pool->GetBlock(control.tree->GetActiveBlocks().front());
    BCHandler handler(ordinary);handler.configure_stage(2.,boundary::BoundaryPurpose::Hydro);
    auto frame=handler.capture_hydro_input_frame(actual.fluid_state,actual.grid,2.);
    handler.require_hydro_input_frame(frame,actual.fluid_state,actual.grid,2.);
    actual.grid.x1_max=std::nextafter(actual.grid.x1_max,std::numeric_limits<double>::infinity());
    bool rejected=false;
    try{handler.require_hydro_input_frame(frame,actual.fluid_state,actual.grid,2.);}
    catch(const std::logic_error&){rejected=true;}
    require(rejected,"ordinary Hydro metadata accepted changed physical bounds");
}

/** Ordinary authority borrows real builtin ghosts and the actual Tree/ledger.
 * The configured integer roots independently fix external versus internal sides;
 * the domain frame checks actual boundary metadata before patch execution.
 */
void ordinary_hydro_domain_root_faces() {
    for(int dim:{1,2,3}) {
        SimConfig config;config.grid.dim=dim;config.grid.geometry="cartesian";
        config.grid.x1_min=1.;config.grid.x1_max=3.;
        config.grid.x2_min=-1.;config.grid.x2_max=1.;
        config.grid.x3_min=-1.;config.grid.x3_max=1.;
        config.grid.nblockx1=2;config.grid.nblockx2=dim>=2?1:0;config.grid.nblockx3=dim==3?1:0;
        config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="reflecting";
        config.grid.x2l_boundary_type=config.grid.x2r_boundary_type="reflecting";
        config.grid.x3l_boundary_type=config.grid.x3r_boundary_type="reflecting";
        config.grid.amr_max_blocks=8;config.amr.lrefinemin=config.amr.lrefinemax=0;
        amr::AMRControl control{8,dim};control.tree->InitRootGrid(config,1);
        SpeciesManager species;species.add_species("X",1.,1.,1.4,1.);
        IdealGas eos(1.4,species);BCHandler boundary_handler(config);
        boundary_handler.configure_stage(2.,boundary::BoundaryPurpose::Hydro);
        const auto& ids=control.tree->GetActiveBlocks();
        require(ids.size()==2,"ordinary domain fixture lacks two actual root leaves");
        topology::TopologyIdentityRegistry topology{{dim,{2,1,1},0}};
        std::vector<topology::TopologyObservation> observations;
        for(int id:ids) {
            auto& b=control.pool->GetBlock(id);
            observations.push_back({id,{dim,b.level,b.logical_x1,b.logical_x2,b.logical_x3}});
            for(int cell=0;cell<b.grid.GetTotalSize();++cell) {
                b.fluid_state.set(cell,{2.,0.,0.,0.,20.});b.fluid_state.X(0,cell)=1.;
            }
            boundary_handler.apply(b.fluid_state,b.grid);
        }
        std::vector<amr::BlockHandle> handles;
        auto proposed=topology.stage_adoption(observations);
        topology.commit_after_success(std::move(proposed),[&](const auto& value){handles=value.handles_in_observation_order;});
        control.BindActiveHandles(handles);
        state::StateResidencyLedger ledger{handles.front().epoch};scheduler::MonotonicSchedulerClock clock;
        scheduler::StageExecutionContext context{state::ExecutionSide::Host,ledger,clock};
        context.step_start_time=2.;context.step_dt=.125;
        scheduler::StageBinding binding{context,handles};
        const auto stage=scheduler::supported_hydro_time_plan(scheduler::HydroMethod::Euler).stages.front();
        const auto publication=clock.next_publication();
        for(std::size_t n=0;n<ids.size();++n) {
            auto& b=control.pool->GetBlock(ids[n]);
            for(int k=b.grid.Ks();k<b.grid.Ke();++k)for(int j=b.grid.Js();j<b.grid.Je();++j)
                for(int i=b.grid.Is();i<b.grid.Ie();++i) {
                    const int index=b.grid.GetIndex(i,j,k);
                    require(eos.get_pressure(b.fluid_state.get(index),&b.fluid_state.X(0,index))>0.,
                        "ordinary physical fixture failed its actual IdealGas input");
                }
            ledger.register_block(handles[n],publication.version,publication.completion);
            ledger.publish_ghost({handles[n],StateSlot::Current},state::ExecutionSide::Host,
                publication.version,clock.next_completion());
        }
        boundary::HostHydroBoundaryDomainAuthority domain(boundary_handler,control,binding,stage);
        for(std::size_t n=0;n<ids.size();++n) {
            auto& b=control.pool->GetBlock(ids[n]);
            boundary::HostHydroBoundaryAuthority patch(domain,n,b.id,b.fluid_state,b.grid);
            const auto walls=patch.require_view(&control,b.id,b.fluid_state,b.grid);
            require(walls.reflecting[0]==(b.logical_x1==0)
                &&walls.reflecting[1]==(b.logical_x1==1),"ordinary wall identity guessed an internal root face");
            for(int axis=1;axis<3;++axis)
                require(walls.reflecting[2*axis]==(axis<dim)&&walls.reflecting[2*axis+1]==(axis<dim),
                    "ordinary wall identity lost an active side or granted an inactive side");
        }
        domain.require_complete_domain();
        // Reuse this genuine ordinary frame for all three refusal categories.
        // The wrong-chart check precedes allocation/EOS in the concrete solver;
        // version and stage-clock faults reach its original require_view gate.
        auto& actual=control.pool->GetBlock(ids.front());
        boundary::HostHydroBoundaryAuthority patch(domain,0,actual.id,actual.fluid_state,actual.grid);
        Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> ordinary_solver(eos);
        std::vector<FluidVector> dU(actual.grid.GetTotalSize(),FluidVector{7.,11.,13.,17.,19.});
        std::vector<double> d_spec(actual.grid.GetTotalSize(),23.);
        const auto original_dU=dU;const auto original_spec=d_spec;
        const auto original_fields=capture_fields(control);
        const auto original_register=control.flux_register.snapshot_host();
        const auto unchanged=[&]{
            require(dU.size()==original_dU.size()&&bits(d_spec,original_spec),
                "ordinary refusal changed output extent/species scratch");
            for(std::size_t index=0;index<dU.size();++index) {
                const auto& now=dU[index];const auto& prior=original_dU[index];
                require(bits(now.rho,prior.rho)&&bits(now.mom_u,prior.mom_u)
                    &&bits(now.mom_v,prior.mom_v)&&bits(now.mom_w,prior.mom_w)
                    &&bits(now.eng,prior.eng),
                    "ordinary refusal mutated divergence sentinel");
            }
            for(const auto& witness:original_fields)witness.matches(control);
            require(control.flux_register.host_snapshot_matches(original_register),
                "ordinary refusal mutated original flux register");
        };
        const auto evaluate=[&](const Numerics::IHydroSolver& solver){
            solver.evaluate_patch(&control,actual.id,actual.fluid_state,actual.grid,.125,
                dU,d_spec,nullptr,config.numerics,1.,nullptr,&patch);
        };
        {
            // A genuine later Host publication invalidates the original ghosts.
            // Restore only this test's prepared metadata lease after observing
            // that both consumers leave the deliberately faulty ledger untouched.
            auto before=ledger.snapshot_metadata(arch::state::ExecutionSide::Host);ledger.freeze_metadata_snapshot(before);
            const auto token=clock.last_token(),version=clock.last_version();
            const auto changed=clock.next_publication();
            ledger.publish_interior({handles.front(),StateSlot::Current},state::ExecutionSide::Host,
                changed.version,changed.completion);
            const auto faulty=ledger.inspect({handles.front(),StateSlot::Current});
            bool view_refused=false,solver_refused=false;
            try{(void)patch.require_view(&control,actual.id,actual.fluid_state,actual.grid);}
            catch(const std::logic_error& error){
                view_refused=std::string(error.what())=="Hydro wall domain input publication changed";
                if(!view_refused)throw;
            }
            try{evaluate(ordinary_solver);}catch(const std::logic_error& error){
                solver_refused=std::string(error.what())=="Hydro wall domain input publication changed";
                if(!solver_refused)throw;
            }
            require(view_refused&&solver_refused,"ordinary authority reused a stale readable version");unchanged();
            const auto after=ledger.inspect({handles.front(),StateSlot::Current});
            require(after.interior.version==faulty.interior.version
                &&after.interior.completion==faulty.interior.completion
                &&after.ghost.residency==faulty.ghost.residency
                &&after.ghost_source_version==faulty.ghost_source_version
                &&clock.last_token()==changed.completion.value&&clock.last_version()==changed.version.value,
                "stale-version refusal rewrote the presented ledger/clock fault");
            ledger.restore_metadata_snapshot_noexcept(before);ledger.release_metadata_snapshot(before);
            clock=scheduler::MonotonicSchedulerClock(token,version);
            (void)patch.require_view(&control,actual.id,actual.fluid_state,actual.grid);
        }
        {
            // Actual stage input time is the authority's frozen clock datum;
            // its monotone completion counter is not an immutable authority key.
            const auto before=ledger.snapshot_metadata(arch::state::ExecutionSide::Host);const auto start=context.step_start_time;
            const auto token=clock.last_token(),version=clock.last_version();
            context.step_start_time=std::nextafter(start,std::numeric_limits<double>::infinity());
            bool view_refused=false,solver_refused=false;
            try{(void)patch.require_view(&control,actual.id,actual.fluid_state,actual.grid);}
            catch(const std::logic_error& error){
                view_refused=std::string(error.what())=="Hydro wall domain stage/context changed";
                if(!view_refused)throw;
            }
            try{evaluate(ordinary_solver);}catch(const std::logic_error& error){
                solver_refused=std::string(error.what())=="Hydro wall domain stage/context changed";
                if(!solver_refused)throw;
            }
            require(view_refused&&solver_refused,"ordinary authority ignored the changed actual stage clock");unchanged();
            require(ledger.metadata_snapshot_matches(before)&&clock.last_token()==token&&clock.last_version()==version,
                "stage-clock refusal changed publication metadata");
            context.step_start_time=start;
            (void)patch.require_view(&control,actual.id,actual.fluid_state,actual.grid);
        }
        domain.require_complete_domain();
        config.grid.x1l_boundary_type="outflow";bool rejected=false;
        try{domain.require_complete_domain();}catch(const std::logic_error&){rejected=true;}
        require(rejected,"ordinary domain accepted a changed raw BC policy token");
    }
}


/** Execute the actual ordinary reflecting Runtime route for each selected method.
 * Root-drift negatives snapshot after legitimate ghost preparation: they prove
 * preflight rejection, not a nonexistent ordinary macro transaction rollback.
 */
void ordinary_runtime_wall_routes() {
    const std::array<std::pair<dispatch::TimeIntegratorId,driver::IntegratorSolve>,3> methods{{
        {dispatch::TimeIntegratorId::Euler,selected_euler},
        {dispatch::TimeIntegratorId::Rk2,selected_rk2},
        {dispatch::TimeIntegratorId::Rk3,selected_rk3}}};
    for(const auto& [method,integrator]:methods)for(bool root_drift:{false,true}) {
        auto config=settings(method);config.grid.dim=1;config.grid.geometry="cartesian";
        config.grid.nblockx2=0;
        config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="reflecting";
        SpeciesManager species;species.add_species("X",1.,1.,1.4,1.);
        IdealGas eos(1.4,species);amr::AMRControl control{8,1};
        control.tree->InitRootGrid(config,1);control.flux_register.EnsureSpecies(1);
        for(int id:control.tree->GetActiveBlocks()) {
            auto& block=control.pool->GetBlock(id);
            for(auto* slot:slots(block))for(int index=0;index<block.grid.GetTotalSize();++index) {
                slot->set(index,{2.,0.,0.,0.,20.});slot->X(0,index)=1.;
            }
        }
        RunState start;start.repairs.reset(1);
        SimulationController controller(config,start);BCHandler bc(config);bc.bind(eos,species);
        driver::DriverRuntime runtime(control,bc,config,species,controller);
        runtime.initialize_topology();auto context=runtime.stage_context();
        context.step_start_time=2.;context.step_dt=.125;
        context.boundary_start_time=2.;context.boundary_step_dt=.125;
        context.configure_boundary_context=[&](double time,boundary::BoundaryPurpose purpose){bc.configure_stage(time,purpose);};
        std::vector<FieldWitness> prepared;
        context.physical_boundary_preparation=[&](StateSlot slot,double time,boundary::BoundaryPurpose purpose){
            bc.configure_stage(time,purpose);runtime.ensure_fluid_ghosts(slot);
            if(root_drift) {
                prepared=capture_fields(control);
                config.grid.x1_max=std::nextafter(config.grid.x1_max,std::numeric_limits<double>::infinity());
            }
        };
        Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> hydro(eos);
        dispatch::ResolvedExecutionPlan plan{};plan.time_integrator=method;
        driver::DriverStageWorkspace workspace;bool rejected=false;
        {
            scheduler::ScopedStageBinding binding(context,runtime.handles());
            try{driver::advance_hydro(runtime,workspace,context,&plan,context.step_dt,
                integrator,nullptr,&hydro);}
            catch(const std::logic_error& error){
                if(!root_drift)throw;
                // The exact Tree/config root owner must reject before patch mathematics.
                const std::string text=error.what();
                rejected=text.find("root")!=std::string::npos||text.find("configuration")!=std::string::npos;
                if(!rejected)throw;
            }
        }
        require(rejected==root_drift,"ordinary wall route lost its real root gate");
        if(root_drift) {
            require(prepared.size()==runtime.handles().size(),"negative never reached real ghost preparation");
            for(const auto& witness:prepared)witness.matches(control);
        } else {
            for(int id:control.tree->GetActiveBlocks()) {
                const auto& block=control.pool->GetBlock(id);
                for(int i=block.grid.Is();i<block.grid.Ie();++i) {
                    const auto index=block.grid.GetIndex(i,block.grid.Js(),block.grid.Ks());
                    const double fraction=block.fluid_state.X(0,index);
                    require(eos.get_pressure(block.fluid_state.get(index),&fraction)>0.,
                        "ordinary reflected actual Hydro output rejected selected EOS");
                }
            }
        }
    }
}

} // namespace

/** Added to the existing gravity_stage_contract executable; no new broad CI campaign. */
void test_host_hydro_transaction(){
    hydro_input_frame_metadata_roles();ordinary_hydro_domain_root_faces();
    ordinary_runtime_wall_routes();

}
