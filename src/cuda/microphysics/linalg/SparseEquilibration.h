/**
 * @file SparseEquilibration.h
 * @brief CUDA launch interface for provider-owned matrix/vector scaling.
 *
 * Buffers and status are borrowed; LinearEquilibration owns the arithmetic.
 * The caller checks stream completion and the invalid latch before use.
 * Workflow:
 * 1. Receive compact or sparse ODE linear systems.
 * 2. Prepare cuDSS or equilibration storage for repeated solves.
 * 3. Return a checked solution through the shared ODE contract.
 */
#pragma once
#include <cuda_runtime_api.h>

namespace arch::cuda {
cudaError_t equilibrate_sparse_rows(int extent, const int* offsets,
    const double* values, double* divisors, double* scaled_values,
    int* invalid, cudaStream_t stream);
cudaError_t equilibrate_sparse_columns(int extent, const int* offsets, const int* slots,
    double* values, double* divisors, int* invalid, cudaStream_t stream);
cudaError_t equilibrate_sparse_vector(int extent, const double* values,
    const double* divisors, double* scaled_values, int* invalid,
    bool clear_invalid, cudaStream_t stream);
cudaError_t original_sparse_residual(int extent, const int* offsets, const int* columns,
    const double* values, const double* rhs, const double* solution,
    double* residual, int* residual_state, cudaStream_t stream);
cudaError_t accumulate_sparse_correction(int extent, const double* correction,
    double* solution, int* invalid, cudaStream_t stream);
} // namespace arch::cuda
