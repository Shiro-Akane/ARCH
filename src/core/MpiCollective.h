/**
 * @file MpiCollective.h
 * @brief Existing WORLD thread/error contract shared by topology and flux consumers.
 * Workflow: require the initializing thread, complete local preparation together,
 * then call the original transport. MPI transport failures abort the job.
 */
#pragma once
#include <exception>
#include <stdexcept>
#include <utility>
#if ARCH_MPI_ENABLED
#include <mpi.h>
namespace arch::mpi {
inline void check(int code) {
    if (code != MPI_SUCCESS) {
        MPI_Abort(MPI_COMM_WORLD, code);
        throw std::runtime_error("MPI transport failed");
    }
}
inline std::pair<int, int> require_world() {
    int initialized = 0, finalized = 0, main = 0, level = 0, rank = 0, size = 0;
    MPI_Initialized(&initialized);
    if (initialized) MPI_Finalized(&finalized);
    if (!initialized || finalized)
        throw std::logic_error("MPI execution is not initialized");
    check(MPI_Is_thread_main(&main));
    check(MPI_Query_thread(&level));
    if (!main || level < MPI_THREAD_FUNNELED) {
        MPI_Abort(MPI_COMM_WORLD, 1);
        throw std::logic_error("MPI execution requires the initializing thread");
    }
    check(MPI_Comm_rank(MPI_COMM_WORLD, &rank));
    check(MPI_Comm_size(MPI_COMM_WORLD, &size));
    return {rank, size};
}
template <class Action> inline void prepare(Action&& action) {
    std::exception_ptr error;
    try {
        action();
    } catch (...) {
        error = std::current_exception();
    }
    int failed = error ? 1 : 0, any = 0;
    check(MPI_Allreduce(&failed, &any, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD));
    if (any) {
        if (error)
            std::rethrow_exception(error);
        throw std::runtime_error("MPI peer rejected preparation");
    }
}
} // namespace arch::mpi
#endif
