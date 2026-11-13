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
#include "physics/boundary/UserBoundary.h"

// Runtime data required for startup reporting and dispatch.
#include "amr/topology/AmrDefines.h"
#include "data/GlobalDefs.h"
#include "physics/species/Species.h"

// Execution and logging services.
#include "driver/SolverDispatch.h"
#include "core/files/FileFingerprint.h"
#include <algorithm>
#include <array>

static int run_arch(int argc, char **argv)
{
#if ARCH_MPI_ENABLED
    int mpi_size = 1;
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);
    if (mpi_size > 1 && argc > 1 && arch::api::contract::find(argv[1])) {
        std::cerr << "Preview commands require a single MPI rank\n";
        return 1;
    }
#endif
    // Isolated application request: branch before logs, directories, backend
    // resolution or the simulation driver can acquire resources.
    if (argc > 1 && arch::api::contract::find(argv[1]))
        return arch::api::RunPreviewCommand(argc, argv);

    // The CLI contract has three entries: executable, problem name, and
    // parameter file. Therefore argc must be at least three.
    if (argc < 3) {
        std::cerr << "Usage: ./ARCH <ProblemType> <ParFile>" << std::endl;
        std::cerr << "Example: ./ARCH Sod runs/test1/arch.par" << std::endl;
        return 1;
    }

    // Preserve both arguments verbatim: the registry consumes the problem
    // name, while the configuration parser consumes the path.
    std::string problem_type = argv[1]; // For example, "Sod" or "Sedov".
    std::string par_file = argv[2];     // For example, "simulation/Sod/Sod.par".

    try {
        // Declared inputs are checked before constructing the model. Setup and
        // its errors stay inside this boundary, before scientific output/logs.
#if ARCH_MPI_ENABLED
        if (mpi_size > 1) {
            const auto identity =
                arch::core::string_sha256(problem_type + "\n" + arch::core::file_sha256(par_file)
                    + "\n" + arch::core::running_executable_sha256());
            std::array<char, 64> reference{};
            std::copy(identity.begin(), identity.end(), reference.begin());
            MPI_Bcast(reference.data(), 64, MPI_CHAR, 0, MPI_COMM_WORLD);
            int mismatch = !std::equal(identity.begin(), identity.end(), reference.begin());
            int any_mismatch = 0;
            MPI_Allreduce(&mismatch, &any_mismatch, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
            if (any_mismatch)
                throw std::invalid_argument(
                    "MPI ranks received different executable, problem or parameter contents");
        }
#endif
        SimConfig input = RuntimeParams::Load(par_file, problem_type);
        auto problem_ptr = ProblemRegistry::Get().Create(problem_type);
        if (!problem_ptr)
            throw std::invalid_argument("Unknown problem type: " + problem_type);
        SpeciesManager species;
        const auto prepared = problem_ptr->SetupChecked(input, species);
        const auto &config = prepared.config();
        const auto &specs = prepared.species();
        (void)arch::boundary::ResolveCaseBoundaries(problem_type, problem_ptr->SourceFile(),
                                                    config);

        std::cout << "[Main] Parameters loaded from: " << par_file << std::endl;
        std::cout << "[Main] Problem created: " << problem_type << std::endl;

        const std::string solver_name = config.numerics.solver_name;
        std::cout << "[Main] Configuration:" << std::endl;
        std::cout << "       Grid: " << config.grid.nblockx1 * amr::BLOCK_NX
                  << " cells, CFL: " << config.numerics.cfl << std::endl;
        std::cout << "       Solver: " << solver_name << std::endl;
        std::cout << "       Species Count: " << specs.count() << std::endl;
        DispatchSolver(*problem_ptr, prepared);
    } catch (const std::exception &e) {
        std::cerr << "[Fatal Error] " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
#if ARCH_MPI_ENABLED
    int provided = 0;
    if (MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided) != MPI_SUCCESS)
        return 1;
    MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_ARE_FATAL);
    if (provided < MPI_THREAD_FUNNELED) {
        std::cerr << "MPI implementation does not provide MPI_THREAD_FUNNELED\n";
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
    // All worker teams join before communication. On any rank's failure abort
    // the job, including peers that may be waiting in an exchange or reduction.
    const int result = run_arch(argc, argv);
    if (result != 0) {
        int size = 1;
        MPI_Comm_size(MPI_COMM_WORLD, &size);
        // A single rank has no waiting peers. Finalize normally so structured
        // CLI errors retain their buffered JSON and the requested exit code.
        if (size > 1)
            MPI_Abort(MPI_COMM_WORLD, result);
    }
    MPI_Finalize();
    return result;
#else
    return run_arch(argc, argv);
#endif
}
