// Real backend namespaces must already be visible when classification is read.
#include "cuda/common/DeviceAllocation.h"
#include "math/ClassificationCases.h"
#include <cuda_runtime.h>
#include <bit>
#include <iostream>
#include <stdexcept>

namespace {
void checked(cudaError_t error)
{
    if (error != cudaSuccess) throw std::runtime_error(cudaGetErrorString(error));
}
template<class T> __global__ void classify_values(const T* input, unsigned* result)
{
    const int i = threadIdx.x;
    if (i < 16) result[i] = arch::test::classify(input[i]);
}
template<class T> bool check_cases()
{
    std::array<T, 16> input{};
    const auto& bits = arch::test::ClassificationCases<T>::bits;
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = std::bit_cast<T>(bits[i]);
    arch::cuda::DeviceAllocation<T> device_input;
    arch::cuda::DeviceAllocation<unsigned> device_result;
    device_input.allocate(input.size());
    device_result.allocate(input.size());
    checked(cudaMemcpy(device_input.get(), input.data(), sizeof(input), cudaMemcpyHostToDevice));
    classify_values<<<1, 32>>>(device_input.get(), device_result.get());
    checked(cudaGetLastError());
    checked(cudaDeviceSynchronize());
    std::array<unsigned, 16> actual{};
    checked(cudaMemcpy(actual.data(), device_result.get(), sizeof(actual), cudaMemcpyDeviceToHost));
    for (std::size_t i = 0; i < input.size(); ++i) {
        const auto expected = arch::test::classification_expected[i];
        if (actual[i] != expected || arch::test::classify(input[i]) != expected) {
            std::cerr << "device classification mismatch size=" << sizeof(T)
                      << " case=" << i << " expected=" << expected
                      << " actual=" << actual[i] << '\n';
            return false;
        }
    }
    return true;
}
} // namespace
int main()
{
    int count = 0;
    const auto probe = cudaGetDeviceCount(&count);
    if (probe == cudaErrorNoDevice || probe == cudaErrorInsufficientDriver
        || (probe == cudaSuccess && count == 0)) return 77;
    try {
        checked(probe);
        if (!check_cases<float>() || !check_cases<double>()) return 1;
        std::cout << "CUDA_MATH_CLASSIFICATION_PASS float=16 double=16 host-and-device\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
