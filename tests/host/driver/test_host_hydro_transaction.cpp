/**
 * @file test_host_hydro_transaction.cpp
 * @brief Real Runtime Hydro rollback and independent Euler/RK repair-receipt tests.
 *
 * Engineering fixture: deterministic injected Hydro and a genuinely tentative
 * preparation journal. It proves lifecycle/rollback, not physical source accuracy.
 * Euler/RK2/RK3 accounting witnesses inject diagnostic impulses, independently
 * count accepted receipts, and compare the unchanged physical state against a
 * no-diagnostic run. These impulses are not physical RZ floor corrections.
 * Post-boundary rejection tests use real BC/exchange and Runtime rollback;
 * their injected ghost poison is an engineering ordering witness, not an EOS reference.
 * Native preflight negatives call each actual selected integrator without a
 * transaction, so unchanged populated registers/fields prove rejection before
 * Clear or patch execution rather than successful restoration afterward.
 * No scientific tolerance, floor, public RZ gate or production method is changed.
 */
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "amr/AMRControl.h"
#include "driver/runtime/HostHydroTransaction.h"
#include "driver/schedule/DriverControl.h"
#include "driver/stages/DriverMacroStep.h"
#include "driver/stages/DriverStages.h"
#include "numerics/integrator/TimeIntegratorEuler.h"
#include "numerics/integrator/TimeIntegratorRK2.h"
#include "numerics/integrator/TimeIntegratorRK3.h"
#include "physics/boundary/UserBoundary.h"
#include "physics/eos/IdealGas.h"

namespace {
using namespace arch;
using state::StateSlot;
using scheduler::StageDescriptor;
constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
using Field=std::vector<double> FluidState::*;
constexpr std::array<Field,7> fields{&FluidState::rho,&FluidState::mom_u,&FluidState::mom_v,
    &FluidState::mom_w,&FluidState::eng,&FluidState::enuc_rate,&FluidState::mass_fractions};
void require(bool okay,const char* text){if(!okay)throw std::runtime_error(text);}
bool bits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
bool bits(const std::vector<double>& a,const std::vector<double>& b){
    return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](double a,double b){return bits(a,b);});
}
bool repairs_equal(const state::RepairBudget& a,const state::RepairBudget& b){
    return bits(a.values,b.values)&&a.semantics==b.semantics&&a.block_uid==b.block_uid&&a.stage==b.stage
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

/** A real, injectable owner journal: preparation privately frames every patch, consumption
 * is exactly once, accept records only pending quadrature, and commit/discard is atomic. */
class JournalProbe final:public scheduler::HydroStagePreparation {
    struct Patch {int id;const FluidState* state;const Grid* grid;amr::BlockHandle handle;
        state::StateVersion version;const double* allocation;std::size_t extent;bool consumed=false;};
    driver::DriverRuntime& owner_;
    scheduler::StageExecutionContext& context_;
    mutable std::mutex mutex_;
    mutable bool prepared_=false;
    bool macro_=false;
    StageDescriptor descriptor_{};
    scheduler::HydroMethod method_=scheduler::HydroMethod::RK3;
    const state::StateResidencyLedger* ledger_=nullptr;
    const SimConfig* configuration_=nullptr;
    driver::DriverRuntime* runtime_=nullptr;
    double input_time_=0.,step_dt_=0.;
    std::vector<Patch> patches_;
    std::vector<double> accepted_boundary_before_;
public:
    enum class Fault {None,Descriptor,ForeignLedger,ForeignRuntime,ForeignConfiguration,
        ForeignEpoch,StaleVersion,ForeignAllocation,MissingConsumption,RepeatedConsumption,Begin};
    Fault fault=Fault::None;
    int descriptor_field=0;
    std::array<double,3> pending{},committed{};
    int pending_count=0,committed_count=0,commit_calls=0,discard_calls=0;
    mutable int invalidations=0;
    bool acceptance_seen_before_discard=false;
    bool observed_pending_boundary=false;
    std::function<void(int,int)> invalidation_observer;
    explicit JournalProbe(driver::DriverRuntime& owner,scheduler::StageExecutionContext& context)
        :owner_(owner),context_(context){}
    JournalProbe(const JournalProbe&)=delete;JournalProbe& operator=(const JournalProbe&)=delete;
    JournalProbe(JournalProbe&&)=delete;JournalProbe& operator=(JournalProbe&&)=delete;
    bool supports_host_macro_step_journal() const noexcept override{return true;}
    /** Begin allocates only private journal capacity before any field mutation. */
    void begin_macro_step() override {
        if(macro_)throw std::logic_error("JOURNAL_DUPLICATE_OWNER");
        patches_.reserve(owner_.handles().size());accepted_boundary_before_=owner_.hydro_boundary_budget();
        pending_count=0;pending.fill(0.);macro_=true;
        if(fault==Fault::Begin)throw std::logic_error("JOURNAL_BEGIN_FAULT");
    }
    /** Publish a complete prepared domain only after all real Runtime inputs are readable. */
    state::CompletionToken prepare(const scheduler::HydroStagePreparationRequest& request) override {
        std::lock_guard lock(mutex_);
        require(macro_&&!prepared_,"preparation lifecycle overlap");
        require(request.handles.size()==owner_.handles().size()&&request.side==state::ExecutionSide::Host,
            "prepare skipped actual Runtime domain/side");
        std::vector<Patch> candidate;candidate.reserve(request.handles.size());
        const auto& ids=owner_.control().tree->GetActiveBlocks();
        for(std::size_t i=0;i<ids.size();++i) {
            auto& b=owner_.control().pool->GetBlock(ids[i]);const auto live=slots(b);
            const auto* input=live[static_cast<int>(request.descriptor.input_slot)];
            const auto version=request.ledger.inspect({request.handles[i],request.descriptor.input_slot}).interior.version;
            request.ledger.require_readable({request.handles[i],request.descriptor.input_slot},
                {request.side,version,true,request.descriptor.input_requires_ghost});
            candidate.push_back({ids[i],input,&b.grid,request.handles[i],version,input->rho.data(),input->rho.size(),false});
        }
        descriptor_=request.descriptor;method_=request.method;ledger_=&request.ledger;
        configuration_=&owner_.configuration();runtime_=&owner_;input_time_=request.input_time;step_dt_=request.step_dt;
        // Owner faults are deliberately local metadata corruptions, never production policy changes.
        if(fault==Fault::ForeignLedger)ledger_=nullptr;
        if(fault==Fault::ForeignRuntime)runtime_=nullptr;
        if(fault==Fault::ForeignConfiguration)configuration_=nullptr;
        if(fault==Fault::ForeignEpoch)++candidate.back().handle.epoch.value;
        if(fault==Fault::StaleVersion)++candidate.back().version.value;
        if(fault==Fault::ForeignAllocation)candidate.back().allocation=nullptr;
        patches_.swap(candidate);prepared_=true;
        return {static_cast<std::uint64_t>(900+request.descriptor.stage),state::CompletionState::Complete};
    }
    /** The actual RK3 patch producer consumes its own framed physical input exactly once. */
    void consume(int id,const FluidState& input,const Grid& grid) const {
        std::lock_guard lock(mutex_);
        require(prepared_&&macro_,"source consumed without published private batch");
        auto* self=const_cast<JournalProbe*>(this);
        auto found=std::find_if(self->patches_.begin(),self->patches_.end(),[&](const auto& p){return p.id==id;});
        require(found!=self->patches_.end(),"source consumer omitted domain identity");
        if(fault==Fault::MissingConsumption&&found==self->patches_.end()-1)return;
        if(found->consumed)throw std::logic_error("JOURNAL_REPEATED_CONSUMPTION");
        if(ledger_!=&context_.ledger||runtime_!=&owner_||configuration_!=&owner_.configuration()
            ||found->state!=&input||found->grid!=&grid||found->allocation!=input.rho.data()
            ||found->extent!=input.rho.size()||found->handle!=owner_.handles()[found-self->patches_.begin()]
            ||found->version!=context_.ledger.inspect({found->handle,descriptor_.input_slot}).interior.version)
            throw std::logic_error("JOURNAL_FOREIGN_OR_STALE_FRAME");
        found->consumed=true;
    }
    /** Validate all descriptor fields and domain consumption before publishing a pending receipt. */
    void accept(const StageDescriptor& actual) override {
        std::lock_guard lock(mutex_);
        require(macro_&&prepared_&&pending_count<3,"accept outside complete prepared batch");
        auto expected=descriptor_;
        if(fault==Fault::Descriptor)switch(descriptor_field) {
            case 0:++expected.stage;break;
            case 1:expected.old_slot=StateSlot::Scratch;break;
            case 2:expected.input_slot=StateSlot::Next;break;
            case 3:expected.output_slot=StateSlot::Next;break;
            case 4:expected.old_weight+=.125;break;
            case 5:expected.update_weight+=.125;break;
            case 6:expected.flux_register_weight+=.125;break;
            case 7:expected.input_requires_ghost=!expected.input_requires_ghost;break;
            case 8:expected.refresh_ghost_after=!expected.refresh_ghost_after;break;
            case 9:expected.input_time_fraction+=.125;break;
        }
        if(!scheduler::same_stage_descriptor(actual,expected)||method_!=scheduler::HydroMethod::RK3
            ||step_dt_!=context_.step_dt
            ||input_time_!=context_.step_start_time+actual.input_time_fraction*context_.step_dt
            ||!std::all_of(patches_.begin(),patches_.end(),[](const auto& p){return p.consumed;}))
            throw std::logic_error("JOURNAL_DESCRIPTOR_OR_CONSUMPTION_MISMATCH");
        for(const auto& patch:patches_)context_.ledger.require_readable(
            {patch.handle,actual.input_slot},{state::ExecutionSide::Host,patch.version,true,actual.input_requires_ghost});
        pending[pending_count++]=actual.flux_register_weight;
        // Accepted Runtime boundary budget stays unchanged despite an earlier stage capture acceptance.
        require(bits(owner_.hydro_boundary_budget(),accepted_boundary_before_),
            "stage boundary prefix was visible as accepted history");
        if(actual.stage>1)observed_pending_boundary=true;
    }
    /** Invalidation retires borrowed batch pointers before rollback may restore old arrays. */
    void invalidate() const override {
        std::lock_guard lock(mutex_);
        if(invalidation_observer)invalidation_observer(descriptor_.stage,pending_count);
        prepared_=false;const_cast<JournalProbe*>(this)->patches_.clear();++invalidations;
    }
    /** Publication never allocates or exposes a partially accepted source prefix. */
    void commit_macro_step() noexcept override {
        if(!macro_||prepared_||pending_count!=3)std::terminate();
        for(std::size_t i=0;i<3;++i)committed[i]+=pending[i];
        committed_count+=pending_count;++commit_calls;pending_count=0;macro_=false;
    }
    /** Abort preserves earlier accepted history and drops only this macro-step's tentative rows. */
    void discard_macro_step() noexcept override {
        if(prepared_)std::terminate();
        acceptance_seen_before_discard=pending_count>0;
        pending_count=0;pending.fill(0.);patches_.clear();macro_=false;++discard_calls;
    }
    int stage() const {std::lock_guard lock(mutex_);return descriptor_.stage;}
};

/** Actual type-erased Hydro producer with bounded faults and explicit diagnostic-only impulses. */
class HydroProbe:public Numerics::IHydroSolver {
public:
    enum class Fault {None,LastBlock,Stage3NativeRejection,RepairSemantics};
    enum class DiagnosticReceipt {EventOnly,StageImpulses,None};
    DiagnosticReceipt diagnostic_receipt=DiagnosticReceipt::EventOnly;
    bool poison_output_ghosts=false;
    JournalProbe* journal=nullptr;
    Fault fault=Fault::None;
    int last_id=-1;
    mutable std::atomic<int> final_block_visits{0};
    mutable std::atomic<int> patch_visits{0};
    GridMetrics::GeometrySemantics geometry_semantics() const noexcept override{return rz;}
    Numerics::HostHydroStorageContract host_storage_contract() const noexcept override {
        return Numerics::HostHydroStorageContract::FixedExtentSlotPermutation;
    }
    void evaluate_patch(amr::AMRControl* c,int id,const FluidState& input,const Grid& g,double,
        std::vector<FluidVector>& increment,std::vector<double>& species,
        const Physical::Gravity::IGravityPolicy*,const NumericsConfig&,double weight,void*) const override {
        ++patch_visits;
        if(journal) {
            journal->consume(id,input,g);
            if(journal->fault==JournalProbe::Fault::RepeatedConsumption)journal->consume(id,input,g);
        }
        std::fill(increment.begin(),increment.end(),FluidVector{});std::fill(species.begin(),species.end(),0.);
        for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i)increment[g.GetIndex(i,j,0)].eng=.125;
        // Mutate the real register and shared observer: rollback must handle these owners, too.
        c->flux_register.AddCoarseFlux(id,0,0,{1.,2.,3.,4.,5.},weight);
        c->flux_register.AddCoarseSpeciesFlux(id,0,0,0,.25,weight);
        if(input.boundary_flux_capture)
            for(int face=0;face<6;++face) {
                auto& plane=input.boundary_flux_capture->stage[face];
                // Left/right fluxes deliberately differ, guaranteeing nonzero surface accounting.
                for(std::size_t k=0;k<plane.size();++k)plane[k]=weight*(1.+face+.01*k);
            }
        if(weight==2./3.) {
            ++final_block_visits;
            if(fault==Fault::LastBlock&&id==last_id)throw std::logic_error("HYDRO_LAST_STAGE_LAST_BLOCK_FAULT");
        }
    }
    void update_patch(const FluidState& old,const FluidState& input,FluidState& output,
        const std::vector<FluidVector>& increment,const std::vector<double>&,
        const Grid& g,double old_weight,double update_weight,const NumericsConfig&,void*) const override {
        for(int f=0;f<7;++f) {
            auto& destination=output.*fields[f];const auto& a=old.*fields[f];const auto& b=input.*fields[f];
            for(std::size_t i=0;i<destination.size();++i)destination[i]=old_weight*a[i]+update_weight*b[i];
        }
        for(std::size_t i=0;i<output.eng.size();++i)output.eng[i]+=update_weight*increment[i].eng;
        output.stage_repairs.reset(1,state::RepairSemantics::RzVolumeAngular);
        if(diagnostic_receipt!=DiagnosticReceipt::None)
            output.stage_repairs.view().event(1.,g.GetIndex(g.Is(),g.Js(),0));
        if(diagnostic_receipt==DiagnosticReceipt::StageImpulses) {
            // These diagnostic amounts never alter rho, energy or composition.
            // Distinguish stage 1/2/3 using the actual Euler/SSPRK recurrence.
            const double impulse=old_weight==0. ? 1. : old_weight==1./3. ? 4. : 2.;
            require(old_weight==0.||old_weight==.5||old_weight==.75||old_weight==1./3.,
                "diagnostic producer received an unexpected stage recurrence");
            output.stage_repairs.view().conserved(impulse,0.,0.,0.,0.);
        }
        if(fault==Fault::RepairSemantics)output.stage_repairs.semantics=state::RepairSemantics::ExistingVolume;
        if(poison_output_ghosts)
            for(int j=g.Js();j<g.Je();++j)for(int i:{g.Is()-1,g.Ie()}) {
                // One real radial ghost on each side is filled either by physical
                // BC or the neighboring Runtime block's actual exchange.
                const int cell=g.GetIndex(i,j,0);
                output.rho[cell]=std::numeric_limits<double>::quiet_NaN();
                output.eng[cell]=std::numeric_limits<double>::quiet_NaN();
            }
        // This invalid third-stage output must be rejected by the mandatory
        // native post-ghost gate before final slot rotation or reflux. It is
        // an explicit engineering counterexample, not a reflux-generated state.
        if(fault==Fault::Stage3NativeRejection&&old_weight==1./3.)
            output.rho[g.GetIndex(g.Is(),g.Js(),0)]=0.;
    }
};

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
SimConfig settings(dispatch::TimeIntegratorId method,bool split_profile=false){
    SimConfig c;c.grid.dim=2;c.grid.geometry="cylindrical";c.grid.nblockx1=2;c.grid.nblockx2=1;c.grid.nblockx3=0;
    c.grid.amr_max_blocks=8;c.grid.x1_min=1.;c.grid.x1_max=3.;c.grid.x2_min=-1.;c.grid.x2_max=1.;
    c.amr.lrefinemin=0;c.amr.lrefinemax=0;
    switch(method) {
        case dispatch::TimeIntegratorId::Euler:c.numerics.time_integrator="Euler";break;
        case dispatch::TimeIntegratorId::Rk2:c.numerics.time_integrator="RK2";break;
        case dispatch::TimeIntegratorId::Rk3:c.numerics.time_integrator="RK3";break;
        default:throw std::invalid_argument("unsupported fixture Hydro method");
    }
    c.numerics.sml_rho=1.e-20;c.numerics.min_eint=1.e-20;c.numerics.max_eint=1.e99;
    if(split_profile) {
        c.physics.burn.use_burn=true;c.physics.burn.nuclearTempMin=0.;
        c.physics.burn.nuclearDensMin=0.;c.physics.burn.smallt=1.e-20;
        c.physics.burn.use_nse=false;
        c.physics.diffusion.use_diffusion=true;
        c.physics.diffusion.use_viscous_diffusion=true;c.physics.diffusion.nu_visc=1.e-4;
    }
    return c;
}
/** Real Runtime fixture; no replacement ledger, scheduler or accepted-owner service. */
struct Fixture {
    SimConfig config;SpeciesManager species;
    std::unique_ptr<IdealGas> eos;
    amr::AMRControl control{8,2};RunState start;
    std::unique_ptr<SimulationController> controller;
    std::unique_ptr<boundary::ScopedUserBoundarySelection> selection;
    std::unique_ptr<BCHandler> bc;
    std::unique_ptr<driver::DriverRuntime> runtime;
    std::optional<scheduler::StageExecutionContext> context;
    std::unique_ptr<JournalProbe> journal;
    HydroProbe hydro;driver::DriverStageWorkspace workspace;
    dispatch::ResolvedExecutionPlan plan{};
    std::function<void(const scheduler::StageExecutionContext&, StateSlot,
        state::StateVersion)> boundary_witness;
    /** Keep the actual mandatory EOS gate while adding explicit test-only faults. */
    void refresh_boundary_gate() {
        runtime->bind_native_boundary_acceptance(*context, runtime->handles());
        if (boundary_witness) {
            const auto science = context->post_boundary_acceptance;
            const auto witness = boundary_witness;
            context->post_boundary_acceptance = [science, witness](
                const scheduler::StageExecutionContext& actual, StateSlot slot,
                state::StateVersion version) {
                science(actual, slot, version);
                witness(actual, slot, version);
            };
        }
    }
    explicit Fixture(dispatch::TimeIntegratorId method=dispatch::TimeIntegratorId::Rk3,
        bool bind_source_journal=true,bool split_profile=false):config(settings(method,split_profile)) {
        if(bind_source_journal&&method!=dispatch::TimeIntegratorId::Rk3)
            throw std::invalid_argument("fixture source journal is explicitly RK3-only");
        species.add_species("X",1.,1.,1.4,1.);
        eos=std::make_unique<IdealGas>(1.4,species);
        control.tree->InitRootGrid(config,1,rz);control.flux_register.EnsureSpecies(1);
        for(int id:control.tree->GetActiveBlocks()) {
            auto& block=control.pool->GetBlock(id);
            for(auto* state:slots(block)) {
                state->stage_repairs.reset(1,state::RepairSemantics::RzVolumeAngular);
                for(int cell=0;cell<block.grid.GetTotalSize();++cell) {
                    state->set(cell,split_profile?FluidVector{2.,0.,.3,0.,20.}:FluidVector{2.,.2,.3,.4,20.});
                    state->enuc_rate[cell]=.75;state->X(0,cell)=1.;
                }
            }
        }
        start.repairs.reset(1,state::RepairSemantics::RzVolumeAngular);
        controller=std::make_unique<SimulationController>(config,start);
        boundary::ResolvedUserBoundaries selected;
        // Nonempty registered selection exercises real Runtime accounting, with builtin physical ghosts.
        selected.gravity=[](const boundary::GravityBoundaryContext&){return boundary::GravityBoundaryData{};};
        selected.identity="owner-negative-fixture";
        selection=std::make_unique<boundary::ScopedUserBoundarySelection>(std::move(selected),config,species);
        bc=std::make_unique<BCHandler>(config,rz);
        bc->bind(*eos,species);
        runtime=std::make_unique<driver::DriverRuntime>(control,*bc,config,species,*controller);
        runtime->bind_native_rz_eos(*eos);
        runtime->initialize_topology();context.emplace(runtime->stage_context());
        context->step_start_time=2.;context->step_dt=.125;context->boundary_start_time=2.;context->boundary_step_dt=.125;
        if(bind_source_journal) {
            journal=std::make_unique<JournalProbe>(*runtime,*context);
            context->hydro_preparation=journal.get();hydro.journal=journal.get();
        }
        hydro.last_id=control.tree->GetActiveBlocks().back();
        // Use the existing Driver rule even with pure builtin ghosts, so stage time/revision is exercised.
        context->configure_boundary_context=[this](double time,boundary::BoundaryPurpose purpose){
            bc->configure_stage(time,purpose);refresh_boundary_gate();
        };
        context->physical_boundary_preparation=[this](StateSlot slot,double time,boundary::BoundaryPurpose purpose){
            bc->configure_stage(time,purpose);runtime->ensure_fluid_ghosts(slot);
            refresh_boundary_gate();
        };
        runtime->bind_boundary_accounting(*context);plan.time_integrator=method;
    }
    void advance(driver::IntegratorSolve integrator=selected_rk3,
        driver::HostHydroQualification qualification=driver::HostHydroQualification::NativeRzRollback){
        scheduler::ScopedStageBinding binding(*context,runtime->handles());
        driver::advance_hydro(*runtime,workspace,*context,&plan,context->step_dt,integrator,nullptr,&hydro,qualification);
    }
};
/** Require the actual native scheduler/gate frame before any selected solve writes. */
void native_gate_preflight_preserves_evidence(){
    struct Method {dispatch::TimeIntegratorId id;driver::IntegratorSolve solve;};
    const std::array<Method,3> methods{{
        {dispatch::TimeIntegratorId::Euler,selected_euler},
        {dispatch::TimeIntegratorId::Rk2,selected_rk2},
        {dispatch::TimeIntegratorId::Rk3,selected_rk3}}};
    const std::array<const char*,4> expected{{
        "production stage scheduler is not bound",
        "Native RZ Hydro requires post-boundary acceptance",
        "Native RZ Hydro requires a Host stage binding",
        "Native RZ Hydro stage domain extent mismatch"}};
    for(const auto& method:methods)for(int fault=0;fault<4;++fault) {
        Fixture f(method.id,false);
        const int id=f.control.tree->GetActiveBlocks().front();
        // Nonzero original fluid/species registers and receipts make Clear()
        // or reset observably destructive; there is no rollback guard here.
        f.control.flux_register.AddCoarseFlux(id,0,0,{1.,2.,3.,4.,5.},.75);
        f.control.flux_register.AddCoarseSpeciesFlux(id,0,0,0,.25,.75);
        for(int active:f.control.tree->GetActiveBlocks()) {
            auto& block=f.control.pool->GetBlock(active);
            for(auto* slot:slots(block))
                slot->stage_repairs.view().event(1.,block.grid.GetIndex(block.grid.Is(),block.grid.Js(),0));
        }
        // The real Runtime installs the gate for all cases initially. Only the
        // explicit absent-gate negative removes it; this does not substitute a
        // no-op callback or assert authentication from std::function presence.
        require(bool(f.context->post_boundary_acceptance),"real Runtime native gate is missing from fixture");
        if(fault==1)f.context->post_boundary_acceptance={};
        if(fault==2)f.context->side=state::ExecutionSide::Device;
        const auto fields_before=capture_fields(f.control);
        const auto owner_before=driver::HostHydroTransaction::snapshot_owner(*f.runtime,*f.context);
        bool caught=false;
        try {
            const auto invoke=[&]{method.solve(f.control,f.context->step_dt,*f.bc,nullptr,&f.hydro,f.config.numerics);};
            if(fault==0)invoke();
            else {
                std::span<const amr::BlockHandle> handles=f.runtime->handles();
                if(fault==3) {
                    require(handles.size()==2,"extent negative needs two actual Runtime patches");
                    handles=handles.first(1);
                }
                scheduler::ScopedStageBinding binding(*f.context,handles);
                invoke();
            }
        }catch(const std::logic_error& error) {
            caught=std::string(error.what()).find(expected[fault])!=std::string::npos;
            if(!caught)throw;
        }
        require(caught,"actual native selected solve did not reject its incomplete execution frame");
        for(const auto& field:fields_before)field.matches(f.control);
        require(driver::HostHydroTransaction::owner_matches(*f.runtime,*f.context,owner_before),
            "native gate preflight changed populated registers, Runtime owners or publication evidence");
        require(f.hydro.patch_visits==0&&f.hydro.final_block_visits==0,
            "native gate preflight reached an actual numerical patch producer");
        require(!f.journal&&!f.context->hydro_preparation,
            "native gate preflight negative acquired a source journal or transaction");
    }
}
/** Independent receipt algebra through real Runtime and all three selected Hydro lanes. */
void independent_rz_hydro_receipt_counts(){
    struct Case {
        dispatch::TimeIntegratorId method;
        driver::IntegratorSolve solve;
        double events,mass,volume;
    };
    // With two blocks, delta=[1,2,4] and actual SSP recurrences give these totals.
    // Event counts count executions; mass/volume carry their recurrence gains.
    const std::array<Case,3> cases{{
        {dispatch::TimeIntegratorId::Euler,selected_euler,2.,2.,2.},
        {dispatch::TimeIntegratorId::Rk2,selected_rk2,4.,5.,3.},
        {dispatch::TimeIntegratorId::Rk3,selected_rk3,6.,11.,11./3.}
    }};
    for(const auto& test:cases) {
        Fixture measured(test.method,false),control(test.method,false);
        require(!measured.context->hydro_preparation&&!measured.hydro.journal
            &&!control.context->hydro_preparation&&!control.hydro.journal,
            "three-method receipt test accidentally reused the RK3 source journal");
        require(measured.control.tree->GetActiveBlocks().size()==2,
            "independent receipt reference requires exactly two actual Runtime blocks");
        measured.hydro.diagnostic_receipt=HydroProbe::DiagnosticReceipt::StageImpulses;
        control.hydro.diagnostic_receipt=HydroProbe::DiagnosticReceipt::None;
        auto& history=measured.runtime->repair_budget();
        history.view().event(3.,0);history.view().conserved(-7.,11.,-13.,17.,-19.);
        history.view().species_mass(0,-7.);
        history.block_uid=measured.runtime->handles()[0].uid.value;
        history.stage=2;history.time=1.;history.position[0]=1.125;history.position[1]=-.75;
        const auto prior=history;
        measured.advance(test.solve);control.advance(test.solve);

        auto expected=prior;
        expected.values[0]+=test.events;expected.values[1]+=test.volume;
        expected.values[2]+=test.mass;expected.values[3]+=test.mass;
        require(repairs_equal(measured.runtime->repair_budget(),expected),
            "actual Hydro lane double-counted its final stage or erased accepted history");
        require(std::all_of(control.runtime->repair_budget().values.begin(),
            control.runtime->repair_budget().values.end(),[](double value){return value==0.;}),
            "diagnostic-free control acquired a physical repair");
        for(int id:measured.control.tree->GetActiveBlocks()) {
            const auto a=slots(measured.control.pool->GetBlock(id));
            const auto b=slots(control.control.pool->GetBlock(id));
            for(int slot=0;slot<3;++slot)
                for(const auto field:fields)
                    require(bits(a[slot]->*field,b[slot]->*field),
                        "diagnostic impulses or reflux receipt reset changed a physical field");
            require(repairs_equal(a[0]->stage_repairs,
                state::RepairBudget(1,state::RepairSemantics::RzVolumeAngular)),
                "final Current receipt did not start an independent RZ reflux epoch");
            TimeIntegration::validate_stage_state(*a[0],measured.control.pool->GetBlock(id).grid,
                measured.config.numerics);
        }
    }
}

/** Wrong measure/layout on a later block must not erase any earlier diagnostic row. */
void reflux_receipt_preflight_preserves_evidence(){
    for(int fault:{0,1,2}) {
        Fixture f(dispatch::TimeIntegratorId::Rk3,false);
        for(int id:f.control.tree->GetActiveBlocks())
            f.control.pool->GetBlock(id).fluid_state.stage_repairs.view().event(1.,0);
        auto& bad=f.control.pool->GetBlock(f.control.tree->GetActiveBlocks().back()).fluid_state.stage_repairs;
        if(fault==0)bad.semantics=state::RepairSemantics::ExistingVolume;
        if(fault==1)bad.semantics=static_cast<state::RepairSemantics>(255);
        if(fault==2)bad.values.pop_back();
        const auto before=capture_fields(f.control);
        const auto history=f.runtime->repair_budget();
        bool rejected=false;
        try{TimeIntegration::begin_rz_hydro_reflux_receipts(f.control);}
        catch(const std::invalid_argument& error) {
            rejected=std::string(error.what()).find("RZ Hydro reflux receipt measure/layout mismatch")!=std::string::npos;
            if(!rejected)throw;
        }
        require(rejected,"RZ reflux receipt preflight accepted a foreign/invalid measure or extent");
        for(const auto& field:before)field.matches(f.control);
        require(repairs_equal(f.runtime->repair_budget(),history),
            "receipt preflight changed previously accepted Runtime history");
    }
}

/** Require an actual injected rejection, then independent full owner/array comparison. */
template<class Action> void rejected_exact(Fixture& f,Action action,const char* expected){
    const auto fields_before=capture_fields(f.control);
    const auto owner_before=driver::HostHydroTransaction::snapshot_owner(*f.runtime,*f.context);
    const auto committed=f.journal->committed;const int committed_count=f.journal->committed_count;
    bool caught=false;
    try{action();}catch(const std::exception& error){
        caught=std::string(error.what()).find(expected)!=std::string::npos;
        if(!caught)throw;
    }
    require(caught,"expected concrete rejection was not reached");
    for(const auto& field:fields_before)field.matches(f.control);
    require(driver::HostHydroTransaction::owner_matches(*f.runtime,*f.context,owner_before),
        "Runtime BC/budget/clock/ledger/flux owner changed on rejection");
    require(f.journal->committed==committed&&f.journal->committed_count==committed_count
        &&f.journal->pending_count==0,"failed macro-step exposed an accepted source prefix");
}

/** A real completed BC/exchange is observed before rejection rolls back the entire RK macro-step. */
void post_boundary_rejection_rolls_back_complete_runtime(){
    for(int reject_stage:{1,3}) {
        Fixture f;f.advance();
        require(f.journal->committed_count==3,"post-boundary rollback requires nonempty accepted source history");
        f.hydro.poison_output_ghosts=true;
        const int first_id=f.control.tree->GetActiveBlocks().front();
        const auto& first_block=f.control.pool->GetBlock(first_id);
        const int interior=first_block.grid.GetIndex(first_block.grid.Is(),first_block.grid.Js(),0);
        const auto old_slots=slots(f.control.pool->GetBlock(first_id));
        const std::array<double,3> old_energy{old_slots[0]->eng[interior],old_slots[1]->eng[interior],
                                             old_slots[2]->eng[interior]};
        const int invalidations_before=f.journal->invalidations;
        int ghosts_per_gate=0;
        for(int id:f.control.tree->GetActiveBlocks()) {
            const auto& grid=f.control.pool->GetBlock(id).grid;
            ghosts_per_gate+=2*(grid.Je()-grid.Js());
        }
        int gate_calls=0,checked_ghosts=0;
        bool gate_rejected=false,source_invalidated_before_restore=false;
        f.boundary_witness=[&](const scheduler::StageExecutionContext& actual_context,
            StateSlot slot,state::StateVersion version) {
            require(&actual_context==&*f.context,"post-boundary witness lost actual calling context");
            ++gate_calls;
            require(gate_calls==f.journal->pending_count
                &&f.journal->invalidations>=invalidations_before+gate_calls,
                "post-boundary gate moved before source acceptance/invalidation");
            require(slot==(gate_calls==2?StateSlot::Next:StateSlot::Scratch),
                "post-boundary gate changed the frozen RK3 output route");
            for(std::size_t block=0;block<f.runtime->handles().size();++block) {
                const auto handle=f.runtime->handles()[block];
                const auto publication=f.context->ledger.inspect({handle,slot});
                require(publication.interior.version==version
                    &&publication.ghost.residency==state::StateResidency::Invalid,
                    "post-boundary gate observed a stale or already advertised ghost frame");
                auto& actual=f.control.pool->GetBlock(f.control.tree->GetActiveBlocks()[block]);
                const auto candidate=slots(actual)[static_cast<int>(slot)];
                for(int j=actual.grid.Js();j<actual.grid.Je();++j)
                    for(int i:{actual.grid.Is()-1,actual.grid.Ie()}) {
                        const int cell=actual.grid.GetIndex(i,j,0);
                        require(std::isfinite(candidate->rho[cell])&&std::isfinite(candidate->eng[cell]),
                            "post-boundary gate ran before real physical/interblock ghost fill");
                        ++checked_ghosts;
                    }
            }
            if(gate_calls==reject_stage) {
                gate_rejected=true;
                throw std::logic_error("POST_BOUNDARY_RUNTIME_GATE_REJECTED");
            }
        };
        f.journal->invalidation_observer=[&](int,int pending) {
            if(!gate_rejected||pending!=reject_stage)return;
            const auto candidate=slots(f.control.pool->GetBlock(first_id));
            for(int slot=0;slot<3;++slot)
                if(!bits(candidate[slot]->eng[interior],old_energy[slot]))
                    source_invalidated_before_restore=true;
        };
        rejected_exact(f,[&]{f.advance();},"POST_BOUNDARY_RUNTIME_GATE_REJECTED");
        require(gate_calls==reject_stage&&checked_ghosts==reject_stage*ghosts_per_gate
            &&source_invalidated_before_restore,
            "completed-boundary rejection did not restore the full macro-step after source invalidation");
        require(f.journal->committed_count==3&&f.journal->commit_calls==1
            &&f.journal->discard_calls==1&&bool(f.context->post_boundary_acceptance),
            "post-boundary rejection erased earlier history or dropped the original callback");
        // The new hook deliberately rejects before final rotation/Current mapping.
        // The mandatory real-EOS closure runs before this injected witness;
        // an ordering/rollback pass alone does not qualify RZ source dynamics.
    }
}

/** The optional gate is part of the frozen Runtime owner frame, including its original callable. */
void post_boundary_callback_presence_is_frozen(){
    for(bool present:{false,true}) {
        Fixture f;int original_calls=0;
        const auto science=f.context->post_boundary_acceptance;
        if(present)f.context->post_boundary_acceptance=[&,science](
            const scheduler::StageExecutionContext& actual,StateSlot slot,state::StateVersion version){
            science(actual,slot,version);++original_calls;
        };
        else {
            // Explicit malformed-context negative only; this absent mandatory
            // gate never reaches a scientific producer or successful advance.
            f.context->post_boundary_acceptance={};
        }
        rejected_exact(f,[&] {
            scheduler::ScopedStageBinding binding(*f.context,f.runtime->handles());
            driver::HostHydroTransaction transaction(*f.runtime,*f.context,f.hydro);
            if(present)f.context->post_boundary_acceptance={};
            else f.context->post_boundary_acceptance=science;
            transaction.validate_storage();
        },"Host Hydro transaction owner/frame changed");
        require(bool(f.context->post_boundary_acceptance)==present,
            "rollback failed to restore the new hook's original presence");
        if(present) {
            const auto version=f.context->ledger.inspect(
                {f.runtime->handles().front(),StateSlot::Current}).interior.version;
            f.context->post_boundary_acceptance(*f.context,StateSlot::Current,version);
            require(original_calls==1,"rollback restored presence but lost the original gate callable");
        }
        require(f.hydro.patch_visits==0,"callback-presence rejection reached a numerical producer");
    }
}

void first_use_and_retry(){
    Fixture failed;
    for(int id:failed.control.tree->GetActiveBlocks())for(auto* slot:slots(failed.control.pool->GetBlock(id)))
        require(!slot->boundary_flux_capture,"first-use observer was already allocated");
    failed.hydro.fault=HydroProbe::Fault::LastBlock;
    bool invalidated_before_restore=false;
    failed.journal->invalidation_observer=[&](int stage,int pending) {
        if(stage!=3||pending!=2)return;
        const auto& block=failed.control.pool->GetBlock(failed.control.tree->GetActiveBlocks()[0]);
        invalidated_before_restore=block.state_next.eng[block.grid.GetIndex(block.grid.Is(),block.grid.Js(),0)]!=20.;
    };
    rejected_exact(failed,[&]{failed.advance();},"HYDRO_LAST_STAGE_LAST_BLOCK_FAULT");
    require(invalidated_before_restore,"source invalidation did not precede allocation/value restoration");
    failed.journal->invalidation_observer={};
    require(failed.journal->acceptance_seen_before_discard&&failed.journal->discard_calls==1
        &&failed.hydro.final_block_visits==2,"late domain failure did not follow two real accepted stages");
    failed.hydro.fault=HydroProbe::Fault::None;failed.advance();
    require(failed.journal->commit_calls==1&&failed.journal->committed_count==3,"retry journal not committed once");
    require(failed.journal->committed==std::array<double,3>{1./6.,1./6.,2./3.},
        "source quadrature was replaced by repair recurrence weights");
    Fixture control;control.advance();
    const auto reference=capture_fields(control.control);
    for(const auto& field:reference)field.matches(failed.control,false);
    require(bits(failed.runtime->hydro_boundary_budget(),control.runtime->hydro_boundary_budget()),"retry boundary receipt differs");
    require(repairs_equal(failed.runtime->repair_budget(),control.runtime->repair_budget()),"retry repair receipt differs");
    require(failed.context->clock.last_token()==control.context->clock.last_token()
        &&failed.context->clock.last_version()==control.context->clock.last_version(),"retry publication clock differs");
    const auto ledger=control.context->ledger.snapshot_host();
    require(failed.context->ledger.host_snapshot_matches(ledger),"retry ledger differs from uninterrupted control");
    const auto flux=control.control.flux_register.snapshot_host();
    require(failed.control.flux_register.host_snapshot_matches(flux,false),
        "retry register metadata/payload differs from uninterrupted control");
    require(std::any_of(failed.runtime->hydro_boundary_budget().begin(),failed.runtime->hydro_boundary_budget().end(),
        [](double value){return value!=0.;}),"fixture failed to exercise actual nonzero boundary accounting");
}
void existing_alias_late_ghost_native_rejection(){
    for(int failure:{0,1,2}){
        Fixture f;f.advance();
        for(int id:f.control.tree->GetActiveBlocks()){
            const auto slot=slots(f.control.pool->GetBlock(id));
            require(slot[0]->boundary_flux_capture&&slot[0]->boundary_flux_capture==slot[1]->boundary_flux_capture
                &&slot[1]->boundary_flux_capture==slot[2]->boundary_flux_capture,"warm shared alias fixture missing");
        }
        if(failure==0){f.hydro.fault=HydroProbe::Fault::LastBlock;
            rejected_exact(f,[&]{f.advance();},"HYDRO_LAST_STAGE_LAST_BLOCK_FAULT");}
        if(failure==1) {
            // Engineering-only rejection after the real phased BC, AMR
            // exchange and mandatory actual-EOS gate. The original pending
            // source receipt and full Runtime rollback assertions still own it.
            // This witness does not replace or certify physical boundary data.
            int completed_boundary_calls=0;
            f.boundary_witness=[&](const scheduler::StageExecutionContext& actual,
                StateSlot slot,state::StateVersion version) {
                require(&actual==&*f.context&&slot==StateSlot::Scratch
                    &&f.journal->pending_count==1,
                    "late ghost fault did not reach its real first-stage pending receipt");
                for(const auto handle:f.runtime->handles()) {
                    const auto publication=actual.ledger.inspect({handle,slot});
                    require(publication.interior.version==version
                        &&publication.ghost.residency==state::StateResidency::Invalid,
                        "late ghost fault ran before interior acceptance or after ghost publication");
                }
                ++completed_boundary_calls;
                throw std::logic_error("HYDRO_GHOST_AFTER_RECEIPT_FAULT");
            };
            f.refresh_boundary_gate();
            rejected_exact(f,[&]{f.advance();},"HYDRO_GHOST_AFTER_RECEIPT_FAULT");
            require(completed_boundary_calls==1,
                "actual completed first-stage boundary fault was not observed exactly once");
            f.boundary_witness={};f.refresh_boundary_gate();
        }
        if(failure==2){f.hydro.fault=HydroProbe::Fault::Stage3NativeRejection;
            rejected_exact(f,[&]{f.advance();},"RZ native provisional state rejected at cell ");}
        require(f.journal->committed_count==3&&f.journal->commit_calls==1,"failure erased previously accepted source history");
        f.hydro.fault=HydroProbe::Fault::None;f.advance();
        require(f.journal->commit_calls==2&&f.journal->discard_calls==1,"warm retry lifecycle failed");
    }
}
void descriptor_and_frame_negatives(){
    for(int field=0;field<10;++field){Fixture f;f.journal->fault=JournalProbe::Fault::Descriptor;f.journal->descriptor_field=field;
        rejected_exact(f,[&]{f.advance();},"JOURNAL_DESCRIPTOR_OR_CONSUMPTION_MISMATCH");}
    for(auto fault:{JournalProbe::Fault::ForeignLedger,JournalProbe::Fault::ForeignRuntime,
        JournalProbe::Fault::ForeignConfiguration,JournalProbe::Fault::ForeignEpoch,
        JournalProbe::Fault::StaleVersion,JournalProbe::Fault::ForeignAllocation,
        JournalProbe::Fault::MissingConsumption,JournalProbe::Fault::RepeatedConsumption,JournalProbe::Fault::Begin}){
        Fixture f;f.journal->fault=fault;
        const char* expected=fault==JournalProbe::Fault::MissingConsumption?"JOURNAL_DESCRIPTOR_OR_CONSUMPTION_MISMATCH"
            :fault==JournalProbe::Fault::RepeatedConsumption?"JOURNAL_REPEATED_CONSUMPTION"
            :fault==JournalProbe::Fault::Begin?"JOURNAL_BEGIN_FAULT":"JOURNAL_FOREIGN_OR_STALE_FRAME";
        rejected_exact(f,[&]{f.advance();},expected);
    }
}
void exclusive_owner_and_partial_permutation(){
    Fixture f;
    const auto before=capture_fields(f.control);
    const auto owner_before=driver::HostHydroTransaction::snapshot_owner(*f.runtime,*f.context);
    {
        scheduler::ScopedStageBinding binding(*f.context,f.runtime->handles());
        driver::HostHydroTransaction transaction(*f.runtime,*f.context,f.hydro);
        const auto must_reject=[](auto action,const char* name){bool caught=false;try{action();}
            catch(const std::logic_error&){caught=true;}require(caught,name);};
        must_reject([&]{driver::HostHydroTransaction duplicate(*f.runtime,*f.context,f.hydro);},"duplicate owner accepted");
        must_reject([&]{f.runtime->regrid_native_rz_candidate(0,0.);},"regrid escaped active owner");
        must_reject([&]{f.runtime->initialize_topology();},"topology adoption escaped active owner");
        must_reject([&]{f.runtime->prepare_backend_bindings();},"backend storage issuance escaped active owner");
        must_reject([&]{f.runtime->bind_boundary_accounting(*f.context);},"boundary rebinding escaped active owner");
        must_reject([&]{f.context->ledger.retire_block(f.runtime->handles()[0]);},"UID retirement escaped active owner");
        must_reject([&]{f.control.flux_register.EnsureSpecies(2);},"species arena resize escaped active owner");
        must_reject([&]{f.control.flux_register.Resize(16,2);},"flux arena resize escaped active owner");
        must_reject([&]{f.context->ledger.begin_transfer({f.runtime->handles()[0],StateSlot::Current},
            state::StateRegion::Interior,state::PendingTransferPhase::PendingH2D,{999,state::CompletionState::Pending});},
            "Device transfer escaped active owner");
        auto& block=f.control.pool->GetBlock(f.control.tree->GetActiveBlocks()[0]);
        std::swap(block.fluid_state,block.state_scratch);block.fluid_state.eng[0]=123.;
        // Destructor rejects a partially completed physical rotation without backup allocations.
    }
    for(const auto& field:before)field.matches(f.control);
    require(driver::HostHydroTransaction::owner_matches(*f.runtime,*f.context,owner_before),"partial rotation rollback changed owner");
    f.advance();require(f.journal->commit_calls==1,"duplicate-owner test released another live owner token");
}

/** Exercise retired-record and invisible high-watermark restoration in the actual Runtime ledger. */
void retired_uid_and_replay_history(){
    Fixture f;
    const amr::BlockHandle retired{{1000},f.runtime->handles().front().epoch};
    const auto witness=f.context->clock.next_publication();
    // Deliberate owner-metadata fixture: this UID is retired before any prepared physical batch.
    f.context->ledger.register_block(retired,witness.version,witness.completion);
    f.context->ledger.retire_block(retired);
    f.hydro.fault=HydroProbe::Fault::LastBlock;
    rejected_exact(f,[&]{f.advance();},"HYDRO_LAST_STAGE_LAST_BLOCK_FAULT");
    bool rejected=false;
    try{f.context->ledger.register_block(retired,witness.version,witness.completion);}
    catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"rejected macro-step lost retired UID tombstone");
    // Correct clock AND per-slot token high-watermarks are needed for this real retry publication.
    f.hydro.fault=HydroProbe::Fault::None;f.advance();
    require(f.journal->committed_count==3,"post-rejection token history prevented legitimate retry");
}

void fail_closed_profiles(){
    Fixture f;const auto before=capture_fields(f.control);
    const auto require_failure=[&](auto action,const char* name){bool caught=false;try{action();}
        catch(const std::logic_error&){caught=true;}require(caught,name);for(const auto& field:before)field.matches(f.control);};
    f.context->side=state::ExecutionSide::Device;
    require_failure([&]{f.advance();},"Device transaction was not excluded");f.context->side=state::ExecutionSide::Host;
    struct Unqualified final:scheduler::HydroStagePreparation{
        state::CompletionToken prepare(const scheduler::HydroStagePreparationRequest&)override{
            throw std::logic_error("unqualified preparation should not execute");}
    } unqualified;
    f.context->hydro_preparation=&unqualified;
    require_failure([&]{f.advance();},"default no-op service was silently qualified");f.context->hydro_preparation=f.journal.get();
    int foreign_calls=0;f.context->hydro_acceptance=[&](const StageDescriptor&){++foreign_calls;};
    require_failure([&]{f.advance();},"existing acceptance owner overwritten");
    f.context->hydro_acceptance({});require(foreign_calls==1,"foreign owner binding lost");f.context->hydro_acceptance={};
    struct UnqualifiedHydro final:HydroProbe {
        Numerics::HostHydroStorageContract host_storage_contract() const noexcept override {
            return Numerics::HostHydroStorageContract::Unavailable;
        }
    } unsupported;
    require_failure([&]{scheduler::ScopedStageBinding binding(*f.context,f.runtime->handles());
        driver::HostHydroTransaction no_storage_contract(*f.runtime,*f.context,unsupported);},
        "unavailable fixed-extent Hydro trait accepted");
    Fixture foreign;
    require_failure([&]{scheduler::ScopedStageBinding binding(*foreign.context,foreign.runtime->handles());
        driver::HostHydroTransaction wrong(*f.runtime,*foreign.context,f.hydro);},"foreign Runtime context accepted");
    // Ordinary legacy lane is still callable with its default qualification and has no transaction backups.
    f.context->hydro_preparation=nullptr;f.hydro.journal=nullptr;
    f.advance(selected_rk3,driver::HostHydroQualification::NativeRzRollback);
    f.advance(selected_rk3,driver::HostHydroQualification::Production);
    require(f.journal->commit_calls==0,"production lane implicitly acquired an internal journal transaction");
}

/** Genuine residency transitions on a real Runtime prove initial Synchronized/Device fail closed. */
void forbidden_residency(){
    for(bool synchronized:{false,true}) {
        Fixture f;const auto before=capture_fields(f.control);
        const auto handle=f.runtime->handles()[0];
        if(synchronized) {
            const auto completion=f.context->clock.next_completion();
            f.context->ledger.begin_transfer({handle,StateSlot::Current},state::StateRegion::Interior,
                state::PendingTransferPhase::PendingH2D,{completion.value,state::CompletionState::Pending});
            f.context->ledger.complete_transfer({handle,StateSlot::Current},state::StateRegion::Interior,completion);
        } else {
            const auto witness=f.context->clock.next_publication();
            f.context->ledger.publish_interior({handle,StateSlot::Current},state::ExecutionSide::Device,
                witness.version,witness.completion);
        }
        const auto token=f.context->clock.last_token(),version=f.context->clock.last_version();
        bool caught=false;try{f.advance();}catch(const std::logic_error& error){
            caught=std::string(error.what()).find("excludes Device/Synchronized")!=std::string::npos;
            if(!caught)throw;
        }
        require(caught,"non-Host initial owner state silently qualified");
        for(const auto& field:before)field.matches(f.control);
        require(f.context->clock.last_token()==token&&f.context->clock.last_version()==version,
            "unsupported residency rejection issued a publication token");
        require(f.journal->commit_calls==0&&f.journal->discard_calls==0,
            "unsupported residency touched the source journal");
    }
}


/** Use another valid Runtime's actual binding: neither owner may receive a ghost/stage/publication write. */
void actual_foreign_binding_before_write(){
    Fixture a,b;
    const auto a_fields=capture_fields(a.control),b_fields=capture_fields(b.control);
    const auto a_owner=driver::HostHydroTransaction::snapshot_owner(*a.runtime,*a.context);
    const auto b_owner=driver::HostHydroTransaction::snapshot_owner(*b.runtime,*b.context);
    const auto copied_handles=a.runtime->handles();
    const auto rejects_binding=[&](scheduler::StageExecutionContext& bound_context,
        std::span<const amr::BlockHandle> borrowed_handles) {
        bool caught=false;
        {
            scheduler::ScopedStageBinding actual_binding(bound_context,borrowed_handles);
            try {
                driver::advance_hydro(*a.runtime,a.workspace,*a.context,&a.plan,a.context->step_dt,
                    selected_rk3,nullptr,&a.hydro,driver::HostHydroQualification::NativeRzRollback);
            } catch(const std::logic_error& error) {
                caught=std::string(error.what()).find("exact bound stage context and borrowed Runtime handles")!=std::string::npos;
                if(!caught)throw;
            }
        }
        require(caught,"foreign-valid actual thread-local binding was not rejected before writes");
        for(const auto& field:a_fields)field.matches(a.control);
        for(const auto& field:b_fields)field.matches(b.control);
        require(driver::HostHydroTransaction::owner_matches(*a.runtime,*a.context,a_owner),
            "caller Runtime owner/publications changed under foreign binding");
        require(driver::HostHydroTransaction::owner_matches(*b.runtime,*b.context,b_owner),
            "actual bound foreign Runtime owner/publications changed");
        require(a.hydro.patch_visits==0&&b.hydro.patch_visits==0
            &&a.journal->commit_calls==0&&b.journal->commit_calls==0
            &&a.journal->discard_calls==0&&b.journal->discard_calls==0,
            "foreign binding reached a producer or opened a journal");
    };
    // Both contexts and both complete handle spans are otherwise independently valid.
    rejects_binding(*b.context,b.runtime->handles());
    rejects_binding(*a.context,b.runtime->handles());
    rejects_binding(*a.context,copied_handles); // Equal contents, wrong original allocation.
    rejects_binding(*a.context,std::span<const amr::BlockHandle>(a.runtime->handles()).first(1));
}

/** A begun owner's real borrowed span cannot change data, size or content during the step. */
void changed_borrowed_binding_after_begin(){
    Fixture f;
    const auto fields_before=capture_fields(f.control);
    const auto owner_before=driver::HostHydroTransaction::snapshot_owner(*f.runtime,*f.context);
    auto copied_handles=f.runtime->handles();
    {
        scheduler::ScopedStageBinding scope(*f.context,f.runtime->handles());
        driver::HostHydroTransaction transaction(*f.runtime,*f.context,f.hydro);
        auto& actual=const_cast<scheduler::StageBinding&>(scheduler::current_stage_binding());
        const auto original=actual.handles;
        const auto must_reject=[&] {
            bool caught=false;
            try{transaction.validate_storage();}catch(const std::logic_error& error){
                caught=std::string(error.what()).find("actual stage binding changed")!=std::string::npos;
                if(!caught)throw;
            }
            require(caught,"changed borrowed binding escaped actual-owner revalidation");
        };
        actual.handles=copied_handles;must_reject();actual.handles=original;
        actual.handles=original.first(1);must_reject();actual.handles=original;
        auto& live_handles=const_cast<std::vector<amr::BlockHandle>&>(f.runtime->handles());
        const auto original_handle=live_handles.front();
        ++live_handles.front().uid.value;must_reject();live_handles.front()=original_handle;
        // Restore deliberate fixture metadata before leaving scope; it never publishes a topology.
        transaction.validate_storage();
    }
    for(const auto& field:fields_before)field.matches(f.control);
    require(driver::HostHydroTransaction::owner_matches(*f.runtime,*f.context,owner_before),
        "binding-tamper rejection changed accepted owner/publications");
    require(f.hydro.patch_visits==0,"binding tamper executed a numerical producer");
}

/** Actual advance_hydro dt cannot disagree even by one ULP with source/boundary frozen context dt. */
void mismatched_step_size_before_write(){
    Fixture f;
    const std::array<double,6> bad_steps{f.context->step_dt*.5,
        std::nextafter(f.context->step_dt,std::numeric_limits<double>::infinity()),
        0.,-f.context->step_dt,std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()};
    for(double dt:bad_steps)rejected_exact(f,[&] {
        scheduler::ScopedStageBinding actual_binding(*f.context,f.runtime->handles());
        driver::advance_hydro(*f.runtime,f.workspace,*f.context,&f.plan,dt,
            selected_rk3,nullptr,&f.hydro,driver::HostHydroQualification::NativeRzRollback);
    },"Hydro step size must be finite, positive and equal frozen context.step_dt");
    require(f.hydro.patch_visits==0&&f.journal->commit_calls==0&&f.journal->discard_calls==0,
        "bad independent step size reached a producer or opened a journal");
    f.advance();require(f.journal->commit_calls==1,"bad-dt rejection damaged a valid subsequent step");
}

/** Production source readiness must be retired before fallible repair collection, with no journal begin/accept. */
class ProductionReadinessProbe final:public scheduler::HydroStagePreparation {
    bool journal_capable_;
public:
    mutable bool ready=false;
    int preparations=0,accepts=0,begins=0;
    mutable int invalidations=0;
    explicit ProductionReadinessProbe(bool journal_capable):journal_capable_(journal_capable){}
    bool supports_host_macro_step_journal() const noexcept override{return journal_capable_;}
    void begin_macro_step() override{++begins;}
    state::CompletionToken prepare(const scheduler::HydroStagePreparationRequest& request) override {
        for(const auto handle:request.handles) {
            const auto version=request.ledger.inspect({handle,request.descriptor.input_slot}).interior.version;
            request.ledger.require_readable({handle,request.descriptor.input_slot},
                {request.side,version,true,request.descriptor.input_requires_ghost});
        }
        ++preparations;ready=true;return {900,state::CompletionState::Complete};
    }
    void accept(const StageDescriptor&) override{++accepts;throw std::logic_error("PRODUCTION_ACCEPT_WITHOUT_BEGIN");}
    void invalidate() const override{ready=false;++invalidations;}
};
void production_invalidation_before_repair_failure(){
    for(bool journal_capable:{false,true}) {
        Fixture f;ProductionReadinessProbe preparation(journal_capable);
        f.context->hydro_preparation=&preparation;f.hydro.journal=nullptr;
        f.hydro.fault=HydroProbe::Fault::RepairSemantics;
        const auto accepted_version=f.context->ledger.inspect({f.runtime->handles()[0],StateSlot::Current}).interior.version;
        bool reached_repair_failure=false;
        try{f.advance(selected_rk3,driver::HostHydroQualification::Production);}
        catch(const std::invalid_argument& error){
            reached_repair_failure=std::string(error.what()).find("Cannot merge different repair measure identities")!=std::string::npos;
            if(!reached_repair_failure)throw;
        }
        require(reached_repair_failure&&f.hydro.patch_visits==2,
            "actual Production RK stage did not reach injected repair-semantic rejection");
        require(preparation.preparations==1&&!preparation.ready&&preparation.invalidations==1
            &&preparation.accepts==0&&preparation.begins==0,
            "Production changed original invalidation order or accepted a journal without begin");
        require(f.context->ledger.inspect({f.runtime->handles()[0],StateSlot::Current}).interior.version==accepted_version,
            "rejected Production repair collection published an accepted interior");
        // Production has no rollback guard: no claim is made that scratch/clock owners rolled back.
    }
}

/** Stateless injected heat-release policy through the ACTUAL Host Burn batch.
 * This prescribed de/dt=1/4 is an engineering input, not a reaction network.
 * The existing packed-temperature/first-law handoff, native closure, workers
 * and two Burn half calls remain real; no alternate state update is used.
 */
struct MacroHeatProbe {
    static constexpr int NEQ=3; // One species, temperature, passive energy.
    bool integrate(double* packed,double rho,double interval,const IdealGas& eos,
        const BurnConfig&,double& recommended,double* energy) const {
        const double old_energy=eos.get_eint_from_T(rho,packed[1],packed);
        const double release=.25*interval;
        packed[1]=eos.get_temperature(rho,old_energy+release,packed);
        packed[2]=release;recommended=.5;
        if(energy)*energy=release;
        return true;
    }
};

/** Same five-segment production helper, actual Burn/RKL/Runtime EOS and BC.
 * Hydro/source and additional surface impulses are explicitly diagnostic
 * injections. Nonzero old history, tentative RKL budgets and late failures
 * exercise transaction ownership without claiming source/tensor accuracy.
 */
void whole_macro_endpoint_and_rollback(){
    enum class Fault {None,BurnFirst,DiffusionFirst,HydroLast,DiffusionSecond,BurnSecond,EndpointEos};
    const std::array<Fault,7> faults{{Fault::None,Fault::BurnFirst,Fault::DiffusionFirst,
        Fault::HydroLast,Fault::DiffusionSecond,Fault::BurnSecond,Fault::EndpointEos}};
    for(const auto order:{dispatch::DiffusionIntegratorId::Rkl1,dispatch::DiffusionIntegratorId::Rkl2})
        for(const auto fault:faults) {
            Fixture f(dispatch::TimeIntegratorId::Rk3,true,true);
            f.plan.diffusion_integrator=order;
            f.advance(); // Genuine accepted Hydro history and aliased captures before this attempt.
            f.controller->dt_old=.375;f.controller->step_count=1;f.controller->t_current=2.;
            double burn_advice=.75;
            const auto fields_before=capture_fields(f.control);
            const auto owner_before=driver::HostHydroTransaction::snapshot_owner(*f.runtime,*f.context);
            const auto repairs_before=f.runtime->repair_budget();
            const auto hydro_before=f.runtime->hydro_boundary_budget();
            const auto diffusion_before=f.runtime->diffusion_boundary_budget();
            const auto source_before=f.journal->committed;
            const int source_count=f.journal->committed_count;
            const auto old_dt=f.controller->dt_old,old_burn=burn_advice;
            const int first_id=f.control.tree->GetActiveBlocks().front();
            auto& first=f.control.pool->GetBlock(first_id);
            const int first_cell=first.grid.GetIndex(first.grid.Is(),first.grid.Js(),0);
            const double old_energy=first.fluid_state.eng[first_cell];
            std::optional<std::pair<double,boundary::BoundaryPurpose>> configured;
            const auto actual_configure=f.context->configure_boundary_context;
            f.context->configure_boundary_context=[&](double time,boundary::BoundaryPurpose purpose) {
                actual_configure(time,purpose);configured=std::pair{time,purpose};
            };
            auto actual_rkl_accept=f.context->rkl_flux_capture_accept;
            f.context->rkl_flux_capture_accept=[&](const scheduler::RklStageDescriptor& d,
                const scheduler::RklPlan& plan) {
                // Diagnostic-only nonzero surface impulses make publication
                // of BOTH diffusion budgets observable even for this actual
                // constant axial-translation zero-viscous-operator profile.
                if(d.stage==static_cast<int>(plan.stages.size()))
                    for(int id:f.control.tree->GetActiveBlocks()) {
                        auto capture=f.control.pool->GetBlock(id).fluid_state.boundary_flux_capture;
                        require(bool(capture),"actual RKL did not install the Runtime capture owner");
                        for(int face=0;face<6;++face)
                            for(double& value:capture->stage[face])value+=.001*(1.+face);
                    }
                actual_rkl_accept(d,plan);
                require(bits(f.runtime->diffusion_boundary_budget(),diffusion_before),
                    "RKL half published accepted diffusion budget before the macro endpoint");
            };
            const auto burn=BurnerHandle<IdealGas>::bind<MacroHeatProbe>();
            int burn_calls=0,diffusion_calls=0;
            double completed_burn_advice=0.;
            std::vector<driver::CpuStage> measured;
            bool injected=false;
            auto reject=[&](const char* message){injected=true;throw std::logic_error(message);};
            auto attempt=[&] {
                driver::NativeMacroStepAdvice advice(*f.runtime,*f.controller,burn_advice);
                (void)f.controller->calculate_next_dt(f.context->step_dt,burn_advice);
                burn_advice=1.e99;
                scheduler::ScopedStageBinding binding(*f.context,f.runtime->handles());
                driver::execute_driver_macro_step(*f.runtime,*f.context,&f.hydro,true,
                    [&](driver::BurnHalf half,double interval,state::CompletionToken token) {
                        ++burn_calls;
                        require(f.runtime->active_host_hydro_transaction()!=nullptr,
                            "burn did not borrow the complete macro owner");
                        require(f.journal->committed_count==source_count,
                            "Hydro committed its borrowed macro owner before Burn2");
                        auto result=driver::execute_burn_half(*f.runtime,f.workspace,*f.eos,burn,
                            half,interval,burn_advice,token);
                        if((fault==Fault::BurnFirst&&half==driver::BurnHalf::First)
                            ||(fault==Fault::BurnSecond&&half==driver::BurnHalf::Second))
                            reject("MACRO_ACTUAL_BURN_POST_WRITE_REJECTED");
                        if(fault==Fault::EndpointEos&&half==driver::BurnHalf::Second) {
                            // The lane publishes this finite, positive-rho
                            // candidate. Only the final ACTUAL Runtime EOS
                            // rejects its unresolved negative thermal energy.
                            first.fluid_state.eng[first_cell]=-1.;injected=true;
                        }
                        require(repairs_equal(f.runtime->repair_budget(),repairs_before),
                            "burn observed prematurely accepted macro repair receipts");
                        return result;
                    },
                    [&](double interval) {
                        ++diffusion_calls;
                        require(configured&&bits(configured->first,f.context->boundary_start_time)
                            &&configured->second==boundary::BoundaryPurpose::Diffusion,
                            "macro diffusion input did not refresh its actual Runtime EOS/BC snapshot");
                        const auto rkl_order=order==dispatch::DiffusionIntegratorId::Rkl1
                            ?DiffFunction::RKLOrder::First:DiffFunction::RKLOrder::Second;
                        require(DiffFunction::compute_stages(rkl_order,interval,.01,
                            f.config.physics.diffusion.diff_cfl,f.config.physics.diffusion.max_stages)>=2,
                            "macro RKL profile omitted real recurrence stages");
                        driver::advance_diffusion(*f.runtime,f.workspace,*f.context,*f.eos,&f.plan,
                            1,interval,.01);
                        require(repairs_equal(f.runtime->repair_budget(),repairs_before),
                            "diffusion half exposed accepted macro repair history");
                        if((fault==Fault::DiffusionFirst&&diffusion_calls==1)
                            ||(fault==Fault::DiffusionSecond&&diffusion_calls==2))
                            reject("MACRO_ACTUAL_RKL_POST_WRITE_REJECTED");
                    },
                    [&](double interval) {
                        if(fault==Fault::HydroLast)f.hydro.fault=HydroProbe::Fault::LastBlock;
                        driver::advance_hydro(*f.runtime,f.workspace,*f.context,&f.plan,interval,
                            selected_rk3,nullptr,&f.hydro);
                        require(f.journal->committed_count==source_count
                            &&repairs_equal(f.runtime->repair_budget(),repairs_before)
                            &&bits(f.runtime->hydro_boundary_budget(),hydro_before),
                            "borrowed Hydro published source/repair/surface prefix before final EOS");
                    },
                    [&](driver::CpuStage stage,auto&& execute) {measured.push_back(stage);execute();});
                completed_burn_advice=burn_advice;advice.commit();
            };
            bool rejected=false;
            try{attempt();}
            catch(const std::exception& error) {
                const std::string message=error.what();
                if(fault==Fault::HydroLast)
                    rejected=message.find("HYDRO_LAST_STAGE_LAST_BLOCK_FAULT")!=std::string::npos;
                else if(fault==Fault::EndpointEos)
                    rejected=message.find("RZ native provisional state rejected")!=std::string::npos
                        ||message.find("RZ native closure/EOS rejected")!=std::string::npos;
                else rejected=injected&&message.find("MACRO_ACTUAL_")!=std::string::npos;
                if(!rejected)throw; // No earlier unrelated failure can satisfy this matrix.
            }
            require(bits(f.controller->t_current,2.)&&f.controller->step_count==1,
                "macro helper advanced accepted physical time/count before the caller's commit");
            if(fault!=Fault::None) {
                require(rejected,"macro failure injection did not reach its actual late owner");
                for(const auto& before:fields_before)before.matches(f.control);
                require(driver::HostHydroTransaction::owner_matches(*f.runtime,*f.context,owner_before),
                    "full macro rejection lost fields/addresses/BC/ledger/register/receipts");
                require(bits(f.controller->dt_old,old_dt)&&bits(burn_advice,old_burn),
                    "failed macro did not restore exact accepted timestep advice");
                require(f.journal->committed==source_before&&f.journal->committed_count==source_count
                    &&f.journal->pending_count==0,"late macro failure published source history");
                if(fault==Fault::BurnSecond||fault==Fault::EndpointEos)
                    require(burn_calls==2&&diffusion_calls==2,
                        "late failure did not execute both actual Burn and diffusion halves");
            } else {
                const std::vector<driver::CpuStage> sequence{driver::CpuStage::BurnFirst,
                    driver::CpuStage::Diffusion,driver::CpuStage::Hydro,
                    driver::CpuStage::Diffusion,driver::CpuStage::BurnSecond};
                require(!rejected&&measured==sequence&&burn_calls==2&&diffusion_calls==2,
                    "accepted macro changed the original split or timing intervals");
                require(f.journal->committed_count==source_count+3&&f.journal->pending_count==0
                    &&!f.runtime->active_host_hydro_transaction(),"macro endpoint did not commit/release its one owner");
                require(!bits(f.runtime->diffusion_boundary_budget(),diffusion_before)
                    &&!bits(f.runtime->hydro_boundary_budget(),hydro_before),
                    "macro commit omitted a tentative boundary receipt owner");
                // E gain = injected Hydro .125 + rho * (dt/4). This is an
                // independent engineering first-law count; original rounding
                // budget is unchanged, and no nuclear/tensor claim is made.
                require(std::abs(first.fluid_state.eng[first_cell]-old_energy-.1875)<=2.e-12,
                    "macro endpoint omitted or doubled a real Burn half");
                require(bits(f.controller->dt_old,.125)&&bits(burn_advice,completed_burn_advice)
                    &&std::isfinite(burn_advice)&&burn_advice>0.&&burn_advice<1.e99,
                    "successful macro did not retain actual accepted timestep advice");
                for(const auto handle:f.runtime->handles()) {
                    const auto coherence=f.context->ledger.inspect({handle,StateSlot::Current});
                    f.context->ledger.require_readable({handle,StateSlot::Current},
                        {state::ExecutionSide::Host,coherence.interior.version,true,true});
                    scheduler::detail::require_settled_destination(coherence);
                }
            }
        }
}

/** The original timestep function writes dt_old before it rejects a bad proposal. */
void rejected_timestep_advice_is_not_accepted(){
    Fixture f;double burn_advice=.75;f.controller->dt_old=.375;
    bool rejected=false;
    try {
        driver::NativeMacroStepAdvice guard(*f.runtime,*f.controller,burn_advice);
        (void)f.controller->calculate_next_dt(-1.,burn_advice);
    }catch(const std::runtime_error& error) {
        rejected=std::string(error.what()).find("dt too small")!=std::string::npos;
        if(!rejected)throw;
    }
    require(rejected&&bits(f.controller->dt_old,.375)&&bits(burn_advice,.75),
        "rejected timestep proposal changed accepted scalar advice");
}

} // namespace

/** Added to the existing gravity_stage_contract executable; no new broad CI campaign. */
void test_host_hydro_transaction(){
    first_use_and_retry();existing_alias_late_ghost_native_rejection();descriptor_and_frame_negatives();
    exclusive_owner_and_partial_permutation();retired_uid_and_replay_history();
    fail_closed_profiles();forbidden_residency();actual_foreign_binding_before_write();
    changed_borrowed_binding_after_begin();mismatched_step_size_before_write();
    production_invalidation_before_repair_failure();
    independent_rz_hydro_receipt_counts();reflux_receipt_preflight_preserves_evidence();
    post_boundary_rejection_rolls_back_complete_runtime();post_boundary_callback_presence_is_frozen();
    native_gate_preflight_preserves_evidence();
    whole_macro_endpoint_and_rollback();rejected_timestep_advice_is_not_accepted();
}
