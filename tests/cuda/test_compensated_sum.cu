/**
 * @file test_compensated_sum.cu
 * @brief Run compensated-sum reference cases through device memory.
 *
 * Upload shared fixtures, evaluate the production accumulator in a kernel
 * and compare downloaded results with independently specified exact values.
 */
#include "../math/CompensatedSumCases.h"

#include <cuda_runtime.h>

#include <array>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {

void checked(cudaError_t error)
{
    if (error != cudaSuccess)
        throw std::runtime_error(cudaGetErrorString(error));
}

__global__ void evaluate_cases(
    const arch::test::SumCase* samples, int count, double* results)
{
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < count)
        results[index] = arch::test::evaluate_sum_case(samples[index]);
}

} // namespace

int main()
{
    int devices = 0;
    const auto probe = cudaGetDeviceCount(&devices);
    if (probe == cudaErrorNoDevice || probe == cudaErrorInsufficientDriver
        || (probe == cudaSuccess && devices == 0))
        return 77;
    try {
        checked(probe);
        constexpr int count = std::size(arch::test::compensated_sum_cases);
        arch::test::SumCase* samples = nullptr;
        double* device_results = nullptr;
        std::array<double, count> results{};
        checked(cudaMalloc(&samples, sizeof(arch::test::compensated_sum_cases)));
        checked(cudaMalloc(&device_results, sizeof(results)));
        checked(cudaMemcpy(samples, arch::test::compensated_sum_cases,
                           sizeof(arch::test::compensated_sum_cases),
                           cudaMemcpyHostToDevice));
        evaluate_cases<<<1, 32>>>(samples, count, device_results);
        checked(cudaGetLastError());
        checked(cudaMemcpy(results.data(), device_results, sizeof(results),
                           cudaMemcpyDeviceToHost));
        checked(cudaFree(device_results));
        checked(cudaFree(samples));
        for (int index = 0; index < count; ++index) {
            const auto& sample = arch::test::compensated_sum_cases[index];
            const double host = arch::test::evaluate_runtime_sum_case(sample);
            if (host != sample.expected || results[index] != sample.expected) {
                std::cerr << std::setprecision(17) << "sum case " << index
                          << " expected=" << sample.expected
                          << " host=" << host << " device=" << results[index]
                          << '\n';
                return 1;
            }
        }
        std::cout << "CUDA_COMPENSATED_SUM_PASS cases=" << count << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
