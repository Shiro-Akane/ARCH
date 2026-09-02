/**
 * @file Dispatch_RK2.cpp
 * @brief Dispatcher component for the Runge-Kutta 2 (Heun's method) time integrator.
 *
 * Workflow:
 * 1. Acts as a standalone translation unit specifically for the RK2 scheme.
 * 2. Resolves EOS, Gravity, then erases the BurnerPolicy via BurnerHandle<EosPolicy>.
 * 3. Keeps compiler memory footprints low by isolating RK2 instantiations away from Euler/RK3.
 *
 * BurnerHandle type erasure prevents burner variants from multiplying the flux
 * template matrix; Dispatch_Euler.cpp documents the dispatch boundary.
 */

#include <string_view>

#include "DispatchImpl.h"

// Integrator isolated in this translation unit.
#include "../../numerics/integrator/TimeIntegratorRK2.h"   // Isolate RK2 instantiations in this unit.

// Runtime physics dispatch.
#include "../../numerics/burnsolver/BurnDispatch.h" // Provides make_handle().
#include "../../numerics/burnsolver/BurnerHandle.h"
#include "../../io/chk/CheckpointCompatibility.h"
#include "../../physics/eos/eosdispatch.h"
#include "../../physics/gravity/GravityDispatch.h"

void Dispatch_RK2(
    amr::AMRControl &amr_ctrl, const SimConfig &config,
    const SpeciesManager &specs, const RunState &run_state,
    const arch::dispatch::ResolvedExecutionPlan& plan,
    const arch::dispatch::ExecutionRequirements& requirements,
    const arch::dispatch::BackendResolution& backend,
    arch::dispatch::StartupOrder& startup_order)
{
    EOSDispatcher::dispatch_eos(
        plan.eos, config, specs,
        [&](auto &&eos, std::string_view loaded_table_sha256)
    {
        io::require_loaded_eos_table_compatible(
            run_state.verified_eos_table_sha256, loaded_table_sha256);
        const auto checkpoint_provenance = io::make_checkpoint_provenance(
            config, specs, plan.eos, requirements.burn,
            arch::dispatch::canonical_policy_name<
                arch::dispatch::NetworkPolicies>(plan.network),
            requirements.use_nse, loaded_table_sha256);
        using EosType = std::remove_cvref_t<decltype(eos)>;
        auto burn_handle = BurnDispatcher::make_host_handle<EosType>(
            config, plan, backend);

        auto grav_handle = Physical::Gravity::make_gravity(
            config, requirements.gravity);
        std::cout << "[Dispatch] Strategy: SSPRK2 + "
                  << config.numerics.solver_name << " + "
                  << config.numerics.reconstruction
                  << " (" << config.numerics.limiter << ")" << std::endl;

        DispatchImpl::launch_resolved_run<SolverRK2>(
            amr_ctrl, eos, grav_handle.get(), burn_handle, config, specs,
            run_state, checkpoint_provenance, plan, requirements, backend,
            startup_order);
    });
}
