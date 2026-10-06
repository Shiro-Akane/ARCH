/**
 * @file GravityStage.cpp
 * @brief Lease the correct density generation, solve domain gravity and publish fields.
 *
 * Workflow:
 * 1. Receive a resolved configuration, stage request and current state identity.
 * 2. Lease the correct density generation, solve domain gravity and publish fields.
 * 3. Hand completed state and diagnostics to the next scheduled stage.
 */

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <type_traits>

#include "driver/stages/GravityStage.h"

#include "amr/AMRControl.h"
#include "amr/elliptic/EllipticMeshAdapter.h"
#include "driver/runtime/DriverRuntime.h"
#include "numerics/multigrid/CompositeMultigrid.h"
#include "physics/gravity/GravityExecution.h"
#include "physics/gravity/GravitySolveTypes.h"
#include "physics/gravity/self/SelfGravity.h"

namespace arch::driver {
/** Open diagnostics for a configured self-gravity stage. */
GravityStage::GravityStage(DriverRuntime& runtime,const Physical::Gravity::IGravityPolicy* policy,
    Qualification qualification)
    :qualification_(qualification),runtime_(runtime),gravity_(dynamic_cast<const Physical::Gravity::SelfGravity*>(policy)) {
    if(qualification_!=Qualification::Production&&qualification_!=Qualification::NativeRzCandidate)
        throw std::invalid_argument("Unknown gravity stage qualification");
    if(qualification_==Qualification::NativeRzCandidate
        &&(!gravity_||runtime_.backend()
            ||runtime_.geometry_semantics()!=GridMetrics::GeometrySemantics::AxisymmetricRz))
        throw std::invalid_argument("Native RZ stage verification requires a CPU RZ Runtime");
    if (!gravity_) return;
    const auto& config=runtime.configuration();
    std::filesystem::create_directories(config.io.out_dir);
    diagnostics_.open(config.io.out_dir+(qualification_==Qualification::NativeRzCandidate
        ?"/native_rz_candidates.tsv":"/gravity_solves.tsv"));
    if (!diagnostics_) throw std::runtime_error("Cannot open gravity solve diagnostics");
    if(qualification_==Qualification::NativeRzCandidate) {
        diagnostics_<<"time\tstage\tepoch\tlease\tcells\tsource_generation\tresidual_upper\ttolerance_safe\tphysical_qualified\n"
            <<std::setprecision(17);return;
    }
    if(config.physics.gravity.boundary=="user") {
        boundary_diagnostics_.open(config.io.out_dir+"/gravity_boundary_exchange.tsv");
        if(!boundary_diagnostics_) throw std::runtime_error("Cannot open gravity boundary diagnostics");
        boundary_diagnostics_ << "# scope=since-process-start; fields=successive-publications; "
            "energy=half-integral-rho-Phi; exchange=Green-boundary-term; gauge=solver-policy; "
            "time-may-follow-RK-stage-order; units=CGS-with-GridMetrics-measure\n"
            "time\tprevious_time\tstage\tpotential_energy\tdelta_potential_energy\tboundary_exchange\tcumulative_exchange\tfaces\tobserver_seconds\tkernels\tbytes_h2d\tbytes_d2h\tsynchronizations\n"
            <<std::setprecision(17);
    }
    diagnostics_<<"time\tstage\tepoch\tgeneration\tcells\titerations\trhs_rms\tresidual\ttarget\trho_mean\tdevice\tsetup_seconds\tsolve_seconds\tkernels\tbytes_h2d\tbytes_d2h\tsynchronizations\tsource_boundary_seconds\tpoisson_seconds\tforce_seconds\n"<<std::setprecision(17);
    // Publish the schema once, including runs rejected before their first step.
    diagnostics_.flush();
    if(boundary_diagnostics_.is_open())boundary_diagnostics_.flush();
    if(!diagnostics_||(boundary_diagnostics_.is_open()&&!boundary_diagnostics_))
        throw std::runtime_error("Cannot publish gravity diagnostic schema");
}
/** Lease the exact RK input density generation and publish its solved field. */
state::CompletionToken GravityStage::solve(state::StateSlot slot,const state::StateResidencyLedger& ledger,double time,int stage) {
    const auto start=std::chrono::steady_clock::now();
    invalidate();
    auto* backend=runtime_.backend();
    if(qualification_==Qualification::NativeRzCandidate&&backend)
        throw std::logic_error("Native RZ stage verification cannot execute on Device");
    if(backend)gravity_->set_execution(backend->gravity_execution());
    const auto& handles=runtime_.handles(); const auto& config=runtime_.configuration();
    if (handles.empty()) throw std::logic_error("Gravity requires active topology");
    if (epoch_!=handles.front().epoch) {
        auto binding=amr::bind_elliptic_mesh(runtime_.control(),config.grid,handles);
        if(qualification_==Qualification::NativeRzCandidate)
            gravity_->bind_native_rz_candidate(std::move(binding),65536,100000);
        else gravity_->bind(std::move(binding),time);
        epoch_=handles.front().epoch;
    }
    // A new borrowed storage lease is issued for every solve, even if slots or
    // pool addresses are reused. No publication can survive an expired lease.
    if (++generation_==0) throw std::overflow_error("Gravity density lease exhausted");
    Physical::Gravity::GravitySolveIdentity identity;
    identity.topology=epoch_; identity.input_time=time; identity.gravitational_constant=arch::constants::gravity::cgs::gravitational_constant;
    identity.operator_revision=1;identity.boundary_revision=1;identity.accuracy_revision=1;
    std::vector<Physical::Gravity::GravityDensityView> views;
    const auto& active=runtime_.control().tree->GetActiveBlocks();
    for (std::size_t b=0;b<handles.size();++b) {
        const auto version=ledger.inspect({handles[b],slot}).interior.version;
        ledger.require_readable({handles[b],slot},{backend?state::ExecutionSide::Device:state::ExecutionSide::Host,version,true,false});
        const auto& block=runtime_.control().pool->GetBlock(active[b]);
        const auto& fluid=slot==state::StateSlot::Current?block.fluid_state:slot==state::StateSlot::Next?block.state_next:block.state_scratch;
        const auto& grid=block.grid;
        Physical::Gravity::GravityInputIdentity input{handles[b],slot,version,generation_};
        identity.inputs.push_back(input);
        const auto layout=amr::native_scalar_layout(grid);
        views.push_back({input,{backend?backend->gravity_density(runtime_.backend_access(b,slot)):fluid.rho.data(),
            fluid.rho.size(),layout,backend?arch::grid::FieldMemory::Device:arch::grid::FieldMemory::Host,generation_}});
    }
    const auto prepared=std::chrono::steady_clock::now();
    auto execution=backend?backend->gravity_execution():nullptr;
    const auto before=execution?execution->numeric()->counters():arch::multigrid::ExecutionCounters{};
    const auto token=gravity_->prepare({identity,views});
    if(qualification_==Qualification::NativeRzCandidate) {
        // Use the same Runtime lease and full original request. No physical
        // report/patch/CFL/output consumer is promoted by this diagnostic path.
        const auto& candidate=gravity_->native_rz_assessment();
        if(candidate.source!=identity
            ||candidate.conditional.status!=arch::elliptic::BoundaryResidualStatus::Accepted
            ||candidate.physical_status!=arch::elliptic::BoundaryResidualStatus::UncertifiedInput)
            throw std::logic_error("Native RZ stage candidate identity/qualification mismatch");
        diagnostics_<<time<<'\t'<<stage<<'\t'<<epoch_.value<<'\t'<<generation_<<'\t'
            <<gravity_->cell_count()<<'\t'<<candidate.source_generation<<'\t'
            <<candidate.conditional.total_residual_upper<<'\t'
            <<candidate.conditional.tolerance_safe<<"\t0\n";
        diagnostics_.flush();
        if(!diagnostics_)throw std::runtime_error("Cannot write native RZ candidate diagnostics");
        return token;
    }
    const auto& report=gravity_->report();
    // Transaction rows stay private until the complete split macro-step accepts.
    std::ostringstream solve_row,boundary_row;
    solve_row<<std::setprecision(17);boundary_row<<std::setprecision(17);
    std::ostream& solve_output=journal_active_?static_cast<std::ostream&>(solve_row):diagnostics_;
    const auto finished=std::chrono::steady_clock::now();
    const auto after=execution?execution->numeric()->counters():arch::multigrid::ExecutionCounters{};
    solve_output<<time<<'\t'<<stage<<'\t'<<epoch_.value<<'\t'<<generation_<<'\t'<<gravity_->cell_count()<<'\t'
        <<report.cycles<<'\t'<<report.rhs_rms<<'\t'<<report.residual<<'\t'<<report.target<<'\t'<<gravity_->density_mean()<<'\t'
        <<(backend?1:0)<<'\t'<<std::chrono::duration<double>(prepared-start).count()<<'\t'
        <<std::chrono::duration<double>(finished-prepared).count()<<'\t'<<after.kernels-before.kernels<<'\t'
        <<after.bytes_h2d-before.bytes_h2d<<'\t'<<after.bytes_d2h-before.bytes_d2h<<'\t'
        <<after.synchronizations-before.synchronizations<<'\t'<<gravity_->timings().source_boundary<<'\t'
        <<gravity_->timings().poisson<<'\t'<<gravity_->timings().force<<'\n';
    if (!solve_output) throw std::runtime_error("Cannot write gravity diagnostics");
    if(boundary_diagnostics_.is_open()) {
        auto next=gravity_->boundary_snapshot();
        const auto observed=std::chrono::steady_clock::now();
        const auto observer_counters=execution?execution->numeric()->counters():arch::multigrid::ExecutionCounters{};
        auto& previous=journal_active_?pending_boundary_snapshot_:boundary_snapshot_;
        auto& cumulative=journal_active_?pending_boundary_exchange_:boundary_exchange_;
        std::ostream& boundary_output=journal_active_?static_cast<std::ostream&>(boundary_row):boundary_diagnostics_;
        const double exchange=previous ? Physical::Gravity::gravity_boundary_exchange(*previous,next) : 0.;
        const double change=previous ? next.potential_energy-previous->potential_energy : 0.;
        cumulative+=exchange;
        boundary_output << time << '\t' << (previous?previous->time:time)
            << '\t' << stage << '\t' << next.potential_energy << '\t' << change << '\t' << exchange
            << '\t' << cumulative << '\t' << next.faces.size()
            << '\t' << std::chrono::duration<double>(observed-finished).count()
            << '\t' << observer_counters.kernels-after.kernels
            << '\t' << observer_counters.bytes_h2d-after.bytes_h2d
            << '\t' << observer_counters.bytes_d2h-after.bytes_d2h
            << '\t' << observer_counters.synchronizations-after.synchronizations << '\n';
        if(!boundary_output) throw std::runtime_error("Cannot write gravity boundary diagnostics");
        previous=std::move(next);
    }
    if(journal_active_) {
        if(!prepared_||pending_count_>=pending_rows_.size())
            throw std::logic_error("Gravity journal has no bounded prepared row");
        prepared_->source=identity;
        pending_rows_[pending_count_]={solve_row.str(),boundary_row.str()};
    }
    if(backend)for(std::size_t b=0;b<handles.size();++b)backend->publish_gravity(runtime_.backend_access(b,slot),gravity_->patch_view(b));
    return token;
}
/** Prepare gravity for the requested hydro stage input. */
state::CompletionToken GravityStage::prepare(const scheduler::HydroStagePreparationRequest& request) {
    if (!gravity_) throw std::logic_error("No self-gravity stage service");
    if(journal_active_) {
        if(prepared_||request.side!=state::ExecutionSide::Host||runtime_.backend()
            ||&request.ledger!=&runtime_.stage_context().ledger
            ||request.handles.size()!=runtime_.handles().size()
            ||!std::equal(request.handles.begin(),request.handles.end(),runtime_.handles().begin()))
            throw std::logic_error("Gravity journal request is outside its actual Runtime frame");
        (void)scheduler::hydro_output_time_fraction(request.method,request.descriptor);
        scheduler::detail::require_boundary_interval(request.input_time,request.step_dt);
        if(request.descriptor.stage!=static_cast<int>(pending_count_+1)
            ||(journal_method_&&(*journal_method_!=request.method
                ||journal_dt_!=request.step_dt||request.input_time!=journal_start_
                    +request.descriptor.input_time_fraction*journal_dt_)))
            throw std::logic_error("Gravity journal stage sequence/time changed inside a macro-step");
        if(!journal_method_) {
            journal_method_=request.method;journal_start_=request.input_time;journal_dt_=request.step_dt;
            expected_count_=scheduler::supported_hydro_time_plan(request.method).stages.size();
        }
        prepared_.emplace(PreparedFrame{request.method,request.descriptor,{},request.input_time,request.step_dt});
        gravity_->begin_host_stage_consumption(request.step_dt);
    }
    return solve(request.descriptor.input_slot,request.ledger,request.input_time,request.descriptor.stage);
}
/** Prepare gravity on the accepted current state for output and timestep use. */
void GravityStage::prepare_current(double time, bool reset_solver_history) {
    if(journal_active_||committed_count_)
        throw std::logic_error("Current gravity/output preparation requires a closed, flushed macro-step");
    if (gravity_) {
        // A checkpoint stores accepted fluid fields but no iterative Poisson
        // history. The Driver resets only at durable restart boundaries so
        // direct and resumed paths begin from the same accepted state.
        if (reset_solver_history) gravity_->clear_solver_initial_guess();
        auto context=runtime_.stage_context();
        solve(state::StateSlot::Current,context.ledger,time,0);
    }
}
/** A real Host production service can journal without granting any new physical scope. */
bool GravityStage::supports_host_macro_step_journal() const noexcept {
    return gravity_&&!runtime_.backend()&&qualification_==Qualification::Production;
}
/** Copy accepted observer state before acquiring a fluid transaction. */
void GravityStage::begin_macro_step() {
    if(!supports_host_macro_step_journal()||journal_active_||committed_count_)
        throw std::logic_error("Gravity macro-step journal is unavailable or already live/unflushed");
    auto next=boundary_snapshot_; // All fallible allocation precedes owner mutation.
    pending_boundary_snapshot_=std::move(next);
    pending_boundary_exchange_=boundary_exchange_;pending_count_=0;expected_count_=0;
    journal_method_.reset();prepared_.reset();journal_active_=true;
}
/** Validate the exact source lease, all Runtime inputs and actual force/work consumption. */
void GravityStage::accept(const scheduler::StageDescriptor& descriptor) {
    if(!journal_active_||!prepared_
        ||!scheduler::same_stage_descriptor(descriptor,prepared_->descriptor)
        ||descriptor.stage!=static_cast<int>(pending_count_+1))
        throw std::logic_error("Gravity journal acceptance does not match its prepared descriptor");
    const auto& source=prepared_->source;
    if(source.topology!=epoch_||source.input_time!=prepared_->input_time
        ||source.inputs.size()!=runtime_.handles().size())
        throw std::logic_error("Gravity prepared field identity/time changed before acceptance");
    const auto context=runtime_.stage_context();
    for(std::size_t b=0;b<source.inputs.size();++b) {
        const auto& input=source.inputs[b];
        if(input.block!=runtime_.handles()[b]||input.slot!=descriptor.input_slot
            ||input.storage_generation!=generation_)
            throw std::logic_error("Gravity source patch/slot/storage frame changed before acceptance");
        context.ledger.require_readable({input.block,input.slot},
            {state::ExecutionSide::Host,input.version,true,false});
    }
    gravity_->require_host_stage_consumption(source);
    gravity_->end_host_stage_consumption();
    ++pending_count_;prepared_.reset();invalidate();
}
/** Move only bounded accepted observer records; physical publication was checked before this tail. */
void GravityStage::commit_macro_step() noexcept {
    // The transaction calls this only after all fallible endpoint checks. A
    // programmer contract error cannot silently publish an incomplete prefix.
    if(!journal_active_||prepared_||!journal_method_
        ||!expected_count_||pending_count_!=expected_count_
        ||committed_count_)std::terminate();
    static_assert(std::is_nothrow_swappable_v<decltype(boundary_snapshot_)>);
    committed_rows_.swap(pending_rows_);committed_count_=pending_count_;
    boundary_snapshot_.swap(pending_boundary_snapshot_);
    boundary_exchange_=pending_boundary_exchange_;
    pending_count_=0;journal_method_.reset();journal_active_=false;
}
/** A failed attempt keeps accepted boundary history and emits no diagnostic prefix. */
void GravityStage::discard_macro_step() noexcept {
    if(!journal_active_)return;
    if(gravity_)gravity_->end_host_stage_consumption();
    prepared_.reset();journal_method_.reset();pending_boundary_snapshot_.reset();
    pending_count_=0;journal_active_=false;invalidate();
}
/** Report already accepted records; durable I/O is outside numerical commit/rollback. */
void GravityStage::flush_committed_diagnostics() {
    if(journal_active_)throw std::logic_error("Cannot flush a tentative gravity macro-step");
    if(!committed_count_)return; // Ordinary runs retain their existing buffered reporting.
    for(std::size_t i=0;i<committed_count_;++i) {
        diagnostics_<<committed_rows_[i].solve;
        if(boundary_diagnostics_.is_open())boundary_diagnostics_<<committed_rows_[i].boundary;
    }
    diagnostics_.flush();
    if(boundary_diagnostics_.is_open())boundary_diagnostics_.flush();
    if(committed_count_&&(!diagnostics_||(boundary_diagnostics_.is_open()&&!boundary_diagnostics_)))
        throw std::runtime_error("Cannot publish accepted gravity macro-step diagnostics");
    committed_count_=0;
}
/** Retire both host and device gravity views before changing state. */
void GravityStage::invalidate() const { if(gravity_)gravity_->invalidate();if(runtime_.backend())runtime_.backend()->invalidate_gravity(); }
/** Report the gravity stability cap to the Driver scheduler. */
double GravityStage::timestep() const { return gravity_?gravity_->timestep(runtime_.configuration().numerics.cfl):std::numeric_limits<double>::infinity(); }
/** Materialize accepted potential and acceleration for plot output. */
std::vector<io::PlotScalarField> GravityStage::plot_fields() const {
    if (!gravity_) return {};
    std::vector<io::PlotScalarField> fields{{"GPOT",gravity_->potential()}};
    const char* names[]{"GACX","GACY","GACZ"};
    for(int a=0;a<runtime_.configuration().grid.dim;++a) fields.push_back({names[a],gravity_->acceleration()[a]});
    return fields;
}
}
