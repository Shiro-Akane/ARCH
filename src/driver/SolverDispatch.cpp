/**
 * @file SolverDispatch.cpp
 * @brief The "Switchboard" for the simulation.
 */

#include "SolverDispatch.h"

#include <string>
#include <iostream>
#include <stdexcept>

// 1. Core Data Structures
#include "../data/FluidState.h"
#include "../grid/Grid.h"
#include "../core/RuntimeParams.h"
#include "../interface/ProblemGenerator.h"
#include "../io/IO.h"

// 2. Dispatch declarations
void Dispatch_Euler(FluidState &state, const Grid &grid, const SimConfig &config, const SpeciesManager &specs, const RunState &run_state);
void Dispatch_RK2(FluidState &state, const Grid &grid, const SimConfig &config, const SpeciesManager &specs, const RunState &run_state);
void Dispatch_RK3(FluidState &state, const Grid &grid, const SimConfig &config, const SpeciesManager &specs, const RunState &run_state);

// =========================================================
// Helper: Determine Ghost Cells based on Config
// =========================================================
int determine_required_ng(const SimConfig &config)
{
    std::string recon = config.numerics.reconstruction;

    int ng_recon = 1; // 默认 PCM

    if (recon == "pcm" || recon == "PCM")
    {
        ng_recon = 1;
    }
    else if (recon == "muscl" || recon == "MUSCL")
    {
        ng_recon = 2; // MUSCL 需要 i-1, i, i+1, i+2，所以单侧需要 2 层
    }
    else if (recon == "ppm" || recon == "PPM" || recon == "weno5")
    {
        ng_recon = 3; // PPM/WENO 通常需要更宽的模板
    }
    else
    {
        ng_recon = 2;
    }

    int ng_flux = 1;
    if (config.numerics.solver_name == "SomeHighOrderFlux")
    {
        ng_flux = 3;
    }

    return std::max(ng_recon, ng_flux);
}

// =========================================================
// The Public Dispatch Function
// =========================================================

void DispatchSolver(const std::string &solver_name,
                    ProblemGenerator &problem,
                    const SimConfig &config,
                    const SpeciesManager &specs)
{
    std::cout << "[Dispatch] Initializing System..." << std::endl;

    int required_ng = determine_required_ng(config);
    std::cout << "[Dispatch] Determined Ghost Cell count: " << required_ng << std::endl;

    Grid grid(config.grid, required_ng);

    std::cout << "[Dispatch] Grid Topology: " << grid.dim << "D "
              << config.grid.geometry << " ("
              << grid.n1 << " x " << grid.n2 << " x " << grid.n3 << ")" << std::endl;

    FluidState state(grid, specs.count());

    RunState run_state;

    if (config.io.restart && !config.io.restart_file.empty())
    {
        std::cout << "[Dispatch] Restarting from checkpoint: " << config.io.restart_file << std::endl;
        read_chk(config.io.restart_file, state, grid, run_state);
    }
    else
    {
        std::cout << "[Dispatch] Initializing Data via Problem Generator..." << std::endl;
        problem.InitializeData(state, grid, config, specs);

        int center_idx = grid.GetIndex(grid.Is(), grid.Js(), grid.Ks());
        std::cout << "[Dispatch Debug] After InitializeData, state.X(0, center_idx) = " << state.X(0, center_idx) << std::endl;
    }

    std::cout << "[Dispatch] Resolving Gravity Policy..." << std::endl;
    std::string grav_type = config.physics.gravity.type;
    std::cout << "           -> Type: " << grav_type;

    if (grav_type == "external" || grav_type == "External" || grav_type == "EXTERNAL")
    {
        std::cout << " | g = (" << config.physics.gravity.g_x << ", "
                  << config.physics.gravity.g_y << ", "
                  << config.physics.gravity.g_z << ")";
    }
    else if (grav_type == "self" || grav_type == "Self" || grav_type == "SELF")
    {
        std::cout << " | G_const = " << config.physics.gravity.G_const;
    }
    std::cout << std::endl;

    if (config.physics.diffusion.use_diffusion)
    {
        std::cout << "           -> Diffusion Solver Initialized: " << config.physics.diffusion.integrator 
                  << " (CFL_diff = " << config.physics.diffusion.diff_cfl << ")" << std::endl;
    }

    const std::string &time_int = config.numerics.time_integrator;
    if (time_int == "RK2" || time_int == "SSPRK2")
    {
        Dispatch_RK2(state, grid, config, specs, run_state);
    }
    else if (time_int == "RK3" || time_int == "SSPRK3")
    {
        Dispatch_RK3(state, grid, config, specs, run_state);
    }
    else if (time_int == "Euler" || time_int == "RK1")
    {
        Dispatch_Euler(state, grid, config, specs, run_state);
    }
    else
    {
        std::cerr << "[Warning] Unknown time integrator '" << time_int << "', defaulting to SSPRK2." << std::endl;
        Dispatch_RK2(state, grid, config, specs, run_state);
    }
}
