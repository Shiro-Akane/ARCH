/**
 * @file SparseEquilibration.cu
 * @brief Apply shared matrix/vector scaling helpers to device CSR storage.
 *
 * Row, column and vector kernels call LinearEquilibration for the arithmetic.
 * The provider supplies original inputs, private scaled outputs and an invalid
 * latch, and owns the stream fence before consuming the result.
 */

#include "SparseEquilibration.h"
#include "numerics/linalg/LinearEquilibration.h"
#include <algorithm>
#include <cstddef>

namespace arch::cuda {
namespace {
__global__ void normalize_rows(int extent, const int* offsets,
    const double* values, double* divisors, double* scaled_values, int* invalid)
{
    const auto lane = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const auto stride = static_cast<std::size_t>(gridDim.x) * blockDim.x;
    for (auto row = lane; row < static_cast<std::size_t>(extent); row += stride) {
        const auto divisor = linalg::equilibration_divisor(
            values, offsets[row], offsets[row + 1]);
        divisors[row] = divisor;
        for (int slot = offsets[row]; slot < offsets[row + 1]; ++slot)
            if (!linalg::equilibrated_value(values[slot], divisor, scaled_values[slot]))
                atomicExch(invalid, 1);
    }
}

__global__ void normalize_columns(int extent, const int* offsets, const int* slots,
    double* values, double* divisors, int* invalid)
{
    const auto lane = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const auto stride = static_cast<std::size_t>(gridDim.x) * blockDim.x;
    for (auto column = lane; column < static_cast<std::size_t>(extent); column += stride) {
        const auto divisor = linalg::equilibration_divisor(
            values, offsets[column], offsets[column + 1], slots);
        divisors[column] = divisor;
        // Each numeric entry has exactly one column owner. The preceding row
        // kernel completes on this stream before these disjoint in-place writes.
        for (int index = offsets[column]; index < offsets[column + 1]; ++index) {
            const int slot = slots[index];
            if (!linalg::equilibrated_value(values[slot], divisor, values[slot]))
                atomicExch(invalid, 1);
        }
    }
}

__global__ void normalize_vector(int extent, const double* rhs,
    const double* divisors, double* scaled_rhs, int* invalid)
{
    const auto lane = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const auto stride = static_cast<std::size_t>(gridDim.x) * blockDim.x;
    for (auto row = lane; row < static_cast<std::size_t>(extent); row += stride)
        if (!linalg::equilibrated_value(rhs[row], divisors[row], scaled_rhs[row]))
            atomicExch(invalid, 1);
}

template<class Kernel, class... Args>
cudaError_t launch(Kernel kernel, int extent, int* invalid,
    bool clear_invalid, cudaStream_t stream, Args... args)
{
    if (extent <= 0 || invalid == nullptr) return cudaErrorInvalidValue;
    int minimum_grid = 0, threads = 0;
    auto error = cudaOccupancyMaxPotentialBlockSize(&minimum_grid, &threads, kernel);
    if (error != cudaSuccess) return error;
    if (minimum_grid <= 0 || threads <= 0) return cudaErrorInvalidConfiguration;
    const int blocks = std::min((extent - 1) / threads + 1, minimum_grid);
    if (clear_invalid) {
        error = cudaMemsetAsync(invalid, 0, sizeof(int), stream);
        if (error != cudaSuccess) return error;
    }
    kernel<<<blocks, threads, 0, stream>>>(extent, args..., invalid);
    return cudaGetLastError();
}
} // namespace

cudaError_t equilibrate_sparse_rows(int extent, const int* offsets,
    const double* values, double* divisors, double* scaled_values,
    int* invalid, cudaStream_t stream)
{
    return launch(normalize_rows, extent, invalid, true, stream,
                  offsets, values, divisors, scaled_values);
}

cudaError_t equilibrate_sparse_columns(int extent, const int* offsets, const int* slots,
    double* values, double* divisors, int* invalid, cudaStream_t stream)
{
    return launch(normalize_columns, extent, invalid, false, stream,
                  offsets, slots, values, divisors);
}

cudaError_t equilibrate_sparse_vector(int extent, const double* values,
    const double* divisors, double* scaled_values, int* invalid,
    bool clear_invalid, cudaStream_t stream)
{
    return launch(normalize_vector, extent, invalid, clear_invalid, stream,
                  values, divisors, scaled_values);
}
} // namespace arch::cuda
