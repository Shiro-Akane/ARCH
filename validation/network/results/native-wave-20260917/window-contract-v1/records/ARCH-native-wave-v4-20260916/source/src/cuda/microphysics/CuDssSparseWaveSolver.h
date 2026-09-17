// Experimental execution-only candidate. Not registered in production CMake.
#pragma once
#include "cuda/microphysics/CuDssSparseSolver.h"
#include <cstdint>
#include <memory>
#include <span>

namespace arch::cuda::experimental {
enum class SparseWaveOperation { Idle, Factorize, SolveWithFactors, FactorizeAndSolve };
struct SparseWaveTask {
    SparseWaveOperation operation = SparseWaveOperation::Idle;
    const double* values = nullptr;
    const double* rhs = nullptr;
    double* solution = nullptr;
    std::uint64_t token = 0;
};
struct SparseWaveStatistics {
    std::uint64_t analyses = 0, native_factor_calls = 0, native_solve_calls = 0;
    // Includes inactive/padded systems: never report only useful lane work.
    std::uint64_t native_factor_systems = 0, native_solve_systems = 0;
    std::uint64_t requested_factors = 0, requested_solves = 0;
    std::uint64_t kernels = 0, synchronizations = 0, bytes_h2d = 0, bytes_d2h = 0;
    std::uint64_t estimated_peak_device_bytes = 0;
};

// A fixed, bounded cohort (1..32), one immutable CSR pattern, one stream.
// CPU scheduling only: all original matrices, RHS and solutions stay on GPU.
// The cohort batches native operations, not ODE decisions. Any changed factor
// refactorizes the whole cohort; its extra work is exposed in the counters.
// Idle/Factorize-only output buffers are not written. Private identity matrices
// and zero RHS initialize unused lanes, never the physical state or equations.
// A returned native success still requires the shared original-matrix residual
// acceptance in SparseOdeBatch::validate_corrections before ODE continuation.
class CuDssSparseWaveSolver {
public:
    CuDssSparseWaveSolver(int capacity, int extent, int nonzeros,
                         const int* offsets, const int* columns, cudaStream_t stream);
    ~CuDssSparseWaveSolver();
    CuDssSparseWaveSolver(const CuDssSparseWaveSolver&) = delete;
    CuDssSparseWaveSolver& operator=(const CuDssSparseWaveSolver&) = delete;
    // Exactly capacity entries, including Idle entries. Tokens plus original
    // matrix addresses identify lane factors; a pool reuse needs a new token.
    CuDssResult execute(std::span<const SparseWaveTask> tasks);
    // External residual rejection invalidates all native factors, but retains
    // known address/token pairs so a later valid reuse request can restore them.
    void invalidate() noexcept;
    SparseWaveStatistics statistics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace arch::cuda::experimental
