/**
 * @brief Host-invoked CUDA sparse execution policy; no ODE or network physics.
 */
#pragma once

#include <cuda_runtime_api.h>
#include <cstdint>
#include <memory>

namespace arch::cuda {
struct CuDssResult
{
    int library_status = 0;
    int device_info = 0; // Opaque CUDSS_DATA_INFO, not a documented singularity code.
    cudaError_t cuda_status = cudaSuccess;
    bool success() const
    {
        return library_status == 0 && device_info == 0 && cuda_status == cudaSuccess;
    }
    void require_success() const;
};

/**
 * One fixed CSR pattern and one live set of numerical factors. All coefficient,
 * RHS, and solution inputs/outputs are caller-owned device allocations; private
 * equilibrated buffers never overwrite the original matrix/RHS. The owner must
 * outlive this object and preserve its immutable pattern. Construction copies
 * only sparse metadata for validation, never network/ODE state for CPU work.
 *
 * Calls check both host cuDSS status and stream-completed CUDSS_DATA_INFO. They
 * therefore provide completed responses for a shared ODE continuation, not an
 * optimistic "submitted" success. Future batched scheduling can reduce fences
 * without changing either the ODE or this factor-lifetime contract.
 */
class CuDssSparseSolver
{
public:
    CuDssSparseSolver(int extent, int nonzeros, const int* row_offsets,
                     const int* column_indices, const double* values,
                     const double* rhs, double* solution, cudaStream_t stream);
    ~CuDssSparseSolver();
    CuDssSparseSolver(const CuDssSparseSolver&) = delete;
    CuDssSparseSolver& operator=(const CuDssSparseSolver&) = delete;

    // Matrix tokens identify (cell, matrix generation), not merely pointer
    // addresses reused by a pool. A failed factorization invalidates old factors.
    CuDssResult factorize(const double* values, std::uint64_t matrix_token);
    CuDssResult solve(const double* rhs, double* solution, std::uint64_t matrix_token);
    CuDssResult factorize_and_solve(const double* values, const double* rhs,
                                  double* solution, std::uint64_t matrix_token);
    // A caller's original-matrix residual may reject a library-successful
    // solve. Drop factors and native analysis state on the next factorization;
    // a failed native cache must not poison a later ODE retry or cell.
    void invalidate();
    std::uint64_t analysis_count() const;
    std::uint64_t synchronization_count() const;
    // Explicit ARCH launches/transfers only; vendor-internal work is opaque.
    std::uint64_t kernel_count() const;
    std::uint64_t bytes_d2h() const;
    std::uint64_t bytes_h2d() const;
    std::uint64_t peak_device_bytes() const;
    static int compiled_version();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace arch::cuda
