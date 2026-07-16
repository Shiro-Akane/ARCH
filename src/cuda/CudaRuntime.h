#pragma once

#include <array>
#include <string_view>

namespace arch::cuda {

/**
 * @brief Non-throwing result of probing the CUDA runtime and one device.
 *
 * Fixed-size character storage keeps probe_cuda_runtime() allocation-free, so
 * it can safely report missing drivers and devices during early startup.
 */
struct CudaRuntimeInfo
{
    bool compiled = false;
    bool available = false;
    int device_count = 0;
    int device_index = -1;
    int compute_capability_major = 0;
    int compute_capability_minor = 0;
    std::array<char, 256> device_name{};
    std::array<char, 256> diagnostic{};

    std::string_view name() const noexcept;
    std::string_view message() const noexcept;
};

/** @brief True only when this binary was built with ARCH_ENABLE_CUDA=ON. */
bool cuda_backend_compiled() noexcept;

/**
 * @brief Query the CUDA runtime without throwing or silently choosing CPU.
 *
 * The function does not change the active CUDA device.  An explicit CUDA
 * request should be rejected by the caller when available is false.  Auto may
 * use the same result to make an explicit, logged backend choice.
 */
CudaRuntimeInfo probe_cuda_runtime(int device_index = 0) noexcept;

} // namespace arch::cuda
