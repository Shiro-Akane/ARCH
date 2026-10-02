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
GravityStage::GravityStage(DriverRuntime& runtime,const Physical::Gravity::IGravityPolicy* policy)
    :runtime_(runtime),gravity_(dynamic_cast<const Physical::Gravity::SelfGravity*>(policy)) {
    if (!gravity_) return;
    const auto& config=runtime.configuration();
    std::filesystem::create_directories(config.io.out_dir);
    diagnostics_.open(config.io.out_dir+"/gravity_solves.tsv");
    if (!diagnostics_) throw std::runtime_error("Cannot open gravity solve diagnostics");
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
}
/** Lease the exact RK input density generation and publish its solved field. */
state::CompletionToken GravityStage::solve(state::StateSlot slot,const state::StateResidencyLedger& ledger,double time,int stage) {
    const auto start=std::chrono::steady_clock::now();
    invalidate();
    auto* backend=runtime_.backend();
    if(backend)gravity_->set_execution(backend->gravity_execution());
    const auto& handles=runtime_.handles(); const auto& config=runtime_.configuration();
    if (handles.empty()) throw std::logic_error("Gravity requires active topology");
    if (epoch_!=handles.front().epoch) {
        gravity_->bind(amr::bind_elliptic_mesh(runtime_.control(),config.grid,handles),time);
        epoch_=handles.front().epoch;
    }
    // A new borrowed storage lease is issued for every solve, even if slots or
    // pool addresses are reused. No publication can survive an expired lease.
    if (++generation_==0) throw std::overflow_error("Gravity density lease exhausted");
    Physical::Gravity::GravitySolveIdentity identity;
    identity.topology=epoch_; identity.input_time=time; identity.gravitational_constant=config.physics.gravity.G_const;
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
    const auto token=gravity_->prepare({identity,views}); const auto& report=gravity_->report();
    const auto finished=std::chrono::steady_clock::now();
    const auto after=execution?execution->numeric()->counters():arch::multigrid::ExecutionCounters{};
    diagnostics_<<time<<'\t'<<stage<<'\t'<<epoch_.value<<'\t'<<generation_<<'\t'<<gravity_->cell_count()<<'\t'
        <<report.cycles<<'\t'<<report.rhs_rms<<'\t'<<report.residual<<'\t'<<report.target<<'\t'<<gravity_->density_mean()<<'\t'
        <<(backend?1:0)<<'\t'<<std::chrono::duration<double>(prepared-start).count()<<'\t'
        <<std::chrono::duration<double>(finished-prepared).count()<<'\t'<<after.kernels-before.kernels<<'\t'
        <<after.bytes_h2d-before.bytes_h2d<<'\t'<<after.bytes_d2h-before.bytes_d2h<<'\t'
        <<after.synchronizations-before.synchronizations<<'\t'<<gravity_->timings().source_boundary<<'\t'
        <<gravity_->timings().poisson<<'\t'<<gravity_->timings().force<<'\n';
    if (!diagnostics_) throw std::runtime_error("Cannot write gravity diagnostics");
    if(boundary_diagnostics_.is_open()) {
        auto next=gravity_->boundary_snapshot();
        const auto observed=std::chrono::steady_clock::now();
        const auto observer_counters=execution?execution->numeric()->counters():arch::multigrid::ExecutionCounters{};
        const double exchange=boundary_snapshot_ ? Physical::Gravity::gravity_boundary_exchange(*boundary_snapshot_,next) : 0.;
        const double change=boundary_snapshot_ ? next.potential_energy-boundary_snapshot_->potential_energy : 0.;
        boundary_exchange_+=exchange;
        boundary_diagnostics_ << time << '\t' << (boundary_snapshot_?boundary_snapshot_->time:time)
            << '\t' << stage << '\t' << next.potential_energy << '\t' << change << '\t' << exchange
            << '\t' << boundary_exchange_ << '\t' << next.faces.size()
            << '\t' << std::chrono::duration<double>(observed-finished).count()
            << '\t' << observer_counters.kernels-after.kernels
            << '\t' << observer_counters.bytes_h2d-after.bytes_h2d
            << '\t' << observer_counters.bytes_d2h-after.bytes_d2h
            << '\t' << observer_counters.synchronizations-after.synchronizations << '\n';
        if(!boundary_diagnostics_) throw std::runtime_error("Cannot write gravity boundary diagnostics");
        boundary_snapshot_=std::move(next);
    }
    if(backend)for(std::size_t b=0;b<handles.size();++b)backend->publish_gravity(runtime_.backend_access(b,slot),gravity_->patch_view(b));
    return token;
}
/** Prepare gravity for the requested hydro stage input. */
state::CompletionToken GravityStage::prepare(const scheduler::HydroStagePreparationRequest& request) {
    if (!gravity_) throw std::logic_error("No self-gravity stage service");
    return solve(request.descriptor.input_slot,request.ledger,request.input_time,request.descriptor.stage);
}
/** Prepare gravity on the accepted current state for output and timestep use. */
void GravityStage::prepare_current(double time, bool reset_solver_history) {
    if (gravity_) {
        // A checkpoint stores accepted fluid fields but no iterative Poisson
        // history. The Driver resets only at durable restart boundaries so
        // direct and resumed paths begin from the same accepted state.
        if (reset_solver_history) gravity_->clear_solver_initial_guess();
        auto context=runtime_.stage_context();
        solve(state::StateSlot::Current,context.ledger,time,0);
    }
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
