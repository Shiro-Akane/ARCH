#pragma once

#include <string>
#include <iostream>
#include <stdexcept>

// 1. Core Data Structures
#include "../../data/FluidState.h"
#include "../../grid/Grid.h"
#include "../../core/RuntimeParams.h"
#include "../../interface/ProblemGenerator.h"

// 2. Physics & Solvers
#include "../../numerics/flux/FluxVL.h"
#include "../../numerics/flux/FluxSW.h"
#include "../../numerics/flux/FluxRoe.h"
#include "../../numerics/flux/FluxHLL.h"
#include "../../numerics/flux/FluxHLLC.h"

#include "../../numerics/reconstruction/Reconstruction.h"
#include "../../numerics/reconstruction/Limiters.h"

#include "../../numerics/integrator/TimeIntegratorEuler.h"
#include "../../numerics/integrator/TimeIntegratorRK2.h"
#include "../../numerics/integrator/TimeIntegratorRK3.h"

// 3. The Main Loop
#include "../Driver.h"

namespace DispatchImpl {

// Level 4: Execute the simulation with the fully assembled type
template <typename SolverType, typename EosPolicy, typename GravityPolicy, typename BurnerPolicy>
void launch_run(FluidState &state, const EosPolicy &eos, GravityPolicy &gravity, BurnerPolicy &burn,
                const Grid &grid, const SimConfig &config,
                const SpeciesManager &specs, const RunState &run_state)
{
    // 调用 Driver.h 中的主循环
    run_simulation<SolverType>(state, eos, gravity, burn, grid, config, specs, run_state);
}

// Level 3: Select Limiter (For MUSCL)
template <template <typename> class TimeIntegrator, template <typename> class FluxScheme, typename EosPolicy, typename GravityPolicy, typename BurnerPolicy>
void select_limiter(FluidState &state, const EosPolicy &eos, GravityPolicy &gravity, BurnerPolicy &burn, const Grid &grid,
                    const SimConfig &config, const SpeciesManager &specs, const RunState &run_state)
{
    std::string lim = config.numerics.limiter;

    if (lim == "minmod" || lim == "MinMod")
    {
        using MyRecon = MusclReconstruction<MinMod>;
        using MySolver = TimeIntegrator<FluxScheme<MyRecon>>;
        launch_run<MySolver>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else if (lim == "superbee" || lim == "SuperBee")
    {
        using MyRecon = MusclReconstruction<SuperBee>;
        using MySolver = TimeIntegrator<FluxScheme<MyRecon>>;
        launch_run<MySolver>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else if (lim == "vanleer" || lim == "VanLeer")
    {
        using MyRecon = MusclReconstruction<VanLeer>;
        using MySolver = TimeIntegrator<FluxScheme<MyRecon>>;
        launch_run<MySolver>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else if (lim == "mc" || lim == "MC")
    {
        using MyRecon = MusclReconstruction<McLimiter>;
        using MySolver = TimeIntegrator<FluxScheme<MyRecon>>;
        launch_run<MySolver>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else
    {
        std::cerr << "[Warning] Unknown limiter '" << lim << "', defaulting to MinMod." << std::endl;
        using MyRecon = MusclReconstruction<MinMod>;
        using MySolver = TimeIntegrator<FluxScheme<MyRecon>>;
        launch_run<MySolver>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
}

// Level 2: Select Reconstruction Scheme
template <template <typename> class TimeIntegrator, template <typename> class FluxScheme, typename EosPolicy, typename GravityPolicy, typename BurnerPolicy>
void select_reconstruction(FluidState &state, const EosPolicy &eos, GravityPolicy &gravity, BurnerPolicy &burn, const Grid &grid,
                           const SimConfig &config, const SpeciesManager &specs, const RunState &run_state)
{
    std::string recon = config.numerics.reconstruction;
    if (recon == "pcm" || recon == "PCM")
    {
        using MyRecon = PCMReconstruction;
        using MySolver = TimeIntegrator<FluxScheme<MyRecon>>;
        launch_run<MySolver>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else if (recon == "muscl" || recon == "MUSCL")
    {
        select_limiter<TimeIntegrator, FluxScheme>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else if (recon == "ppm" || recon == "PPM")
    {
        using MyRecon = PPMReconstruction;
        using MySolver = TimeIntegrator<FluxScheme<MyRecon>>;
        launch_run<MySolver>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else
    {
        throw std::runtime_error("Unknown Reconstruction method: " + recon);
    }
}

// Level 1: Select Flux Scheme
template <template <typename> class TimeIntegrator, typename EosPolicy, typename GravityPolicy, typename BurnerPolicy>
void select_flux(FluidState &state, const EosPolicy &eos, GravityPolicy &gravity, BurnerPolicy &burn, const Grid &grid,
                 const SimConfig &config, const SpeciesManager &specs, const RunState &run_state)
{
    std::string flux = config.numerics.solver_name;

    if (flux == "VL" || flux == "VanLeer")
    {
        select_reconstruction<TimeIntegrator, FluxVL>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else if (flux == "SW" || flux == "StegerWarming")
    {
        select_reconstruction<TimeIntegrator, FluxSW>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else if (flux == "Roe" || flux == "roe")
    {
        select_reconstruction<TimeIntegrator, FluxRoe>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else if (flux == "HLL" || flux == "hll")
    {
        select_reconstruction<TimeIntegrator, FluxHLL>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else if (flux == "HLLC")
    {
        select_reconstruction<TimeIntegrator, FluxHLLC>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else
    {
        throw std::runtime_error("Unknown Flux Solver: " + flux);
    }
}

} // namespace DispatchImpl

void Dispatch_Euler(FluidState &state, const Grid &grid, const SimConfig &config, const SpeciesManager &specs, const RunState &run_state);
void Dispatch_RK2(FluidState &state, const Grid &grid, const SimConfig &config, const SpeciesManager &specs, const RunState &run_state);
void Dispatch_RK3(FluidState &state, const Grid &grid, const SimConfig &config, const SpeciesManager &specs, const RunState &run_state);

