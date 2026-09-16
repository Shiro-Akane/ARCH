// Execution-only candidate: bounded descriptors passed by value at launch.
#pragma once
#include <cuda_runtime_api.h>
#include <type_traits>

namespace arch::cuda::experimental {
struct WaveKernelLane {
    const double* values = nullptr;
    const double* rhs = nullptr;
    double* solution = nullptr;
    unsigned selected = 0;
};
struct WaveKernelBatch {
    int capacity = 0, extent = 0, nonzeros = 0;
    const int* offsets = nullptr;
    const int* columns = nullptr;
    const int* column_offsets = nullptr;
    const int* column_slots = nullptr;
    double* row_divisors = nullptr;
    double* column_divisors = nullptr;
    double* scaled_values = nullptr;
    double* scaled_rhs = nullptr;
    double* scaled_solution = nullptr;
    double* residual = nullptr;
    double* correction = nullptr;
    int* statuses = nullptr;
    WaveKernelLane lanes[32]{};
};
static_assert(std::is_trivially_copyable_v<WaveKernelBatch>);
// Stay within the older 4 KiB kernel argument limit, including scalar arguments.
static_assert(sizeof(WaveKernelBatch) <= 3072);

cudaError_t wave_normalize_rows(WaveKernelBatch batch, cudaStream_t stream);
cudaError_t wave_normalize_columns(WaveKernelBatch batch, cudaStream_t stream);
cudaError_t wave_normalize_rhs(WaveKernelBatch batch, bool correction, cudaStream_t stream);
cudaError_t wave_denormalize_solution(WaveKernelBatch batch, bool correction, cudaStream_t stream);
cudaError_t wave_original_residual(WaveKernelBatch batch, cudaStream_t stream);
} // namespace arch::cuda::experimental
