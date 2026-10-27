#include "cuda/common/DeviceAllocation.h"
#include <cuda_runtime.h>
#include "fixtures/core/ArrayFillCases.h"
#include <iostream>
#include <stdexcept>

namespace {
void checked(cudaError_t error)
{
    if (error != cudaSuccess) throw std::runtime_error(cudaGetErrorString(error));
}

template<class T>
__global__ void fill_values(const arch::test::FillBits<T>* bits, unsigned* result)
{
    const unsigned index = threadIdx.x;
    if (index < 16) result[index] = arch::test::fill_pattern<T>(bits[index]);
    if (index == 16) result[index] = arch::test::fill_aliased_value();
}

template<class T> bool check_cases()
{
    const auto& bits = arch::test::ClassificationCases<T>::bits;
    arch::cuda::DeviceAllocation<arch::test::FillBits<T>> device_bits;
    arch::cuda::DeviceAllocation<unsigned> device_result;
    device_bits.allocate(bits.size());
    device_result.allocate(17);
    checked(cudaMemcpy(device_bits.get(), bits.data(), sizeof(bits), cudaMemcpyHostToDevice));
    checked(cudaMemset(device_result.get(), 0, 17 * sizeof(unsigned)));
    fill_values<T><<<1, 32>>>(device_bits.get(), device_result.get());
    checked(cudaGetLastError());
    checked(cudaDeviceSynchronize());
    std::array<unsigned, 17> result{};
    checked(cudaMemcpy(result.data(), device_result.get(), sizeof(result), cudaMemcpyDeviceToHost));
    for (std::size_t index = 0; index < result.size(); ++index) {
        if (result[index] != 1) {
            std::cerr << "device array fill mismatch size=" << sizeof(T)
                      << " pattern=" << index << '\n';
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
        std::cout << "CUDA_FIXED_ARRAY_FILL_PASS float=16 double=16 elements=448 alias=2\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
