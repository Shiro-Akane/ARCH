#include "DispatchImpl.h"

// Physics & Solvers Dispatchers
#include "../../physics/eos/eosdispatch.h"
#include "../../physics/gravity/GravityDispatch.h"
#include "../../numerics/burnsolver/BurnDispatch.h"

void Dispatch_RK3(FluidState &state, const Grid &grid, const SimConfig &config, const SpeciesManager &specs, const RunState &run_state)
{
    EOSDispatcher::dispatch_eos(config, specs, [&](auto &&eos)
    {
        using EosPolicy = std::remove_cvref_t<decltype(eos)>;
        auto burn = BurnDispatcher::make_handle<EosPolicy>(config);
        Physical::Gravity::dispatch_gravity(config, [&](auto &&gravity)
        {
            std::cout << "[Dispatch] Strategy: RK3 + "
                      << config.numerics.solver_name << " + "
                      << config.numerics.reconstruction
                      << " (" << config.numerics.limiter << ")" << std::endl;

            DispatchImpl::select_flux<SolverRK3>(state, eos, gravity, burn, grid, config, specs, run_state);
        });
    });
}
