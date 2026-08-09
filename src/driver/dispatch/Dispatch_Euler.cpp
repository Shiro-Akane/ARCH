/**
 * @file Dispatch_Euler.cpp
 * @brief Dispatcher component for the Forward Euler time integrator.
 * *
 * * Workflow:
 * * 1. Acts as a standalone translation unit specifically for the Euler scheme.
 * * 2. Resolves EOS, Gravity, then erases the BurnerPolicy via BurnerHandle<EosPolicy>.
 * * 3. Only Euler instantiations are in this TU; RK2/RK3 are in separate TUs.
 * *
 * * Memory note: BurnerHandle erases the BurnerPolicy *before* entering select_flux,
 * * so run_simulation is NOT templated on BurnerPolicy. This reduces instantiation
 * * count by ~13x (from 4680 to ~360) and peak RSS from ~3.6 GB to < 600 MB per TU.
 */

#include "DispatchImpl.h"
#include "../../numerics/integrator/TimeIntegratorEuler.h"  // This TU only needs Euler

// Physics & Solvers Dispatchers
#include "../../physics/eos/eosdispatch.h"
#include "../../physics/gravity/GravityDispatch.h"
#include "../../numerics/burnsolver/BurnDispatch.h"    // Only for make_handle()
#include "../../numerics/burnsolver/BurnerHandle.h"

void Dispatch_Euler(amr::AMRControl &amr_ctrl, const SimConfig &config, const SpeciesManager &specs, const RunState &run_state)
{
    EOSDispatcher::dispatch_eos(config, specs, [&](auto &&eos) {
        // Type-erase the BurnerPolicy HERE, inside the EOS lambda where EosPolicy is known.
        // BurnerHandle<EosPolicy> wraps any burner via a single function pointer ---
        // the 12 concrete ODE/network/linsolver combinations are NOT propagated
        // further as template parameters, so select_flux/run_simulation only sees
        // BurnerHandle<EosPolicy> as the burn type. This is the key memory saving.
        using EosType = std::remove_cvref_t<decltype(eos)>;
        auto burn_handle = BurnDispatcher::make_handle<EosType>(config);

        auto grav_handle = Physical::Gravity::make_gravity(config);
        std::cout << "[Dispatch] Strategy: Euler/RK1 + "
                  << config.numerics.solver_name << " + "
                  << config.numerics.reconstruction
                  << " (" << config.numerics.limiter << ")" << std::endl;

        DispatchImpl::select_flux<SolverEuler>(amr_ctrl, eos, grav_handle.get(), burn_handle, config, specs, run_state);
    });
}
