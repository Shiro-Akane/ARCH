#include "CudaRuntime.h"

#include <cuda_runtime_api.h>

#include <cstdio>

namespace arch::cuda {
namespace {

std::string_view bounded_view(const std::array<char, 256>& text) noexcept
{
    std::size_t length = 0;
    while (length < text.size() && text[length] != '\0') {
        ++length;
    }
    return {text.data(), length};
}

void set_cuda_error(CudaRuntimeInfo& info, const char* operation,
                    cudaError_t error) noexcept
{
    const char* detail = cudaGetErrorString(error);
    std::snprintf(info.diagnostic.data(), info.diagnostic.size(), "%s: %s",
                  operation, detail != nullptr ? detail : "unknown CUDA error");
}

} // namespace

std::string_view CudaRuntimeInfo::name() const noexcept
{
    return bounded_view(device_name);
}

std::string_view CudaRuntimeInfo::message() const noexcept
{
    return bounded_view(diagnostic);
}

bool cuda_backend_compiled() noexcept
{
    return true;
}

CudaRuntimeInfo probe_cuda_runtime(int device_index) noexcept
{
    CudaRuntimeInfo info;
    info.compiled = true;
    info.device_index = device_index;

    cudaError_t error = cudaGetDeviceCount(&info.device_count);
    if (error != cudaSuccess) {
        set_cuda_error(info, "cudaGetDeviceCount failed", error);
        return info;
    }
    if (info.device_count == 0) {
        std::snprintf(info.diagnostic.data(), info.diagnostic.size(),
                      "CUDA runtime reported no devices");
        return info;
    }
    if (device_index < 0 || device_index >= info.device_count) {
        std::snprintf(info.diagnostic.data(), info.diagnostic.size(),
                      "CUDA device index %d is outside [0, %d)", device_index,
                      info.device_count);
        return info;
    }

    cudaDeviceProp properties{};
    error = cudaGetDeviceProperties(&properties, device_index);
    if (error != cudaSuccess) {
        set_cuda_error(info, "cudaGetDeviceProperties failed", error);
        return info;
    }

    info.compute_capability_major = properties.major;
    info.compute_capability_minor = properties.minor;
    std::snprintf(info.device_name.data(), info.device_name.size(), "%s",
                  properties.name);
    info.available = true;
    std::snprintf(info.diagnostic.data(), info.diagnostic.size(),
                  "CUDA device %d is available", device_index);
    return info;
}

} // namespace arch::cuda
