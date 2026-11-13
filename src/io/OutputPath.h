/** @file OutputPath.h
 * Keep distributed rank output separate without changing scientific configuration.
 */
#pragma once
#include <string>
#include <iomanip>
#include <sstream>
#if ARCH_MPI_ENABLED
#include <mpi.h>
#endif
namespace io
{
/** Preserve the original native plot filename in serial and shard writers. */
inline std::string plot_filename(const std::string& base_name,
    const std::string& solver_name,int index)
{
    std::ostringstream name;
    name << base_name << "_" << solver_name << "_plt_"
         << std::setw(4) << std::setfill('0') << index << ".h5";
    return name.str();
}
inline std::string output_directory(const std::string &directory)
{
#if ARCH_MPI_ENABLED
    int initialized = 0, finished = 0, rank = 0, size = 1;
    MPI_Initialized(&initialized);
    if (initialized) {
        MPI_Finalized(&finished);
        if (!finished) {
            MPI_Comm_size(MPI_COMM_WORLD, &size);
            if (size > 1) {
                MPI_Comm_rank(MPI_COMM_WORLD, &rank);
                return directory + "/rank-" + std::to_string(rank);
            }
        }
    }
#endif
    return directory;
}
} // namespace io
