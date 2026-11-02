/** @file OutputPath.h
 * Keep distributed rank output separate without changing scientific configuration.
 */
#pragma once
#include <string>
#if ARCH_MPI_ENABLED
#include <mpi.h>
#endif
namespace io
{
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
