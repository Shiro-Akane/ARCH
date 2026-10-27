/** IEEE classification across host C++ and the native Windows CUDA frontend. */
#pragma once
#include "core/ArchPortability.h"
#include <cmath>
#if defined(__CUDACC__) && defined(_WIN32)
#include <cuda/std/cmath>
#endif

namespace arch::math {
// Ordinary CPU and Linux CUDA retain the original standard-library calls.
// Only Windows device compilation needs the CUDA standard-library overloads.
template<class T> ARCH_HOST_DEVICE inline bool isfinite(T value) noexcept
{
#if defined(__CUDA_ARCH__) && defined(_WIN32)
    return ::cuda::std::isfinite(value);
#else
    return std::isfinite(value);
#endif
}
template<class T> ARCH_HOST_DEVICE inline bool isnan(T value) noexcept
{
#if defined(__CUDA_ARCH__) && defined(_WIN32)
    return ::cuda::std::isnan(value);
#else
    return std::isnan(value);
#endif
}
template<class T> ARCH_HOST_DEVICE inline bool signbit(T value) noexcept
{
#if defined(__CUDA_ARCH__) && defined(_WIN32)
    return ::cuda::std::signbit(value);
#else
    return std::signbit(value);
#endif
}
} // namespace arch::math
