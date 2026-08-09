/**
 * @file DispatchImpl.h
 * @brief Template matrix factory for dispatching physical and numerical solver policies.
 * *
 * * Workflow:
 * * 1. Instantiates the physics models (EOS, Gravity, Reaction Networks).
 * * 2. Instantiates the hydrodynamics components (Flux solvers, Limiters).
 * * 3. Binds them into a concrete template sequence to call run_simulation().
 * * 4. Used heavily to avoid bloated compilation objects by keeping template instantiations segregated.
 */

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

// NOTE: TimeIntegrator headers are intentionally NOT included here.
// Each Dispatch_*.cpp must include only the integrator it needs (e.g.,
// Dispatch_Euler.cpp includes TimeIntegratorEuler.h) to avoid instantiating
// the entire template matrix in every translation unit simultaneously.
// Including all three here caused ~3.8GB peak RSS per TU with -j8 OOM.

// 3. The Main Loop
#include "../Driver.h"

// 4. The Integrator Implementations
#include "../../numerics/integrator/HydroSolverImpl.h"
#include "../../physics/gravity/IGravityPolicy.h"
#include "../../numerics/burnsolver/BurnerHandle.h"

namespace DispatchImpl {

// Level 4: Execute the simulation with the fully assembled type
template <typename TimeIntegrator, typename FluxSchemePolicy, typename EosPolicy>
void launch_run(amr::AMRControl &amr_ctrl, const EosPolicy &eos,
                const Physical::Gravity::IGravityPolicy* gravity,
                const BurnerHandle<EosPolicy> &burn,
                const SimConfig &config,
                const SpeciesManager &specs, const RunState &run_state)
{
    // Instantiate the concrete HydroSolver for this Eos and Flux combo
    Numerics::HydroSolverImpl<EosPolicy, FluxSchemePolicy> hydro_solver(eos);

    std::string integrator_name = TimeIntegrator::name() + " + " + FluxSchemePolicy::name();

    // Call Driver.h main loop, passing the integrator's solve static method as a function pointer!
    run_simulation<EosPolicy>(amr_ctrl, eos, gravity, burn, &hydro_solver,
                              &TimeIntegrator::template solve<BCHandler>,
                              integrator_name, config, specs, run_state);
}

// Level 3: Select Limiter (For MUSCL)
template <typename TimeIntegrator, template <typename> class FluxScheme, typename EosPolicy>
void select_limiter(amr::AMRControl &amr_ctrl, const EosPolicy &eos,
                    const Physical::Gravity::IGravityPolicy* gravity,
                    const BurnerHandle<EosPolicy> &burn,
                    const SimConfig &config, const SpeciesManager &specs, const RunState &run_state)
{
    std::string lim = config.numerics.limiter;

    if (lim == "minmod" || lim == "MinMod")
    {
        using MyRecon = MusclReconstruction<MinMod>;
        using MyFlux = FluxScheme<MyRecon>;
        launch_run<TimeIntegrator, MyFlux>(amr_ctrl, eos, gravity, burn, config, specs, run_state);
    }
    else if (lim == "superbee" || lim == "SuperBee")
    {
        using MyRecon = MusclReconstruction<SuperBee>;
        using MyFlux = FluxScheme<MyRecon>;
        launch_run<TimeIntegrator, MyFlux>(amr_ctrl, eos, gravity, burn, config, specs, run_state);
    }
    else if (lim == "vanleer" || lim == "VanLeer")
    {
        using MyRecon = MusclReconstruction<VanLeer>;
        using MyFlux = FluxScheme<MyRecon>;
        launch_run<TimeIntegrator, MyFlux>(amr_ctrl, eos, gravity, burn, config, specs, run_state);
    }
    else if (lim == "mc" || lim == "MC")
    {
        using MyRecon = MusclReconstruction<McLimiter>;
        using MyFlux = FluxScheme<MyRecon>;
        launch_run<TimeIntegrator, MyFlux>(amr_ctrl, eos, gravity, burn, config, specs, run_state);
    }
    else
    {
        std::cerr << "[Warning] Unknown limiter '" << lim << "', defaulting to MinMod." << std::endl;
        using MyRecon = MusclReconstruction<MinMod>;
        using MyFlux = FluxScheme<MyRecon>;
        launch_run<TimeIntegrator, MyFlux>(amr_ctrl, eos, gravity, burn, config, specs, run_state);
    }
}

// Level 2: Select Reconstruction Scheme
template <typename TimeIntegrator, template <typename> class FluxScheme, typename EosPolicy>
void select_reconstruction(amr::AMRControl &amr_ctrl, const EosPolicy &eos,
                           const Physical::Gravity::IGravityPolicy* gravity,
                           const BurnerHandle<EosPolicy> &burn,
                           const SimConfig &config, const SpeciesManager &specs, const RunState &run_state)
{
    std::string recon = config.numerics.reconstruction;

    if (recon == "pcm" || recon == "PCM" || recon == "donor_cell")
    {
        using MyRecon = PCMReconstruction;
        using MyFlux = FluxScheme<MyRecon>;
        launch_run<TimeIntegrator, MyFlux>(amr_ctrl, eos, gravity, burn, config, specs, run_state);
    }
    else if (recon == "plm" || recon == "PLM" || recon == "muscl" || recon == "MUSCL")
    {
        select_limiter<TimeIntegrator, FluxScheme>(amr_ctrl, eos, gravity, burn, config, specs, run_state);
    }
    else if (recon == "ppm" || recon == "PPM")
    {
        using MyRecon = PPMReconstruction;
        using MyFlux = FluxScheme<MyRecon>;
        launch_run<TimeIntegrator, MyFlux>(amr_ctrl, eos, gravity, burn, config, specs, run_state);
    }
    else
    {
        std::cerr << "[Warning] Unknown reconstruction '" << recon << "', defaulting to PCM." << std::endl;
        using MyRecon = PCMReconstruction;
        using MyFlux = FluxScheme<MyRecon>;
        launch_run<TimeIntegrator, MyFlux>(amr_ctrl, eos, gravity, burn, config, specs, run_state);
    }
}

// Level 1: Select Flux Scheme
template <typename TimeIntegrator, typename EosPolicy>
void select_flux(amr::AMRControl &amr_ctrl, const EosPolicy &eos,
                 const Physical::Gravity::IGravityPolicy* gravity,
                 const BurnerHandle<EosPolicy> &burn,
                 const SimConfig &config, const SpeciesManager &specs, const RunState &run_state)
{
    std::string flux = config.numerics.solver_name;

    if (flux == "VL" || flux == "VanLeer")
    {
        select_reconstruction<TimeIntegrator, FluxVL>(amr_ctrl, eos, gravity, burn, config, specs, run_state);
    }
    else if (flux == "SW" || flux == "StegerWarming")
    {
        select_reconstruction<TimeIntegrator, FluxSW>(amr_ctrl, eos, gravity, burn, config, specs, run_state);
    }
    else if (flux == "Roe" || flux == "roe")
    {
        select_reconstruction<TimeIntegrator, FluxRoe>(amr_ctrl, eos, gravity, burn, config, specs, run_state);
    }
    else if (flux == "HLL" || flux == "hll")
    {
        select_reconstruction<TimeIntegrator, FluxHLL>(amr_ctrl, eos, gravity, burn, config, specs, run_state);
    }
    else if (flux == "HLLC" || flux == "hllc")
    {
        select_reconstruction<TimeIntegrator, FluxHLLC>(amr_ctrl, eos, gravity, burn, config, specs, run_state);
    }
    else
    {
        std::cerr << "[Warning] Unknown solver '" << flux << "', defaulting to HLLC." << std::endl;
        select_reconstruction<TimeIntegrator, FluxHLLC>(amr_ctrl, eos, gravity, burn, config, specs, run_state);
    }
}

} // namespace DispatchImpl

void Dispatch_Euler(amr::AMRControl &amr_ctrl, const SimConfig &config, const SpeciesManager &specs, const RunState &run_state);
void Dispatch_RK2(amr::AMRControl &amr_ctrl, const SimConfig &config, const SpeciesManager &specs, const RunState &run_state);
void Dispatch_RK3(amr::AMRControl &amr_ctrl, const SimConfig &config, const SpeciesManager &specs, const RunState &run_state);
