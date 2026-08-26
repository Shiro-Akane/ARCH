/**
 * @file Dispatch_Euler.cpp
 * @brief Dispatcher component for the Forward Euler time integrator.
 *
 * Workflow:
 * 1. Acts as a standalone translation unit specifically for the Euler scheme.
 * 2. Resolves EOS, Gravity, then erases the BurnerPolicy via BurnerHandle<EosPolicy>.
 * 3. Only Euler instantiations are in this TU; RK2/RK3 are in separate TUs.
 *
 * BurnerHandle erases the burner policy before flux dispatch. This prevents the
 * ODE/network/linear-solver matrix from multiplying every flux instantiation.
 */

#include "DispatchImpl.h"

// Integrator isolated in this translation unit.
#include "../../numerics/integrator/TimeIntegratorEuler.h"  // Isolate Euler instantiations in this unit.

// Runtime physics dispatch.
#include "../../numerics/burnsolver/BurnDispatch.h" // Provides make_handle().
#include "../../numerics/burnsolver/BurnerHandle.h"
#include "../../physics/eos/eosdispatch.h"
#include "../../physics/gravity/GravityDispatch.h"

void Dispatch_Euler(
    amr::AMRControl &amr_ctrl, const SimConfig &config,
    const SpeciesManager &specs, const RunState &run_state,
    const arch::dispatch::ResolvedExecutionPlan& plan,
    const arch::dispatch::ExecutionRequirements& requirements,
    const arch::dispatch::BackendResolution& backend,
    arch::dispatch::StartupOrder& startup_order)
{
    EOSDispatcher::dispatch_eos(plan.eos, config, specs, [&](auto &&eos) {
        // Erase the burner policy after EOS resolution. Flux dispatch then sees
        // one BurnerHandle<EosPolicy> type instead of every ODE/network/linear-
        // solver combination, which controls template-instantiation memory.
        using EosType = std::remove_cvref_t<decltype(eos)>;
        auto burn_handle = BurnDispatcher::make_handle<EosType>(config, plan);

        auto grav_handle = Physical::Gravity::make_gravity(
            config, requirements.gravity);
        std::cout << "[Dispatch] Strategy: Euler/RK1 + "
                  << config.numerics.solver_name << " + "
                  << config.numerics.reconstruction
                  << " (" << config.numerics.limiter << ")" << std::endl;

        DispatchImpl::launch_resolved_run<SolverEuler>(
            amr_ctrl, eos, grav_handle.get(), burn_handle, config, specs,
            run_state, plan, requirements, backend, startup_order);
    });
}
