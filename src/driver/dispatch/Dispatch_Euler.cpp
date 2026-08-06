/**
 * @file Dispatch_Euler.cpp
 * @brief Dispatcher component for the Forward Euler time integrator.
 * *
 * * Workflow:
 * * 1. Acts as a standalone translation unit specifically for the Euler scheme.
 * * 2. Instantiates all possible template combinations of Flux, EOS, and Burner solvers.
 * * 3. Keeps compiler memory footprints low by isolating Euler instantiations away from RK2/RK3.
 */

#include "DispatchImpl.h"

// Physics & Solvers Dispatchers
#include "../../physics/eos/eosdispatch.h"
#include "../../physics/gravity/GravityDispatch.h"
#include "../../numerics/burnsolver/BurnDispatch.h"

void Dispatch_Euler(FluidState &state, const Grid &grid, const SimConfig &config, const SpeciesManager &specs, const RunState &run_state)
{
    EOSDispatcher::dispatch_eos(config, specs, [&](auto &&eos)
    {
        using EosPolicy = std::remove_cvref_t<decltype(eos)>;
        auto burn = BurnDispatcher::make_handle<EosPolicy>(config);
        Physical::Gravity::dispatch_gravity(config, [&](auto &&gravity)
        {
            std::cout << "[Dispatch] Strategy: Euler/RK1 + "
                      << config.numerics.solver_name << " + "
                      << config.numerics.reconstruction
                      << " (" << config.numerics.limiter << ")" << std::endl;

            DispatchImpl::select_flux<SolverEuler>(state, eos, gravity, burn, grid, config, specs, run_state);
        });
    });
}
