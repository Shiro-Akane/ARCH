/**
 * @file main.cpp
 * @brief Command-line entry point for the ARCH simulation program.
 *
 * The entry point assembles the top-level workflow and contains no numerical
 * method implementation:
 * 1. Read the problem name and parameter-file path.
 * 2. Parse runtime configuration and initialize output and logging.
 * 3. Construct the requested problem through the registry.
 * 4. Let the problem configure species and initial state.
 * 5. Pass the resolved solver name to runtime dispatch and start integration.
 *
 * This boundary keeps command-line handling, problem definitions, and
 * numerical policies independent so that future backends and solvers can be
 * added without changing the application entry point.
 */

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

// Application control and public problem interface.
#include "core/ProblemRegistry.h"
#include "core/RuntimeParams.h"
#include "interface/ProblemGenerator.h"

// Runtime data required for startup reporting and dispatch.
#include "amr/AmrDefines.h"
#include "data/GlobalDefs.h"
#include "physics/species/Species.h"

// Execution and logging services.
#include "driver/SolverDispatch.h"
#include "io/Logger.h"

int main(int argc, char **argv)
{
    // The CLI contract has three entries: executable, problem name, and
    // parameter file. Therefore argc must be at least three.
    if (argc < 3)
    {
        std::cerr << "Usage: ./ARCH <ProblemType> <ParFile>" << std::endl;
        std::cerr << "Example: ./ARCH Sod runs/test1/arch.par" << std::endl;
        return 1;
    }

    // Preserve both arguments verbatim: the registry consumes the problem
    // name, while the configuration parser consumes the path.
    std::string problem_type = argv[1]; // For example, "Sod" or "Sedov".
    std::string par_file = argv[2];     // For example, "simulation/Sod/Sod.par".

    // Load configuration before constructing the problem so every downstream
    // module observes the same SimConfig instance.
    SimConfig config;
    try
    {
        config = RuntimeParams::Load(par_file);

        // Plot output and checkpoints depend on out_dir.  Logs may be placed
        // in a separate directory so HDF5-only trees remain easy to archive
        // and process; existing inputs retain out_dir as the default.
        std::filesystem::create_directories(config.io.out_dir);
        const std::string log_dir =
            config.Get<std::string>("log_dir", config.io.out_dir);
        std::filesystem::create_directories(log_dir);
        std::string log_filename = log_dir + "/" + config.io.base_name + "_log.dat";
        Logger::Init(log_filename, config.io.restart);
        std::cout << "[Main] Console output is being recorded to log file: " << log_filename << std::endl;

        std::cout << "[Main] Parameters loaded from: " << par_file << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "[Fatal Error] " << e.what() << std::endl;
        return 1;
    }

    // The registry maps a string to a ProblemGenerator, so main does not need
    // to include or switch over concrete problem types.
    auto problem_ptr = ProblemRegistry::Get().Create(problem_type);

    // A null pointer means that the corresponding translation unit did not
    // register the problem through REGISTER_PROBLEM. Grid setup cannot proceed.
    if (!problem_ptr)
    {
        std::cerr << "[Error] Unknown problem type: " << problem_type << std::endl;
        std::cerr << "        Did you forget REGISTER_PROBLEM in the cpp file?" << std::endl;
        return 1;
    }
    std::cout << "[Main] Problem created: " << problem_type << std::endl;

    // Species order is shared by the EOS, reaction network, and HDF5 schema.
    // The problem must establish it exactly once before solver dispatch.
    SpeciesManager specs;

    // Setup invokes the user problem's configuration callback and validates
    // its species and case-specific parameters.
    problem_ptr->Setup(config, specs);

    // RuntimeParams has validated solver_name; DispatchSolver selects the
    // concrete compile-time policy combination.
    std::string solver_name = config.numerics.solver_name;

    // Emit the minimum auditable configuration summary before computation so
    // a log can be matched to its parameter file.
    std::cout << "[Main] Configuration:" << std::endl;
    std::cout << "       Grid: " << config.grid.nblockx1 * amr::BLOCK_NX << " cells, CFL: " << config.numerics.cfl << std::endl;
    std::cout << "       Solver: " << solver_name << std::endl;
    std::cout << "       Species Count: " << specs.count() << std::endl;

    // Dispatch instantiates the EOS, flux, reconstruction, and time-integration
    // policies, then owns the complete simulation loop.
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
