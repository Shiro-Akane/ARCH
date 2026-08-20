/**
 * @file Dispatch_RK3.cpp
 * @brief Dispatcher component for the Runge-Kutta 3 (SSP-RK3) time integrator.
 *
 * Workflow:
 * 1. Acts as a standalone translation unit specifically for the RK3 scheme.
 * 2. Resolves EOS, Gravity, then erases the BurnerPolicy via BurnerHandle<EosPolicy>.
 * 3. Keeps compiler memory footprints low by isolating RK3 instantiations away from Euler/RK2.
 *
 * BurnerHandle type erasure prevents burner variants from multiplying the flux
 * template matrix; Dispatch_Euler.cpp documents the dispatch boundary.
 */

#include "DispatchImpl.h"

// Integrator isolated in this translation unit.
#include "../../numerics/integrator/TimeIntegratorRK3.h"   // Isolate RK3 instantiations in this unit.

// Runtime physics dispatch.
#include "../../numerics/burnsolver/BurnDispatch.h" // Provides make_handle().
#include "../../numerics/burnsolver/BurnerHandle.h"
#include "../../physics/eos/eosdispatch.h"
#include "../../physics/gravity/GravityDispatch.h"

void Dispatch_RK3(amr::AMRControl &amr_ctrl, const SimConfig &config, const SpeciesManager &specs, const RunState &run_state)
{
    EOSDispatcher::dispatch_eos(config, specs, [&](auto &&eos)
    {
        using EosType = std::remove_cvref_t<decltype(eos)>;
        auto burn_handle = BurnDispatcher::make_handle<EosType>(config);

        auto grav_handle = Physical::Gravity::make_gravity(config);
        std::cout << "[Dispatch] Strategy: SSPRK3 + "
                  << config.numerics.solver_name << " + "
                  << config.numerics.reconstruction
                  << " (" << config.numerics.limiter << ")" << std::endl;

        DispatchImpl::select_flux<SolverRK3>(amr_ctrl, eos, grav_handle.get(), burn_handle, config, specs, run_state);
    });
}
