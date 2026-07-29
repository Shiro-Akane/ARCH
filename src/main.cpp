/**
 * @file main.cpp
 * @brief Entry point for the ARCH Simulation Code.
 * *
 * * Workflow:
 * * 1. Parse command line arguments (Problem Type, Parameter File).
 * * 2. Load runtime parameters (Global Config).
 * * 3. Instantiate the specific Problem Generator via the Registry (Factory Pattern).
 * * 4. Configure Grid and Species based on the Problem definition.
 * * 5. Dispatch the simulation to the selected Numerical Solver (SW/LF/LW).
 */

#include <iostream>
#include <string>
#include <memory>

#include "../src/core/RuntimeParams.h"
#include "../src/core/ProblemRegistry.h"

#include "../src/interface/ProblemGenerator.h"
#include "../src/data/GlobalDefs.h"

#include "../src/physics/species/Species.h"
#include "../src/driver/SolverDispatch.h"
#include "../src/io/Logger.h"

// =========================================================
// =================== main function =======================
// =========================================================

int main(int argc, char **argv)
{
    // =========================================================
    // 1. Input Validation & Parameter Loading
    // =========================================================

    // Check command line arguments
    if (argc < 3)
    {
        std::cerr << "Usage: ./ARCH <ProblemType> <ParFile>" << std::endl;
        std::cerr << "Example: ./ARCH Sod runs/test1/arch.par" << std::endl;
        return 1;
    }

    // Capture inputs
    std::string problem_type = argv[1]; // e.g., "Sod", "BlastWave"
    std::string par_file = argv[2];     // e.g., "arch.par"

    // Load global simulation parameters from file
    // This initializes the singleton RuntimeParams so values can be accessed anywhere.
    SimConfig config;
    try
    {
        config = RuntimeParams::Load(par_file);
        
        std::string log_filename = config.io.out_dir + "/" + config.io.base_name + "_log.dat";
        Logger::Init(log_filename, config.io.restart);
        std::cout << "[Main] Console output is being recorded to log file: " << log_filename << std::endl;
        
        std::cout << "[Main] Parameters loaded from: " << par_file << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "[Fatal Error] " << e.what() << std::endl;
        return 1;
    }

    // =========================================================
    // 2. Problem Instantiation (Factory Pattern)
    // =========================================================

    // Create the specific problem instance dynamically based on the string name.
    // Returns a pointer to the base class 'ProblemGenerator'.
    auto problem_ptr = ProblemRegistry::Get().Create(problem_type);

    // Safety check: Ensure the problem was actually registered.
    if (!problem_ptr)
    {
        std::cerr << "[Error] Unknown problem type: " << problem_type << std::endl;
        std::cerr << "        Did you forget REGISTER_PROBLEM in the cpp file?" << std::endl;
        return 1;
    }
    std::cout << "[Main] Problem created: " << problem_type << std::endl;

    // =========================================================
    // 3. System Initialization & Configuration Extract
    // =========================================================

    // Ask the problem object for its specific grid/time configuration.
    // (This calls the user's setup callback internally).
    SpeciesManager specs;

    // Initialize the Species Manager.
    // The problem defines what materials (e.g., H2, He4) are in the simulation.
    problem_ptr->Setup(config, specs);

    // Select the Numerical Solver.
    // Defaults to "SW" (Steger-Warming) if not specified in the .par file.
    std::string solver_name = config.numerics.solver_name;

    // Print summary to console
    std::cout << "[Main] Configuration:" << std::endl;
    std::cout << "       Grid: " << config.grid.nx << " cells, CFL: " << config.numerics.cfl << std::endl;
    std::cout << "       Solver: " << solver_name << std::endl;
    std::cout << "       Species Count: " << specs.count() << std::endl;

    // =========================================================
    // 4. Execution (Dispatch to Core Loop)
    // =========================================================

    // Hand over control to the Solver Factory.
    // This function instantiates the correct Solver Template and starts the time loop.
    try
    {
        DispatchSolver(solver_name, *problem_ptr, config, specs);
    }
    catch (const std::exception &e)
    {
        std::cerr << "[Fatal Error] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}