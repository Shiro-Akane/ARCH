// Isolated Host scheduling candidate. No production registration or new math.
#pragma once
#include "CuDssSparseWaveSolver.h"

namespace arch::cuda::experimental {
struct SparseWindowStatistics {
    std::uint64_t executions = 0, native_pages = 0;
    std::uint64_t requested_factors = 0, requested_solves = 0;
    std::uint64_t evicted_factor_restores = 0, invalidated_factor_restores = 0, invalidations = 0;
};

// Distinct bounds: at most 128 logical ODE lanes, at most 32 native factor slots.
// All original matrices/RHS/solutions remain on the device. The underlying
// provider keeps its existing 256 MiB estimate budget and numerical acceptance.
// A valid logical reuse whose native slot was evicted is explicitly refactored;
// an unknown/stale logical token is an error, not an opportunity to repair it.
// As in the underlying provider, the caller must change its token whenever
// matrix coefficients change, even when the matrix allocation is reused.
class CuDssSparseWindowSolver {
public:
    CuDssSparseWindowSolver(int window_capacity, int native_capacity,
                           int extent, int nonzeros, const int* offsets,
                           const int* columns, cudaStream_t stream);
    ~CuDssSparseWindowSolver();
    CuDssSparseWindowSolver(const CuDssSparseWindowSolver&) = delete;
    CuDssSparseWindowSolver& operator=(const CuDssSparseWindowSolver&) = delete;
    // Exactly window_capacity entries, including Idle. The entire window is
    // validated before any native numeric operation (including cross-page aliases).
    CuDssResult execute(std::span<const SparseWaveTask> tasks);
    void invalidate() noexcept;
    // Native statistics retain all physical work, including padding and restores.
    SparseWaveStatistics statistics() const;
    SparseWindowStatistics logical_statistics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace arch::cuda::experimental
