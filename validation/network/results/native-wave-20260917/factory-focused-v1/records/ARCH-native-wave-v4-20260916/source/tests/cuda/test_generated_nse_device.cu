/** Execute the same independent generated-NSE witnesses on host and device. */
#include "../math/GeneratedNseCases.h"
#include <cuda_runtime.h>
#include <iostream>
#include <stdexcept>

namespace {
using arch::test::generated_nse::Results;
void checked(cudaError_t result)
{
    if (result != cudaSuccess) throw std::runtime_error(cudaGetErrorString(result));
}
__global__ void evaluate(Results* result)
{
    *result = arch::test::generated_nse::run();
}
}

int main()
{
    Results* device = nullptr;
    try {
        const auto host = arch::test::generated_nse::run();
        checked(cudaMalloc(&device, sizeof(Results)));
        evaluate<<<1, 1>>>(device);
        checked(cudaGetLastError());
        Results actual{};
        checked(cudaMemcpy(&actual, device, sizeof(Results), cudaMemcpyDeviceToHost));
        checked(cudaFree(device));
        device = nullptr;
        if (!host.passed() || !actual.passed())
            throw std::runtime_error("generated NSE independent host/device checks failed");
        std::cout << "generated NSE: independent host and CUDA checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (device) cudaFree(device);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
