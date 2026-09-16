// One CTA per independent matrix; scalar arithmetic is the shared production math.
#include "SparseWaveKernels.h"
#include "numerics/linalg/LinearEquilibration.h"
#include "numerics/linalg/SparseResidual.h"
#include <cstddef>

namespace arch::cuda::experimental {
namespace {
constexpr unsigned threads = 256;

__global__ void normalize_rows(WaveKernelBatch b) {
    const auto lane = static_cast<std::size_t>(blockIdx.x);
    if (!b.lanes[lane].selected) return; // Uniform across this entire CTA.
    int* invalid = b.statuses + 2 * lane;
    if (threadIdx.x == 0) *invalid = 0;
    __syncthreads(); // Exactly one CTA owns the matrix and its invalid latch.
    const double* values = b.lanes[lane].values;
    double* scaled = b.scaled_values + lane * b.nonzeros;
    double* divisors = b.row_divisors + lane * b.extent;
    for (std::size_t row = threadIdx.x; row < static_cast<std::size_t>(b.extent); row += blockDim.x) {
        const auto divisor = linalg::equilibration_divisor(values, b.offsets[row], b.offsets[row + 1]);
        divisors[row] = divisor;
        for (int slot = b.offsets[row]; slot < b.offsets[row + 1]; ++slot)
            if (!linalg::equilibrated_value(values[slot], divisor, scaled[slot])) atomicExch(invalid, 1);
    }
}

__global__ void normalize_columns(WaveKernelBatch b) {
    const auto lane = static_cast<std::size_t>(blockIdx.x);
    if (!b.lanes[lane].selected) return;
    double* values = b.scaled_values + lane * b.nonzeros;
    double* divisors = b.column_divisors + lane * b.extent;
    int* invalid = b.statuses + 2 * lane;
    for (std::size_t column = threadIdx.x; column < static_cast<std::size_t>(b.extent); column += blockDim.x) {
        const auto divisor = linalg::equilibration_divisor(values,
            b.column_offsets[column], b.column_offsets[column + 1], b.column_slots);
        divisors[column] = divisor;
        for (int index = b.column_offsets[column]; index < b.column_offsets[column + 1]; ++index) {
            const int slot = b.column_slots[index];
            if (!linalg::equilibrated_value(values[slot], divisor, values[slot])) atomicExch(invalid, 1);
        }
    }
}

__global__ void normalize_rhs(WaveKernelBatch b, bool correction) {
    const auto lane = static_cast<std::size_t>(blockIdx.x);
    double* output = b.scaled_rhs + lane * b.extent;
    if (!b.lanes[lane].selected) {
        for (std::size_t row = threadIdx.x; row < static_cast<std::size_t>(b.extent); row += blockDim.x)
            output[row] = 0.0; // Private inactive RHS only, never a physical output.
        return;
    }
    int* invalid = b.statuses + 2 * lane;
    if (threadIdx.x == 0) *invalid = 0;
    __syncthreads();
    const double* input = correction ? b.residual + lane * b.extent : b.lanes[lane].rhs;
    const double* divisors = b.row_divisors + lane * b.extent;
    for (std::size_t row = threadIdx.x; row < static_cast<std::size_t>(b.extent); row += blockDim.x)
        if (!linalg::equilibrated_value(input[row], divisors[row], output[row])) atomicExch(invalid, 1);
}

__global__ void denormalize_solution(WaveKernelBatch b, bool correction) {
    const auto lane = static_cast<std::size_t>(blockIdx.x);
    if (!b.lanes[lane].selected) return;
    const double* input = b.scaled_solution + lane * b.extent;
    const double* divisors = b.column_divisors + lane * b.extent;
    double* output = correction ? b.correction + lane * b.extent : b.lanes[lane].solution;
    int* invalid = b.statuses + 2 * lane;
    for (std::size_t row = threadIdx.x; row < static_cast<std::size_t>(b.extent); row += blockDim.x)
        if (!linalg::equilibrated_value(input[row], divisors[row], output[row])) atomicExch(invalid, 1);
}

__global__ void original_residual(WaveKernelBatch b) {
    const auto lane = static_cast<std::size_t>(blockIdx.x);
    if (!b.lanes[lane].selected) return;
    int* state = b.statuses + 2 * lane + 1;
    if (threadIdx.x == 0) *state = 0;
    __syncthreads();
    double* residual = b.residual + lane * b.extent;
    for (std::size_t row = threadIdx.x; row < static_cast<std::size_t>(b.extent); row += blockDim.x) {
        const auto result = linalg::sparse_residual_row(b.extent, b.offsets[row], b.offsets[row + 1],
            b.columns, b.lanes[lane].values, b.lanes[lane].solution, b.lanes[lane].rhs[row]);
        residual[row] = result.correction_rhs;
        atomicMax(state, static_cast<int>(result.state));
    }
}

template<class Kernel, class... Args>
cudaError_t launch(Kernel kernel, WaveKernelBatch batch, cudaStream_t stream, Args... args) {
    if (batch.capacity < 1 || batch.capacity > 32 || batch.extent < 1
            || batch.nonzeros < batch.extent || batch.statuses == nullptr) return cudaErrorInvalidValue;
    kernel<<<batch.capacity, threads, 0, stream>>>(batch, args...);
    return cudaGetLastError();
}
} // namespace

cudaError_t wave_normalize_rows(WaveKernelBatch b, cudaStream_t stream) {
    return launch(normalize_rows, b, stream);
}
cudaError_t wave_normalize_columns(WaveKernelBatch b, cudaStream_t stream) {
    return launch(normalize_columns, b, stream);
}
cudaError_t wave_normalize_rhs(WaveKernelBatch b, bool correction, cudaStream_t stream) {
    return launch(normalize_rhs, b, stream, correction);
}
cudaError_t wave_denormalize_solution(WaveKernelBatch b, bool correction, cudaStream_t stream) {
    return launch(denormalize_solution, b, stream, correction);
}
cudaError_t wave_original_residual(WaveKernelBatch b, cudaStream_t stream) {
    return launch(original_residual, b, stream);
}
cudaError_t wave_kernel_attributes(cudaFuncAttributes (&attributes)[5]) {
    const void* functions[] = {reinterpret_cast<const void*>(normalize_rows),
        reinterpret_cast<const void*>(normalize_columns), reinterpret_cast<const void*>(normalize_rhs),
        reinterpret_cast<const void*>(denormalize_solution), reinterpret_cast<const void*>(original_residual)};
    for (int i = 0; i < 5; ++i) {
        const auto status = cudaFuncGetAttributes(&attributes[i], functions[i]);
        if (status != cudaSuccess) return status;
    }
    return cudaSuccess;
}
} // namespace arch::cuda::experimental
