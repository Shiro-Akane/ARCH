/**
 * @file DriverMacroStep.h
 * @brief Execute the existing symmetric split with one actual endpoint owner.
 *
 * Workflow:
 * 1. Borrow the actual Runtime StageBinding and the already-bound diagnostics.
 * 2. Preflight the actual side/source before any B/2-D/2-H-D/2-B/2 field or
 *    receipt write. Actual source journals retain the backend's own frame
 *    authentication.
 * 3. Execute the same two half-steps and full Hydro step through their original
 *    callables and CPU measurement intervals; no time tableau is introduced.
 * 4. Complete real Current ghosts/EOS at Hydro(t_n+dt), check exact ledger and
 *    BC identity, then publish both boundary budgets and repair/source receipts.
 * 5. On rejection the original Runtime/boundary owners unwind the failed
 *    attempt; no completed I/O is reused by a later attempt.
 */
#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "driver/io/DriverIO.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "driver/stages/DriverStages.h"

namespace arch::driver {

/** Propose, align and execute the original complete macro once.
 * The original calculate_next_dt/sync_dt semantics are unchanged: one proposal,
 * one aligned step, then the actual split callables.
 */
template<class Attempt>
double execute_driver_macro_attempts(DriverRuntime& runtime,SimulationController& controller,
    double& burn_advice,double accepted_dt_cap,Attempt&& execute)
{
    const double proposal=controller.calculate_next_dt(accepted_dt_cap,burn_advice);
    burn_advice=1e99;
    const double actual_dt=controller.sync_dt(proposal);
    std::forward<Attempt>(execute)(actual_dt,1u);
    return actual_dt;
}

/** Execute the unique Driver five-segment sequence.
 * burn(half,dt/2,token), diffusion(dt/2), hydro(dt) call the existing owners.
 * measure(CpuStage,callable) preserves the existing nonoverlapping CPU timers.
 * The final Current check uses the actual field side and source journal.
 * Injected callables in engineering tests exercise rollback only; they do not
 * replace a scientific qualification of the actual Burn/Diffusion/source math.
 */
template<class BurnExecutor,class DiffusionExecutor,class HydroExecutor,class Measure>
void execute_driver_macro_step(DriverRuntime& runtime,StageExecutionContext& context,
    const Numerics::IHydroSolver* hydro,bool has_burn,BurnExecutor&& burn,
    DiffusionExecutor&& diffusion,HydroExecutor&& advance_hydro,Measure&& measure)
{
    const bool has_diff=runtime.configuration().physics.diffusion.use_diffusion;
    (void)hydro;
    std::array<std::optional<backend::DiffusionActivityReceipt>,2> activity_halves;
    // Scalars are owned by this original macro attempt; any exception/retry
    // destroys them. This guard clears only the unconsumed diagnostic scratch.
    struct ClearActivityHalf {
        DriverRuntime& runtime;
        ~ClearActivityHalf() { runtime.clear_diffusion_activity_half(); }
    } clear_activity_half{runtime};
    runtime.clear_diffusion_activity_half();
    const double start=context.step_start_time,dt=context.step_dt,half_dt=.5*dt;
    auto& boundaries=runtime.boundaries();
    // Original symmetric split: B(dt/2), D(dt/2), H(dt), D(dt/2), B(dt/2).
    if(has_burn)measure(CpuStage::BurnFirst,[&] {
        (void)scheduler::execute_burn_first_lane(context,runtime.handles(),
            [&](state::CompletionToken token) {return burn(BurnHalf::First,half_dt,token);});
    });
    if(has_diff)measure(CpuStage::Diffusion,[&] {
        context.boundary_start_time=start;context.boundary_step_dt=half_dt;
        boundaries.configure_stage(start,boundary::BoundaryPurpose::Diffusion);
        diffusion(half_dt);
        activity_halves[0]=runtime.take_diffusion_activity_half();
    });
    measure(CpuStage::Hydro,[&] {advance_hydro(dt);});
    if(has_diff)measure(CpuStage::Diffusion,[&] {
        context.boundary_start_time=start+half_dt;context.boundary_step_dt=half_dt;
        boundaries.configure_stage(start+half_dt,boundary::BoundaryPurpose::Diffusion);
        diffusion(half_dt);
        activity_halves[1]=runtime.take_diffusion_activity_half();
    });
    if(has_burn)measure(CpuStage::BurnSecond,[&] {
        // Burn has no spatial boundary channel. Its existing fixed-density
        // closure borrows Hydro ghosts at the original macro endpoint.
        context.configure_boundary_context(start+dt,boundary::BoundaryPurpose::Hydro);
        (void)scheduler::execute_burn_second_lane(context,runtime.handles(),
            [&](state::CompletionToken token) {return burn(BurnHalf::Second,half_dt,token);});
    });
    // Finish potentially-throwing scalar checks before publication.
    const auto activity=runtime.prepare_diffusion_activity_promotion(activity_halves);
    runtime.promote_diffusion_activity(activity);
}
} // namespace arch::driver
