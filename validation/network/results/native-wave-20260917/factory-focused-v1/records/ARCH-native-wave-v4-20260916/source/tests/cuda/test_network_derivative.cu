/**
 * @file test_network_derivative.cu
 * @brief Evaluate analytic network-derivative fixtures on CUDA.
 *
 * Reuse the host test cases to check the complete temperature derivative,
 * including its energy component and invalid-input behavior.
 */
#include "../math/NetworkDerivativeCases.h"
#include <cuda_runtime.h>
#include <iostream>

__global__ void check_derivative(bool* passed) {
    *passed = arch::test::network_derivative_contract();
}
int main() {
    int count = 0;
    const auto probe = cudaGetDeviceCount(&count);
    if (probe == cudaErrorNoDevice || probe == cudaErrorInsufficientDriver
        || (probe == cudaSuccess && count == 0)) return 77;
    if (probe != cudaSuccess) return 1;
    bool* device = nullptr;
    if (cudaMalloc(&device, sizeof(bool)) != cudaSuccess) return 1;
    check_derivative<<<1, 1>>>(device);
    bool gpu = false;
    const auto launch = cudaGetLastError();
    const auto copy = cudaMemcpy(&gpu, device, sizeof(bool), cudaMemcpyDeviceToHost);
    const auto release = cudaFree(device);
    const bool host = arch::test::network_derivative_contract();
    std::cout << "NETWORK_DERIVATIVE host=" << host << " cuda=" << gpu << '\n';
    return launch == cudaSuccess && copy == cudaSuccess && release == cudaSuccess
        && host && gpu ? 0 : 1;
}
