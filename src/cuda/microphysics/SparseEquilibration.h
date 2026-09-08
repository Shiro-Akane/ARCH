/**
 * @file SparseEquilibration.h
 * @brief CUDA launch interface for provider-owned matrix/vector scaling.
 *
 * Buffers and status are borrowed; LinearEquilibration owns the arithmetic.
 * The caller checks stream completion and the invalid latch before use.
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
} // namespace arch::cuda
