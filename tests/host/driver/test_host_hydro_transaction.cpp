/** @file test_host_hydro_transaction.cpp
 * @brief Host boundary authority and shared Runtime CPU/Device Hydro routes.
 * Preserve actual fields, three slots and metadata on rejected preflight.
 */
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <memory>
#include <iostream>
#include <iomanip>
#include <numbers>
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
#include "physics/gravity/GravityDispatch.h"
#include "physics/gravity/GravityExecution.h"
#include "driver/stages/GravityStage.h"
#include <filesystem>

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

/** Observe actual gravity backend allocation/work without changing its math.
 * The disabled factory creates no field owner; its allocation counter stays
 * zero through the same Runtime path used by active policies.
 */
class GravityAllocationWitness final:public multigrid::CompositeExecution {
    std::shared_ptr<multigrid::CompositeExecution> owner_;
public:
    std::size_t allocations=0,bytes=0;
    explicit GravityAllocationWitness(std::shared_ptr<multigrid::CompositeExecution> owner):owner_(std::move(owner)){}
    bool device()const override{return owner_->device();}
    std::shared_ptr<void> allocate(std::size_t n)override{++allocations;bytes+=n;return owner_->allocate(n);}
    void copy(void* to,const void* from,std::size_t n,multigrid::Transfer t)override{owner_->copy(to,from,n,t);}
    void run(const multigrid::CompositeWork& w)override{owner_->run(w);}
    double reduce(const multigrid::Reduction& r)override{return owner_->reduce(r);}
    void fence()override{owner_->fence();}
    void project(multigrid::Vector& x,const multigrid::Vector& w)override{owner_->project(x,w);}
    multigrid::ExecutionCounters counters()const override{return owner_->counters();}
};
class GravityExecutionWitness final:public Physical::Gravity::GravityExecution {
    std::shared_ptr<Physical::Gravity::GravityExecution> owner_=Physical::Gravity::make_host_gravity_execution();
public:
    std::shared_ptr<GravityAllocationWitness> memory=std::make_shared<GravityAllocationWitness>(owner_->numeric());
    std::size_t gathers=0,boundaries=0,verified_density_cells=0;
    std::shared_ptr<multigrid::CompositeExecution> numeric()const override{return memory;}
    void run(const Physical::Gravity::GravityWork& work)override {
        owner_->run(work);
        if(const auto* gather=std::get_if<Physical::Gravity::GatherDensity>(&work)) {
            ++gathers;
            for(int c=0;c<gather->size;++c) {
                const auto location=gather->cells[c];
                require(gather->out[c]==gather->patches[location.block][location.offset],"gravity gathered stale density");
                ++verified_density_cells;
            }
        }
        if(std::holds_alternative<Physical::Gravity::EvaluateBoundary>(work))++boundaries;
    }
};
struct GravitySourceBudget {
    std::mutex lock;std::array<long double,3> change{};std::size_t body_calls=0,flux_calls=0;
    bool axial_walls=false;double z_lower=0.,z_upper=0.;long double wall_axial=0.;
    void boundary_impulse(const std::vector<FluidVector>& flux,const Grid& grid,double dt,int dir,double weight) {
        if(!axial_walls||dir!=1)return;
        long double impulse=0.;
        for(int i=grid.Is();i<grid.Ie();++i) {
            if(grid.x2_min==z_lower) {
                const int j=grid.Js();impulse+=GridMetrics::FaceArea(grid,1,i,j,0,false)*flux[grid.GetIndex(i,j,0)].mom_v;
            }
            if(grid.x2_max==z_upper) {
                const int j=grid.Je()-1;impulse-=GridMetrics::FaceArea(grid,1,i,j,0,true)*flux[grid.GetIndex(i,j+1,0)].mom_v;
            }
        }
        std::lock_guard guard(lock);wall_axial+=dt*weight*impulse;
    }
    void add(const std::vector<FluidVector>& before,const std::vector<FluidVector>& after,
        const Grid& grid,double weight,bool body) {
        std::array<long double,3> delta{};
        for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
            const int c=grid.GetIndex(i,j,0);
            const long double v=GridMetrics::CellVolume(grid,i,j,0),w=GridMetrics::AngularMomentumVolumeMoment(grid,i,j,0);
            delta[0]+=(static_cast<long double>(after[c].eng)-before[c].eng)*v;
            delta[1]+=(static_cast<long double>(after[c].mom_w)-before[c].mom_w)*w;
            delta[2]+=(static_cast<long double>(after[c].mom_v)-before[c].mom_v)*v;
        }
        std::lock_guard guard(lock);for(int c=0;c<3;++c)change[c]+=weight*delta[c];
        if(body)++body_calls;else ++flux_calls;
    }
};
/** Intercept the original source calls only, preserving SelfGravity receipts. */
class GravitySourceWitness final:public Physical::Gravity::IGravityPolicy {
    const Physical::Gravity::IGravityPolicy& owner_;GravitySourceBudget& budget_;double weight_;
public:
    GravitySourceWitness(const Physical::Gravity::IGravityPolicy& owner,GravitySourceBudget& budget,double weight)
        :owner_(owner),budget_(budget),weight_(weight){}
    void add_sources_on_patch(std::vector<FluidVector>& delta,const FluidState& state,const Grid& grid,double dt,void* stream=nullptr)const override {
        const auto before=delta;owner_.add_sources_on_patch(delta,state,grid,dt,stream);budget_.add(before,delta,grid,weight_,true);
    }
    void add_flux_work_on_patch(std::vector<FluidVector>& delta,const std::vector<FluidVector>& flux,const FluidState& state,const Grid& grid,double dt,int dir)const override {
        const auto before=delta;owner_.add_flux_work_on_patch(delta,flux,state,grid,dt,dir);budget_.add(before,delta,grid,weight_,false);
        budget_.boundary_impulse(flux,grid,dt,dir,weight_);
    }
};
/** Borrow the actual RK flux quadrature weight; no extra stage or solve. */
class GravityHydroWitness final:public Numerics::IHydroSolver {
    const Numerics::IHydroSolver& owner_;GravitySourceBudget& budget_;
public:
    GravityHydroWitness(const Numerics::IHydroSolver& owner,GravitySourceBudget& budget):owner_(owner),budget_(budget){}
    Numerics::HostHydroStorageContract host_storage_contract()const noexcept override{return owner_.host_storage_contract();}
    void evaluate_patch(amr::AMRControl* control,int block,const FluidState& state,const Grid& grid,double dt,
        std::vector<FluidVector>& delta,std::vector<double>& species,const Physical::Gravity::IGravityPolicy* gravity,
        const NumericsConfig& numerics,double weight=1.,void* stream=nullptr,const boundary::HostHydroBoundaryAuthority* walls=nullptr)const override {
        if(!gravity) {owner_.evaluate_patch(control,block,state,grid,dt,delta,species,nullptr,numerics,weight,stream,walls);return;}
        const GravitySourceWitness witness(*gravity,budget_,weight);
        owner_.evaluate_patch(control,block,state,grid,dt,delta,species,&witness,numerics,weight,stream,walls);
    }
    void update_patch(const FluidState& old,const FluidState& current,FluidState& next,const std::vector<FluidVector>& delta,
        const std::vector<double>& species,const Grid& grid,double old_weight,double flux_weight,const NumericsConfig& numerics,void* stream=nullptr)const override {
        owner_.update_patch(old,current,next,delta,species,grid,old_weight,flux_weight,numerics,stream);
    }
};

/** Actual gravity-off Runtime/RK/BC/reflux route with PWC angular witnesses.
 * The fixed-work mode borrows this same route, suppressing file output. Its
 * clock includes Runtime boundaries/reflux and must not be called a leaf cost.
 */
void curved_runtime(const char* geometry,double inner,int roots,int steps,
    dispatch::TimeIntegratorId method,driver::IntegratorSolve integrator,bool mixed,
    bool adaptive=false,double rotation=.08,int diffusion=0,bool cost=false,int reconstruction=0,bool runtime_regrid=false,int gravity_kind=0,bool device=false)
{
    GravitySourceBudget source_budget;
    const bool angular=std::string(geometry)=="cylindrical";
    auto config=settings(method);config.grid.geometry=geometry;
    config.grid.nblockx1=config.grid.nblockx2=roots;
    config.grid.x1_min=inner;config.grid.x1_max=inner+1.;config.grid.x2_min=0.;config.grid.x2_max=1.;
    if(cost) {
        // Match the frozen CYL-0 steady MUSCL/MC rphi work, including its
        // full azimuth. Native cylindrical z keeps its unit physical length.
        config.grid.x2_max=angular?1.:2.*std::numbers::pi;config.grid.x3_max=1.;
        config.numerics.sml_rho=1.e-12;config.numerics.min_eint=1.e-10;config.numerics.max_eint=1.e6;
        config.numerics.hll_roe_wave_speed=true;
    }
    config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="reflecting";
    config.grid.x2l_boundary_type=config.grid.x2r_boundary_type="periodic";
    const auto gravity_id=gravity_kind==0?dispatch::GravityId::None:(gravity_kind<3?dispatch::GravityId::External:dispatch::GravityId::Self);
    config.physics.gravity.type=gravity_kind==0?"none":(gravity_kind<3?"external":"self");
    config.physics.gravity.g_x=0.;config.physics.gravity.g_y=.2;config.physics.gravity.g_z=gravity_kind==2?.03:0.;
    if(gravity_kind>=3) {
        config.physics.gravity.boundary=gravity_kind==3?"dirichlet":"isolated";
        config.physics.gravity.relative_tolerance=1e-12;config.physics.gravity.absolute_tolerance=0.;config.physics.gravity.max_cycles=300;
        if(gravity_kind==4)config.grid.x2l_boundary_type=config.grid.x2r_boundary_type="reflecting";
        config.io.out_dir="output/release/cyl3-20261018/runtime-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    }
    source_budget.axial_walls=gravity_kind==4;source_budget.z_lower=config.grid.x2_min;source_budget.z_upper=config.grid.x2_max;
    config.amr.lrefinemax=(mixed||runtime_regrid)?1:0;config.grid.amr_max_blocks=roots*roots+(cost?8:16);
    if(runtime_regrid) {
        config.amr.refine_on_rho=true;config.amr.refine_threshold=.001;
        config.amr.derefine_threshold=.0005;
    }
    if(diffusion) {
        config.physics.diffusion.use_diffusion=config.physics.diffusion.use_viscous_diffusion=true;
        config.physics.diffusion.use_thermal_diffusion=config.physics.diffusion.use_species_diffusion=true;
        config.physics.diffusion.nu_visc=.03;config.physics.diffusion.alpha_therm=.015;
        config.physics.diffusion.D_spec=.02;config.physics.diffusion.diff_cfl=.8;config.physics.diffusion.max_stages=16;
    }
    SpeciesManager species;
    // Cost lanes match the actual Gaussian checkpoint: BgGas/PassiveGas,
    // equal A/Z/gamma/cv, fractions [1,0], and its spare-block capacity.
    // Numerical mixed-species lanes retain their original two definitions.
    if(cost) {species.add_species("BgGas",1.,1.,1.4,717.5);
        species.add_species("PassiveGas",1.,1.,1.4,717.5);}
    else {species.add_species("X",1.,1.,1.4,1.);species.add_species("Y",2.,1.,1.4,1.);}
    const int species_count=species.count();
    IdealGas eos(1.4,species);amr::AMRControl control(config.grid.amr_max_blocks,config.grid.dim);
    control.tree->InitRootGrid(config,species_count);control.flux_register.EnsureSpecies(species_count);
    for(int id:control.tree->GetActiveBlocks()) {
        auto& b=control.pool->GetBlock(id);const auto g=GridMetrics::make_geometry_view(b.grid);
        for(auto* slot:slots(b))for(int j=0;j<b.grid.GetTotalY();++j)for(int i=0;i<b.grid.GetTotalX();++i) {
            const int c=b.grid.GetIndex(i,j,0);const double r=b.grid.GetCellCenterX(i),z=b.grid.GetCellCenterY(j);
            const double wave=cost?0.:std::sin(2.*std::numbers::pi*z),rho=1.+.05*wave;
            const double lever=angular?GridMetrics::AngularMomentumVolumeMoment(g,i,j,0)/GridMetrics::CellVolume(g,i,j,0):r;
            const double azimuth=cost?0.:rotation*lever,axial=.03*wave,radial=0.;
            const double v=angular?axial:azimuth,w=angular?azimuth:axial;
            slot->set(c,{rho,rho*radial,rho*v,rho*w,(cost?1./(1.4-1.):5.)+.5*rho*(v*v+w*w)});
            slot->X(0,c)=cost?1.:.4+.03*wave;
            slot->X(1,c)=cost?0.:1.-slot->X(0,c);
        }
    }
    if(gravity_kind>=3) {
        // Scale rho and all conserved densities together so CGS gravity is
        // dynamically visible; specific energy and sound speed are unchanged.
        const double scale=1./constants::gravity::cgs::gravitational_constant;
        for(int id:control.tree->GetActiveBlocks())for(auto* state:slots(control.pool->GetBlock(id)))
            for(int c=0;c<static_cast<int>(state->rho.size());++c)state->set(c,state->get(c)*scale);
    }
    auto totals=[&]() {
        std::array<long double,6> total{};
        for(int id:control.tree->GetActiveBlocks()) {
            const auto& b=control.pool->GetBlock(id);
            for(int j=b.grid.Js();j<b.grid.Je();++j)for(int i=b.grid.Is();i<b.grid.Ie();++i) {
                const int c=b.grid.GetIndex(i,j,0);const auto u=b.fluid_state.get(c);
                const long double v=GridMetrics::CellVolume(b.grid,i,j,0);
                const long double w=GridMetrics::AngularMomentumVolumeMoment(b.grid,i,j,0);
                total[0]+=u.rho*v;total[1]+=u.eng*v;total[2]+=u.rho*b.fluid_state.X(0,c)*v;
                total[3]+=(angular?u.mom_w:u.mom_v)*w;total[4]+=std::abs(angular?u.mom_w:u.mom_v)*w;
                total[5]+=(angular?u.mom_v:u.mom_w)*v;
            }
        }
        return total;
    };
    const auto initial=totals();
    auto check=[&](const auto& reference,const char* phase,bool evolution=false) {
        const auto final=totals();
        for(int n=0;n<3;++n) {
            const long double expected=reference[n]+(evolution&&n==1?source_budget.change[0]:0.L);
            if(std::abs(final[n]-expected)>2.e-11L+2.e-12L*std::abs(reference[n]))
                std::cerr<<"GRAVITY_BUDGET_FAILURE field="<<n<<" mixed="<<mixed<<" kind="<<gravity_kind<<" observed="<<static_cast<double>(final[n]-reference[n])<<" source="<<static_cast<double>(expected-reference[n])<<" residual="<<static_cast<double>(final[n]-expected)<<'\n';
            require(std::abs(final[n]-expected)<=2.e-11L+2.e-12L*std::abs(reference[n]),phase);
        }
        require(std::abs(final[5]-reference[5]-(evolution?source_budget.change[2]+source_budget.wall_axial:0.L))<=2.e-11L+2.e-12L*std::abs(reference[5]),"curved Runtime axial momentum conservation");
        if(angular)require(std::abs(final[3]-reference[3]-(evolution?source_budget.change[1]:0.L))<=1.e-12L*reference[4],phase);
        for(int id:control.tree->GetActiveBlocks()) {
            const auto& b=control.pool->GetBlock(id);
            for(double value:b.fluid_state.stage_repairs.values)require(value==0.,"curved Runtime repaired a state");
            for(int j=b.grid.Js();j<b.grid.Je();++j)for(int i=b.grid.Is();i<b.grid.Ie();++i) {
                const int c=b.grid.GetIndex(i,j,0);double fractions[]{b.fluid_state.X(0,c),cost?0.:b.fluid_state.X(1,c)};
                require(eos.get_pressure(b.fluid_state.get(c),fractions)>0.,"curved Runtime actual EOS gate");
            }
        }
    };
    if(mixed) {
        require(control.tree->Regrid(config,[&](const auto& tree){
            const int first=tree.GetActiveBlocks().front();
            for(int id:tree.GetActiveBlocks())control.pool->GetBlock(id).refine_flag=id==first?1:0;
        }),"mixed curved fixture failed to refine");check(initial,"refinement conservation");
    }
    // Explicit closed diffusive surfaces: the existing reflecting Hydro
    // ghosts alone do not prescribe zero cylindrical tangential traction.
    std::optional<boundary::ScopedUserBoundarySelection> boundary_selection;
    if(diffusion) {
        boundary::ResolvedUserBoundaries callbacks;
        callbacks.physical=[](const boundary::PhysicalBoundaryContext& context) {
            boundary::PhysicalBoundaryData data;
            if(context.purpose==boundary::BoundaryPurpose::Diffusion) {
                data.temperature={boundary::ScalarBoundaryKind::OutwardFlux,0.};
                for(auto& velocity:data.velocity)velocity={boundary::ScalarBoundaryKind::OutwardFlux,0.};
                data.species.assign(context.species.count(),{boundary::ScalarBoundaryKind::OutwardFlux,0.});
            }
            return data;
        };
        boundary_selection.emplace(std::move(callbacks),config,species);
    }
    RunState start;start.repairs.reset(species_count);SimulationController controller(config,start);
    BCHandler bc(config);bc.bind(eos,species);driver::DriverRuntime runtime(control,bc,config,species,controller);
    runtime.initialize_topology();
    dispatch::ResolvedExecutionPlan plan{};plan.time_integrator=method;
    plan.flux=dispatch::FluxId::Hllc;
    plan.reconstruction=(cost||reconstruction==1)?dispatch::ReconstructionId::Muscl:dispatch::ReconstructionId::Pcm;
    plan.limiter=dispatch::LimiterId::Mc;
    if(diffusion)plan.diffusion_integrator=diffusion==1?dispatch::DiffusionIntegratorId::Rkl1:dispatch::DiffusionIntegratorId::Rkl2;
    if(device) {
#if ARCH_CUDA_BUILD_ENABLED
        require(gravity_kind==0,"Device conservation fixture qualifies gravity-off only");
        runtime.ensure_fluid_ghosts();
        const auto topology=runtime.prepare_backend_bindings();
        std::vector<cuda::CudaBlockBinding> bindings;
        for(const auto& b:topology)bindings.push_back({b.block,b.handle,b.storage,b.physical_boundary,b.hydro_boundary});
        runtime.install_backend(cuda::make_cuda_backend(bindings,0,
            cuda::make_cuda_launch_config(plan,config),species,eos));
        runtime.upload_initial_state();
#else
        throw std::runtime_error("Device conservation fixture requires a CUDA build");
#endif
    }
    auto policy=Physical::Gravity::make_gravity(config,gravity_id);
    auto gravity_execution=std::make_shared<GravityExecutionWitness>();
    auto* self=dynamic_cast<Physical::Gravity::SelfGravity*>(policy.get());
    if(self)self->set_execution(gravity_execution);
    driver::GravityStage gravity_stage(runtime,policy.get());
    gravity_stage.prepare_current(0.,true);
    if(gravity_kind==0)require(!policy&&!gravity_stage.active(),"disabled gravity created an owner");
    if(gravity_kind<3)require(!gravity_stage.active()&&gravity_stage.plot_fields().empty(),"nonself field owner created");
    std::uint64_t boundary_prepare_calls=0;
    const auto make_context=[&]() {
        auto context=runtime.stage_context();
        context.hydro_preparation=gravity_stage.active()?&gravity_stage:nullptr;
        context.configure_boundary_context=[&](double t,boundary::BoundaryPurpose p){bc.configure_stage(t,p);};
        // Mirror Driver.h: builtin-only domains already have the ordinary
        // timestep/input and RK output boundary owners. An unconditional hook
        // would repeat that work and distort comparison with production.
        if(bc.has_user())context.physical_boundary_preparation=[&](StateSlot slot,double t,boundary::BoundaryPurpose p){++boundary_prepare_calls;bc.configure_stage(t,p);runtime.ensure_fluid_ghosts(slot);};
        return context;
    };
    if(runtime_regrid) {
        gravity_stage.invalidate();
        const auto retired=runtime.handles();
        require(runtime.perform_regrid(0,0.),"actual curved Runtime failed to refine");
        require(runtime.handles().size()>retired.size()&&runtime.handles().front().epoch!=retired.front().epoch,"refinement retained old Runtime topology identity");
        auto current=runtime.stage_context();bool rejected=false;
        try{(void)current.ledger.inspect({retired.front(),StateSlot::Current});}catch(const std::exception&){rejected=true;}
        require(rejected,"refinement accepted a retired Runtime handle");
        if(device)runtime.materialize_current_for_host();
        check(initial,"actual Runtime refinement conservation");
    }
    Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> hydro(eos);
    Numerics::HydroSolverImpl<IdealGas,FluxHLLC<MusclReconstruction<McLimiter>>> cost_hydro(eos);
    const Numerics::IHydroSolver* selected_solver=(cost||reconstruction==1)?static_cast<const Numerics::IHydroSolver*>(&cost_hydro):&hydro;
    GravityHydroWitness source_witness(*selected_solver,source_budget);
    const Numerics::IHydroSolver* solver=gravity_kind?&source_witness:selected_solver;
    driver::DriverStageWorkspace workspace;
    // Small fixed dt is below the acoustic/source limits at both mesh sizes;
    // the budget is work-based (64 RK2 steps), not a changed science endpoint.
    const auto stage_count=scheduler::make_hydro_plan(method==dispatch::TimeIntegratorId::Euler?scheduler::HydroMethod::Euler:
        (method==dispatch::TimeIntegratorId::Rk2?scheduler::HydroMethod::RK2:scheduler::HydroMethod::RK3)).stages.size();
    std::size_t expected_source_patches=0;
    const double fixed_dt=1.e-4/roots,target=.02;
    double time=0.,dt_min=std::numeric_limits<double>::max(),dt_max=0.,hydro_seconds=0.;int executed=0;
    const auto begin=std::chrono::steady_clock::now();
    for(int step=0;step<steps&&(adaptive?time<target:true);++step) {
        if(runtime_regrid&&step==2) {
            // Exercise coarsening with legal indicator thresholds, without
            // changing accepted fields, PDE, timestep policy or tolerances.
            if(device)runtime.materialize_current_for_host();
            const auto before=totals();const auto retired=runtime.handles();
            config.amr.refine_threshold=1.;config.amr.derefine_threshold=.9;
            require(runtime.perform_regrid(step,time),"actual curved Runtime failed to coarsen");
            require(runtime.handles().size()==std::size_t(roots*roots)&&runtime.handles().front().epoch!=retired.front().epoch,"coarsening failed Runtime topology publication");
            auto current=runtime.stage_context();bool rejected=false;
            try{(void)current.ledger.inspect({retired.back(),StateSlot::Current});}catch(const std::exception&){rejected=true;}
            require(rejected,"coarsening accepted a retired Runtime handle");
            // The new topology owns resident Device interiors. Observe only
            // after explicit download, just as at refinement and final output.
            if(device)runtime.materialize_current_for_host();
            check(before,"actual Runtime coarsening conservation");
        }
        // Every macrostep borrows the live ledger/clock, including after
        // topology publication; no context outlives a regrid transaction.
        auto context=make_context();
        double dt=fixed_dt;
        double dt_fe=0.;
        if(!diffusion&&!adaptive) {
            bc.configure_stage(time,boundary::BoundaryPurpose::Hydro);
            runtime.ensure_fluid_ghosts(StateSlot::Current);
        }
        if(diffusion) {
            bc.configure_stage(time,boundary::BoundaryPurpose::Diffusion);runtime.ensure_fluid_ghosts(StateSlot::Current);
            dt_fe=std::numeric_limits<double>::max();
            if(!device)for(int id:control.tree->GetActiveBlocks()) {
                const auto& b=control.pool->GetBlock(id);
                dt_fe=std::min(dt_fe,DiffFlux::adaptive_dt_diff(b.fluid_state,eos,b.grid,config,1.));
            }
            if(device) {
                std::vector<backend::BackendStateAccess> current;
                for(std::size_t n=0;n<runtime.handles().size();++n)current.push_back(runtime.backend_access(n,StateSlot::Current));
                const auto values=runtime.backend()->compute_diffusion_dt_batch(current);
                dt_fe=*std::min_element(values.begin(),values.end());
            }
            dt=3.*dt_fe; // Exercise a true multistage STS step, not its FE limit.
        }
        if(adaptive) {
            bc.configure_stage(time,boundary::BoundaryPurpose::Hydro);runtime.ensure_fluid_ghosts(StateSlot::Current);
            dt=std::numeric_limits<double>::max();
            for(int id:control.tree->GetActiveBlocks()) {
                const auto& b=control.pool->GetBlock(id);
                dt=std::min(dt,adaptive_dt(b.fluid_state,eos,b.grid,.8,false));
            }
            dt=std::min(dt,target-time);
        }
        require(std::isfinite(dt)&&dt>0.,"curved physical endpoint invalid dt");
        dt_min=std::min(dt_min,dt);dt_max=std::max(dt_max,dt);
        if(device)for(int id:control.tree->GetActiveBlocks())
            for(auto* host:slots(control.pool->GetBlock(id)))
                std::fill(host->rho.begin(),host->rho.end(),std::numeric_limits<double>::quiet_NaN());
        context.step_start_time=time;context.step_dt=dt;context.boundary_start_time=time;context.boundary_step_dt=dt;
        scheduler::ScopedStageBinding binding(context,runtime.handles());
        try {
            if(diffusion)driver::advance_diffusion(runtime,workspace,context,eos,&plan,step,dt,dt_fe);
            else {
                // Match CYL-0 cpu_stage_timings.hydro: input/timestep preparation
                // is outside, while RK output ghosts and reflux remain inside.
                const auto hydro_begin=std::chrono::steady_clock::now();
                if(policy)expected_source_patches+=runtime.handles().size()*stage_count;
                driver::advance_hydro(runtime,workspace,context,&plan,dt,integrator,policy.get(),solver);
                hydro_seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-hydro_begin).count();
            }
        } catch(...) {
            std::cerr<<"CURVED_FAILURE step="<<step<<" time="<<time<<" dt="<<dt<<" rotation="<<rotation<<'\n';
            // Diagnose actual wall bars in all three slots; no state repair.
            for(int id:control.tree->GetActiveBlocks()) {
                auto& b=control.pool->GetBlock(id);
                for(int s=0;s<3;++s)for(int side=0;side<2;++side) {
                    if((side==0?b.grid.x1_min:b.grid.x1_max)!=(side==0?config.grid.x1_min:config.grid.x1_max))continue;
                    const auto* state=slots(b)[s];const int i=side==0?b.grid.Is():b.grid.Ie()-1;
                    for(int j=b.grid.Js();j<b.grid.Je();++j) {
                        const int cell=b.grid.GetIndex(i,j,0);const auto base=state->get(cell);const auto k=state::recover(base);
                        double x[]{state->X(0,cell),cost?0.:state->X(1,cell)};const double p=eos.get_pressure(base,x),c=eos.get_sound_speed(base,p,x);
                        FluidVector low{};const bool wall=StationarySlipWallFlux::point_wall_flux(base,x,eos,0,side,p,c,low);
                        const auto f=get_flux(base,p,0);const double speed=std::abs(base.mom_u/base.rho)+c;
                        const auto bar=base+((side==0?low-f:f-low)/speed);const auto kb=state::recover(bar);
                        if(!wall||!FluxAdmissibility::valid(bar)) {
                            std::cerr<<"CURVED_WALL slot="<<s<<" side="<<side<<" j="<<j<<" rho="<<base.rho
                                <<" ur="<<k.u<<" uphi="<<k.w<<" e="<<k.internal<<" p="<<p<<" c="<<c
                                <<" wall="<<wall<<" pwall="<<low.mom_u<<" bar_rho="<<bar.rho<<" bar_e="<<kb.internal<<'\n';break;
                        }
                    }
                }
            }
            throw;
        }
        gravity_stage.invalidate();gravity_stage.flush_committed_diagnostics();
        for(double value:runtime.repair_budget().values)
            require(value==0.,"curved Runtime accumulated an earlier stage repair");
        time+=dt;++executed;
    }
    if(adaptive)require(time==target,"curved adaptive physical endpoint not reached");
    const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    if(device)runtime.materialize_current_for_host();
    check(initial,"curved Runtime conservative evolution",true);const auto final=totals();
    std::cout<<std::setprecision(17)<<"CURVED_RUNTIME geometry="<<geometry<<" inner="<<inner<<" roots="<<roots
        <<" device="<<device<<" patches="<<control.tree->GetActiveBlocks().size()<<" steps="<<executed<<" method="<<config.numerics.time_integrator
        <<" mixed="<<mixed<<" seconds="<<seconds<<" hydro_seconds="<<hydro_seconds
        <<" boundary_prepare_calls="<<boundary_prepare_calls<<" J_error="<<static_cast<double>(initial[4]>0.?std::abs(final[3]-initial[3]-source_budget.change[1])/initial[4]:std::abs(final[3]-initial[3]-source_budget.change[1]))
        <<" mass_error="<<static_cast<double>(std::abs(final[0]-initial[0])/initial[0])
        <<" energy_error="<<static_cast<double>(std::abs(final[1]-initial[1])/initial[1])
        <<" species_error="<<static_cast<double>(std::abs(final[2]-initial[2])/initial[2])
        <<" axial_momentum_absolute_error="<<static_cast<double>(std::abs(final[5]-initial[5]))
        <<" adaptive="<<adaptive<<" rotation="<<rotation<<" physical_time="<<time
        <<" dt_min="<<dt_min<<" dt_max="<<dt_max<<" diffusion="<<diffusion<<" cost="<<cost
        <<" species="<<species_count<<" max_blocks="<<config.grid.amr_max_blocks
        <<" runtime_regrid="<<runtime_regrid<<" regrid_records="<<runtime.regrid_records().size()
        <<" source_body_calls="<<source_budget.body_calls<<" source_flux_calls="<<source_budget.flux_calls
        <<" gravity_kind="<<gravity_kind<<" gravity_gathers="<<gravity_execution->gathers
        <<" gravity_allocations="<<gravity_execution->memory->allocations<<" gravity_allocated_bytes="<<gravity_execution->memory->bytes
        <<" gravity_boundary_evaluations="<<gravity_execution->boundaries<<" verified_density_cells="<<gravity_execution->verified_density_cells
        <<" gravity_energy_budget_relative="<<static_cast<double>(std::abs(final[1]-initial[1]-source_budget.change[0])/std::abs(initial[1]))
        <<" torque_impulse="<<static_cast<double>(source_budget.change[1])<<" axial_wall_impulse="<<static_cast<double>(source_budget.wall_axial)
        <<" reconstruct="<<((cost||reconstruction==1)?"muscl_mc":"pcm")<<" repairs=0\n";
    require(source_budget.body_calls==expected_source_patches&&source_budget.flux_calls==2*expected_source_patches,
        "gravity patch body/work source was repeated or omitted");
    if(gravity_kind<3)require(gravity_execution->gathers==0&&gravity_execution->memory->allocations==0&&gravity_execution->boundaries==0,
        "off/external gravity solved or allocated dedicated fields");
    else require(gravity_execution->gathers==1+executed*stage_count&&gravity_execution->memory->allocations>0,
        "self gravity solve count differs from actual RK density inputs");
    if(runtime_regrid)require(runtime.regrid_records().size()==2&&
        control.ghost_exchange.PlanCacheBuilds()>=2,"actual Runtime regrid/cache lifecycle was not exercised");
    if(mixed&&!device) {
        require(control.tree->Regrid(config,[&](const auto& tree){
            for(int id:tree.GetActiveBlocks())control.pool->GetBlock(id).refine_flag=-1;
        }),"mixed curved fixture failed to coarsen");check(final,"coarsening conservation");
    }
}

} // namespace

/** Added to the existing gravity_stage_contract executable; no new broad CI campaign. */
void test_host_hydro_transaction(){
    hydro_input_frame_metadata_roles();ordinary_hydro_domain_root_faces();
    ordinary_runtime_wall_routes();

}

/** Bounded modes of the existing executable, retaining its ordinary default. */
void test_curved_no_gravity(bool cost) {
    if(cost) {
        for(int roots:{4,8})for(int repeat=0;repeat<2;++repeat)
            for(const char* chart:{"spherical","cylindrical","cylindrical","spherical"})
            curved_runtime(chart,1.,roots,64,arch::dispatch::TimeIntegratorId::Rk2,selected_rk2,false,false,0.,0,true);
        return;
    }
    for(double inner:{0.,1.}) {
        curved_runtime("cylindrical",inner,2,8,arch::dispatch::TimeIntegratorId::Euler,selected_euler,false);
        curved_runtime("cylindrical",inner,2,8,arch::dispatch::TimeIntegratorId::Rk2,selected_rk2,false);
        curved_runtime("cylindrical",inner,2,8,arch::dispatch::TimeIntegratorId::Rk3,selected_rk3,false);
        curved_runtime("cylindrical",inner,2,8,arch::dispatch::TimeIntegratorId::Rk2,selected_rk2,true);
        for(double rotation:{.08,2.,20.})
            curved_runtime("cylindrical",inner,2,200,arch::dispatch::TimeIntegratorId::Rk2,selected_rk2,false,true,rotation);
        for(int diffusion:{1,2})for(bool mixed:{false,true})
            curved_runtime("cylindrical",inner,2,4,arch::dispatch::TimeIntegratorId::Rk2,selected_rk2,mixed,false,.08,diffusion);
        for(int diffusion:{0,2})
            curved_runtime("cylindrical",inner,2,4,arch::dispatch::TimeIntegratorId::Rk2,selected_rk2,false,false,.08,diffusion,false,0,true);
    }
}

/** Diagnostic control: distinguish shared wall admissibility from the new
 * mixed angular measure, without treating a failed stress lane as accepted. */
void test_curved_reference_stress() {
    curved_runtime("spherical",1.,2,2000,arch::dispatch::TimeIntegratorId::Rk2,selected_rk2,false,true,20.);
    curved_runtime("cylindrical",1.,2,2000,arch::dispatch::TimeIntegratorId::Rk2,selected_rk2,false,true,20.,0,false,1);
}

/** Optional fields use the same native Runtime, RK, BC and AMR owners. */
void test_curved_optional_gravity() {
    for(double inner:{0.,1.})for(int gravity:{0,1,2,3,4}) {
        curved_runtime("cylindrical",inner,2,4,arch::dispatch::TimeIntegratorId::Euler,selected_euler,false,false,.08,0,false,0,false,gravity);
        curved_runtime("cylindrical",inner,2,4,arch::dispatch::TimeIntegratorId::Rk2,selected_rk2,false,false,.08,0,false,0,false,gravity);
        curved_runtime("cylindrical",inner,2,4,arch::dispatch::TimeIntegratorId::Rk3,selected_rk3,true,false,.08,0,false,0,false,gravity);
    }
    for(int gravity:{3,4})curved_runtime("cylindrical",0.,2,4,arch::dispatch::TimeIntegratorId::Rk2,selected_rk2,false,false,.08,0,false,0,true,gravity);
}

/** Reuse the CPU conservation budgets with real resident Device stages.
 * Poison Host storage after preparation; only explicit output materialization
 * restores it. Axis/annulus, every RK, mixed levels and RKL use existing owners.
 */
void test_curved_device_runtime() {
    for(double inner:{0.,1.}) {
        curved_runtime("cylindrical",inner,2,8,arch::dispatch::TimeIntegratorId::Euler,selected_euler,false,false,.08,0,false,0,false,0,true);
        curved_runtime("cylindrical",inner,2,8,arch::dispatch::TimeIntegratorId::Rk2,selected_rk2,false,false,.08,0,false,0,false,0,true);
        curved_runtime("cylindrical",inner,2,8,arch::dispatch::TimeIntegratorId::Rk3,selected_rk3,true,false,.08,0,false,0,false,0,true);
        curved_runtime("cylindrical",inner,2,4,arch::dispatch::TimeIntegratorId::Rk2,selected_rk2,false,false,.08,0,false,1,true,0,true);
        for(int order:{1,2})curved_runtime("cylindrical",inner,2,4,arch::dispatch::TimeIntegratorId::Rk2,selected_rk2,true,false,.08,order,false,0,false,0,true);
    }
}

/** Matched fixed-work Device cost samples; this is not a science endpoint. */
void test_curved_device_cost() {
    for(int roots:{4,8})for(int repeat=0;repeat<4;++repeat)
        for(int order=0;order<2;++order) {
            const char* chart=((repeat+order)%2)?"spherical":"cylindrical";
            curved_runtime(chart,1.,roots,64,arch::dispatch::TimeIntegratorId::Rk2,
                selected_rk2,false,false,0.,0,true,0,false,0,true);
        }
}
