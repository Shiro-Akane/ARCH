/**
 * @file SolverFactory.h
 * @brief Dispatcher and Initializer for the simulation engine.
 * * Implements a Factory-like pattern to select the specific numerical scheme
 * * (Solver) at runtime based on configuration strings.
 * * Responsible for assembling the core objects (Grid, FluidState, EOS)
 * * and handing control over to the main Driver loop.
 */

#pragma once
#include <string>
#include <iostream>

#include "../../grid/Grid.h"

#include "../../data/FluidState.h"
#include "../../data/GlobalDefs.h"

#include "../../interface/ProblemGenerator.h"

#include "../../driver/Driver.h"

#include "../../physics/eos/IdealGas.h"
#include "../../physics/species/Species.h"

// Concrete Solvers
#include "SolverSW.h" // Steger-Warming
#include "SolverLF.h" // Lax-Friedrichs
#include "SolverLW.h" // Lax-Wendroff

// ------------------------------------------------------------------
//  Template Launcher
// ------------------------------------------------------------------

/**
 * @brief Orchestrates the initialization and startup of a specific solver type.
 * * This template allows us to write the initialization logic once, while supporting
 * * different solvers that might have different requirements (e.g., different Ghost Cell counts).
 * * @tparam SolverT  The specific solver class (e.g., SolverSW, SolverLF).
 * @tparam ProblemT The problem generator type (usually GenericProblemGenerator).
 * * @param problem Reference to the problem setup object.
 * @param config  Simulation configuration (nx, domain length, etc.).
 * @param specs   Species manager.
 */
template <typename SolverT, typename ProblemT>
void StartSimulation(ProblemT &problem, const SimConfig &config, const SpeciesManager &specs)
{
    std::cout << "Initializing Simulation with Solver: " << SolverT::name() << std::endl;

    // 1. Setup Grid
    // Note: SolverT::NG is a static constant defined in each solver class.
    // Different schemes require different numbers of ghost cells (stencil width).
    double domain_len = config.grid.x_max - config.grid.x_min;

    Grid grid(config.grid.nx, SolverT::NG, config.grid.x_min, domain_len);

    // 2. Setup Physics (Equation of State)
    IdealGas eos(specs);

    // 3. Allocate Memory (FluidState)
    FluidState state(grid, specs.count());

    // 4. Initialize Flow Field (t=0)
    // Uses the user-defined problem logic to fill 'state'
    problem.InitializeData(state, grid, eos);

    // 5. Hand over control to the Driver Loop
    // The driver will run the time-stepping loop until t_end
    run_simulation<SolverT>(state, eos, grid, config, specs);
}

// ------------------------------------------------------------------
//  Factory Dispatcher
// ------------------------------------------------------------------

/**
 * @brief Selects the numerical method based on the string name and launches the simulation.
 * * @param solver_name The string identifier for the solver (e.g., "SW", "LW").
 * @param problem     The problem generator instance.
 * @param cfg         Global configuration.
 * @param specs       Species definitions.
 */
void DispatchSolver(const std::string &solver_name,
                    ProblemGenerator &problem,
                    const SimConfig &cfg,
                    const SpeciesManager &specs)
{
    // String matching to select the correct template instantiation
    if (solver_name == "SW")
    {
        // Steger-Warming Flux Vector Splitting (Upwind, 1st Order)
        StartSimulation<SolverSW>(problem, cfg, specs);
    }
    else if (solver_name == "LF")
    {
        // Lax-Friedrichs (Central, 1st Order, Dissipative)
        StartSimulation<SolverLF>(problem, cfg, specs);
    }
    else if (solver_name == "LW")
    {
        // Lax-Wendroff (Central, 2nd Order, Dispersive)
        StartSimulation<SolverLW>(problem, cfg, specs);
    }
    // Future expansion:
    // else if (solver_name == "Roe") { ... }
    // else if (solver_name == "HLLC") { ... }
    else
    {
        std::cerr << "Error: Unknown solver " << solver_name << std::endl;
        exit(1);
    }
}