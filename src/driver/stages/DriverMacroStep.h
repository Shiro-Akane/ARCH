/**
 * @file DriverMacroStep.h
 * @brief Execute the existing symmetric split with one native Host endpoint owner.
 *
 * Workflow:
 * 1. Borrow the actual Runtime StageBinding and the already-bound diagnostics.
 * 2. For Host native RZ only, snapshot the existing complete transaction owner
 *    before any B/2-D/2-H-D/2-B/2 field or receipt write.
 * 3. Execute the same two half-steps and full Hydro step through their original
 *    callables and CPU measurement intervals; no time tableau is introduced.
 * 4. Complete real Current ghosts/EOS at Hydro(t_n+dt), check exact ledger and
 *    BC identity, then publish both boundary budgets and repair/source receipts.
 * 5. On rejection, the one owner restores fields/leases/metadata. The separately
 *    scoped advice guard restores accepted timestep advice, not completed I/O.
 *
 * Existing charts and Device execution retain their original sequence without
 * transaction snapshots or an additional endpoint boundary evaluation.
 */
#pragma once

#include <cmath>
#include <optional>
#include <stdexcept>

#include "driver/io/DriverIO.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/runtime/HostHydroTransaction.h"
#include "driver/schedule/DriverControl.h"
#include "driver/stages/DriverStages.h"

namespace arch::driver {

/** Save accepted dt growth/burn advice before a native timestep proposal.
 * The guard owns only two scalars. Old-state output indices and consumed CPU
 * time remain with their actual owners; no retry or cache reset is performed.
 */
class NativeMacroStepAdvice final {
    SimulationController& controller_;
    double& burn_advice_;
    bool active_;
    double old_dt_,old_burn_;
public:
    NativeMacroStepAdvice(const DriverRuntime& runtime,SimulationController& controller,
        double& burn_advice) noexcept
        : controller_(controller),burn_advice_(burn_advice),
          active_(!runtime.backend()
              &&runtime.geometry_semantics()==GridMetrics::GeometrySemantics::AxisymmetricRz),
          old_dt_(active_?controller.dt_old:0.),old_burn_(active_?burn_advice:0.) {}
    NativeMacroStepAdvice(const NativeMacroStepAdvice&)=delete;
    NativeMacroStepAdvice& operator=(const NativeMacroStepAdvice&)=delete;
    /** Retain the proposal only after the full native endpoint was accepted. */
    void commit() noexcept {active_=false;}
    /** Restore exact accepted scalar values when proposal or split work throws. */
    ~NativeMacroStepAdvice() noexcept {
        if(active_) {controller_.dt_old=old_dt_;burn_advice_=old_burn_;}
    }
};

/** Execute the unique Driver five-segment sequence.
 * burn(half,dt/2,token), diffusion(dt/2), hydro(dt) call the existing owners.
 * measure(CpuStage,callable) preserves the existing nonoverlapping CPU timers.
 * Injected callables in engineering tests exercise rollback only; they do not
 * replace a scientific qualification of the actual Burn/Diffusion/source math.
 */
template<class BurnExecutor,class DiffusionExecutor,class HydroExecutor,class Measure>
void execute_driver_macro_step(DriverRuntime& runtime,StageExecutionContext& context,
    const Numerics::IHydroSolver* hydro,bool has_burn,BurnExecutor&& burn,
    DiffusionExecutor&& diffusion,HydroExecutor&& advance_hydro,Measure&& measure)
{
    const bool native_host=!runtime.backend()
        &&runtime.geometry_semantics()==GridMetrics::GeometrySemantics::AxisymmetricRz;
    std::optional<HostHydroTransaction> transaction;
    if(native_host) {
        // Presence is a necessary preflight, not scientific authority. The
        // subsequent Runtime boundary work authenticates its actual EOS/BC
        // and domain, including failures after the second burn publication.
        if(!hydro||!context.post_boundary_acceptance||!context.configure_boundary_context
            ||context.hydro_acceptance||context.rkl_acceptance
            ||!std::isfinite(context.step_start_time+context.step_dt))
            throw std::logic_error("Native macro endpoint requires its genuine boundary/stage owners");
        transaction.emplace(runtime,context,*hydro);
    }
    const double start=context.step_start_time,dt=context.step_dt,half_dt=.5*dt;
    auto& boundaries=runtime.boundaries();
    // Original symmetric split: B(dt/2), D(dt/2), H(dt), D(dt/2), B(dt/2).
    if(has_burn)measure(CpuStage::BurnFirst,[&] {
        (void)scheduler::execute_burn_first_lane(context,runtime.handles(),
            [&](state::CompletionToken token) {return burn(BurnHalf::First,half_dt,token);});
    });
    measure(CpuStage::Diffusion,[&] {
        context.boundary_start_time=start;context.boundary_step_dt=half_dt;
        // Native RKL completes Current boundaries before its first recurrence
        // stage. Refresh the genuine Runtime EOS snapshot at this half's
        // input time before that initial completion, not only inside stages.
        if(native_host)context.configure_boundary_context(start,boundary::BoundaryPurpose::Diffusion);
        else boundaries.configure_stage(start,boundary::BoundaryPurpose::Diffusion);
        diffusion(half_dt);
    });
    measure(CpuStage::Hydro,[&] {advance_hydro(dt);});
    measure(CpuStage::Diffusion,[&] {
        context.boundary_start_time=start+half_dt;context.boundary_step_dt=half_dt;
        if(native_host)context.configure_boundary_context(start+half_dt,boundary::BoundaryPurpose::Diffusion);
        else boundaries.configure_stage(start+half_dt,boundary::BoundaryPurpose::Diffusion);
        diffusion(half_dt);
    });
    if(has_burn)measure(CpuStage::BurnSecond,[&] {
        // Burn has no spatial boundary channel. Its existing fixed-density
        // closure borrows Hydro ghosts at the original macro endpoint.
        context.configure_boundary_context(start+dt,boundary::BoundaryPurpose::Hydro);
        (void)scheduler::execute_burn_second_lane(context,runtime.handles(),
            [&](state::CompletionToken token) {return burn(BurnHalf::Second,half_dt,token);});
    });
    if(transaction) {
        // Pure configure first, then ACTUAL whole-domain BC/exchange/EOS. A
        // ghost-valid Hydro intermediate cannot accept later D/B updates.
        context.configure_boundary_context(start+dt,boundary::BoundaryPurpose::Hydro);
        const auto boundary_frame=boundaries.snapshot_stage_context();
        runtime.ensure_fluid_ghosts(state::StateSlot::Current);
        if(!boundaries.stage_context_matches(boundary_frame))
            throw std::logic_error("Native macro endpoint boundary frame changed");
        for(const auto handle:runtime.handles()) {
            const state::StateKey key{handle,state::StateSlot::Current};
            const auto coherence=context.ledger.inspect(key);
            scheduler::detail::require_settled_destination(coherence);
            context.ledger.require_readable(key,
                {state::ExecutionSide::Host,coherence.interior.version,true,true});
        }
        transaction->validate_storage();
        transaction->commit();
    }
}
} // namespace arch::driver
