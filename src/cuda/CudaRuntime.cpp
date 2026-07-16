#include "CudaRuntime.h"

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
    return false;
}

CudaRuntimeInfo probe_cuda_runtime(int device_index) noexcept
{
    CudaRuntimeInfo info;
    info.device_index = device_index;
    std::snprintf(info.diagnostic.data(), info.diagnostic.size(),
                  "CUDA support is not compiled into this binary");
    return info;
}

} // namespace arch::cuda
