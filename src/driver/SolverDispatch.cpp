/**
 * @file SolverDispatch.cpp
 * @brief The "Switchboard" for the simulation.
 */

/**
 * Workflow:
 * 1. Select the configured policy and determine a stable macro step.
 * 2. Apply hydro, diffusion, gravity, and burn operators in the documented order.
 * 3. Synchronize AMR leaves and emit diagnostics before continuing the evolution.
 */

#include "SolverDispatch.h"
#include "DriverStartup.h"

#include <string>
#include <iostream>
#include <stdexcept>

// 1. Core Data Structures
#include "../data/FluidState.h"
#include "../grid/Grid.h"
#include "../core/RuntimeParams.h"
#include "../interface/ProblemGenerator.h"
#include "../io/IO.h"

#include "../amr/AMRControl.h"

// 2. Dispatch declarations
void Dispatch_Euler(amr::AMRControl &amr_ctrl, const SimConfig &config, const SpeciesManager &specs, const RunState &run_state);
void Dispatch_RK2(amr::AMRControl &amr_ctrl, const SimConfig &config, const SpeciesManager &specs, const RunState &run_state);
void Dispatch_RK3(amr::AMRControl &amr_ctrl, const SimConfig &config, const SpeciesManager &specs, const RunState &run_state);

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
        ng_recon = 2;
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
    if (required_ng > amr::MAX_NG) {
        throw std::runtime_error("Required ghost cells exceed AMR static MAX_NG!");
    }
    std::cout << "[Dispatch] Required Ghost Cell count: " << required_ng << " (Static MAX_NG: " << amr::MAX_NG << ")" << std::endl;

    // --- AMR Initialization ---
    int max_blocks = config.grid.amr_max_blocks > 0 ? config.grid.amr_max_blocks : 10000;
    amr::AMRControl amr_ctrl(max_blocks, config.grid.dim);

    amr_ctrl.tree->ConfigureRefinementSpecies(config.amr, specs);
    RunState run_state;

    if (config.io.restart && !config.io.restart_file.empty())
    {
        std::cout << "[Dispatch] Restarting from checkpoint: " << config.io.restart_file << std::endl;
        read_chk(config.io.restart_file, amr_ctrl, run_state, config, specs.count());
        std::cout << ">>> Grid Config | Dim: " << config.grid.dim
                  << " | Geometry: " << config.grid.geometry << std::endl;
        DriverStartup::print_amr_resolution_summary(config);
    }
    else
    {
        std::cout << "[Dispatch] Initializing Root Grid (Level 0)..." << std::endl;
        amr_ctrl.tree->InitRootGrid(config, specs.count());
        std::cout << "[Dispatch] Initializing Data via Problem Generator..." << std::endl;
        problem.InitializeData(amr_ctrl, config, specs);
        std::cout << ">>> Grid Config | Dim: " << config.grid.dim
                  << " | Geometry: " << config.grid.geometry << std::endl;
        DriverStartup::print_amr_resolution_summary(config);

        if (config.amr.lrefinemax > 0) {
            const bool eos_indicator = config.amr.refine_on_p || config.amr.refine_on_temp ||
                config.amr.refine_on_entropy;
            if (eos_indicator) {
                // EOS policies are selected by the later template dispatch.  Do not
                // approximate pressure or temperature here: initial refinement is
                // deferred until Driver has installed the actual EOS callback.
                amr_ctrl.tree->DeferInitialRefinement(config.amr.lrefinemax);
            } else {
                std::cout << "[Dispatch] Performing initial AMR refinement loop..." << std::endl;
                for (int l = 0; l < config.amr.lrefinemax; ++l) {
                    bool changed = amr_ctrl.tree->Regrid(config);
                    if (changed) {
                        std::cout << "           -> Refining initial condition (Pass " << l + 1 << ")..." << std::endl;
                        // Re-initialize exact data on newly created fine blocks.
                        problem.InitializeData(amr_ctrl, config, specs);
                    } else {
                        break;
                    }
                }
            }
        }
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
        Dispatch_RK2(amr_ctrl, config, specs, run_state);
    }
    else if (time_int == "RK3" || time_int == "SSPRK3")
    {
        Dispatch_RK3(amr_ctrl, config, specs, run_state);
    }
    else if (time_int == "Euler" || time_int == "RK1")
    {
        Dispatch_Euler(amr_ctrl, config, specs, run_state);
    }
    else
    {
        std::cerr << "[Warning] Unknown time integrator '" << time_int << "', defaulting to SSPRK2." << std::endl;
        Dispatch_RK2(amr_ctrl, config, specs, run_state);
    }
}
