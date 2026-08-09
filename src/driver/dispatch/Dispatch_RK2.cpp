/**
 * @file Dispatch_RK2.cpp
 * @brief Dispatcher component for the Runge-Kutta 2 (Heun's method) time integrator.
 * *
 * * Workflow:
 * * 1. Acts as a standalone translation unit specifically for the RK2 scheme.
 * * 2. Resolves EOS, Gravity, then erases the BurnerPolicy via BurnerHandle<EosPolicy>.
 * * 3. Keeps compiler memory footprints low by isolating RK2 instantiations away from Euler/RK3.
 * *
 * * Memory note: BurnerHandle type erasure prevents 13 burner variants from
 * * multiplying into the full template matrix. See Dispatch_Euler.cpp for details.
 */

#include "DispatchImpl.h"
#include "../../numerics/integrator/TimeIntegratorRK2.h"   // This TU only needs RK2

// Physics & Solvers Dispatchers
#include "../../physics/eos/eosdispatch.h"
#include "../../physics/gravity/GravityDispatch.h"
#include "../../numerics/burnsolver/BurnDispatch.h"    // Only for make_handle()
#include "../../numerics/burnsolver/BurnerHandle.h"

void Dispatch_RK2(amr::AMRControl &amr_ctrl, const SimConfig &config, const SpeciesManager &specs, const RunState &run_state)
{
    EOSDispatcher::dispatch_eos(config, specs, [&](auto &&eos)
    {
        using EosType = std::remove_cvref_t<decltype(eos)>;
        auto burn_handle = BurnDispatcher::make_handle<EosType>(config);

        auto grav_handle = Physical::Gravity::make_gravity(config);
        std::cout << "[Dispatch] Strategy: SSPRK2 + "
                  << config.numerics.solver_name << " + "
                  << config.numerics.reconstruction
                  << " (" << config.numerics.limiter << ")" << std::endl;

        DispatchImpl::select_flux<SolverRK2>(amr_ctrl, eos, grav_handle.get(), burn_handle, config, specs, run_state);
    });
}
