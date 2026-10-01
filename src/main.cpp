/**
 * @file main.cpp
 * @brief Command-line entry point for the ARCH simulation program.
 *
 * The entry point assembles the top-level workflow and contains no numerical
 * method implementation:
 * 1. Read the problem name and parameter-file path.
 * 2. Check declared runtime and model inputs before constructing a problem.
 * 3. Prepare species and initial state within the common exception boundary.
 * 4. Validate preparation before creating output and logging resources.
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
#include "api/Preview.h"

// Application control and public problem interface.
#include "core/problem/ProblemRegistry.h"
#include "core/config/RuntimeParams.h"
#include "interface/ProblemGenerator.h"

// Runtime data required for startup reporting and dispatch.
#include "amr/topology/AmrDefines.h"
#include "data/GlobalDefs.h"
#include "physics/species/Species.h"

// Execution and logging services.
#include "driver/SolverDispatch.h"
#include "io/Logger.h"

int main(int argc, char **argv)
{
    // Isolated application request: branch before logs, directories, backend
    // resolution or the simulation driver can acquire resources.
    if (argc > 1 && arch::api::contract::find(argv[1]))
        return arch::api::RunPreviewCommand(argc, argv);

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

    try
    {
        // Declared inputs are checked before constructing the model. Setup and
        // its errors stay inside this boundary, before scientific output/logs.
        SimConfig input = RuntimeParams::Load(par_file, problem_type);
        auto problem_ptr = ProblemRegistry::Get().Create(problem_type);
        if (!problem_ptr)
            throw std::invalid_argument("Unknown problem type: " + problem_type);
        SpeciesManager species;
        const auto prepared = problem_ptr->SetupChecked(input, species);
        const auto& config = prepared.config();
        const auto& specs = prepared.species();

        std::filesystem::create_directories(config.io.out_dir);
        const std::string log_dir =
            config.Get<std::string>("log_dir", config.io.out_dir);
        std::filesystem::create_directories(log_dir);
        const std::string log_filename = log_dir + "/" + config.io.base_name + "_log.dat";
        Logger::Init(log_filename, config.io.restart);
        std::cout << "[Main] Console output is being recorded to log file: " << log_filename << std::endl;
        std::cout << "[Main] Parameters loaded from: " << par_file << std::endl;
        std::cout << "[Main] Problem created: " << problem_type << std::endl;

        const std::string solver_name = config.numerics.solver_name;
        std::cout << "[Main] Configuration:" << std::endl;
        std::cout << "       Grid: " << config.grid.nblockx1 * amr::BLOCK_NX << " cells, CFL: " << config.numerics.cfl << std::endl;
        std::cout << "       Solver: " << solver_name << std::endl;
        std::cout << "       Species Count: " << specs.count() << std::endl;
        DispatchSolver(*problem_ptr, prepared);
    }
    catch (const std::exception &e)
    {
        std::cerr << "[Fatal Error] " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
