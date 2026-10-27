#pragma once
#include "core/ArrayFill.h"
#include "math/ClassificationCases.h"
#include <bit>
#include <cstdint>
#include <type_traits>

namespace arch::test {
template<class T>
using FillBits = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;

template<class T> ARCH_HOST_DEVICE T fill_value(FillBits<T> bits)
{
#if defined(__CUDA_ARCH__)
    if constexpr (sizeof(T) == 4) return __uint_as_float(bits);
    else return __longlong_as_double(static_cast<long long>(bits));
#else
    return std::bit_cast<T>(bits);
#endif
}

template<class T> ARCH_HOST_DEVICE FillBits<T> fill_bits(T value)
{
#if defined(__CUDA_ARCH__)
    if constexpr (sizeof(T) == 4) return __float_as_uint(value);
    else return static_cast<FillBits<T>>(__double_as_longlong(value));
#else
    return std::bit_cast<FillBits<T>>(value);
#endif
}

template<class T, std::size_t N>
ARCH_HOST_DEVICE bool fill_extent(FillBits<T> expected)
{
    std::array<T, N> values{};
    arch::fill_array(values, fill_value<T>(expected));
    // Fixed input bits are the oracle; no second fill constructs the expected data.
    for (std::size_t index = 0; index < N; ++index)
        if (fill_bits(values[index]) != expected) return false;
    return true;
}

template<class T> ARCH_HOST_DEVICE bool fill_pattern(FillBits<T> expected)
{
    return fill_extent<T, 2>(expected) && fill_extent<T, 3>(expected)
        && fill_extent<T, 9>(expected);
}

ARCH_HOST_DEVICE inline bool fill_aliased_value()
{
    std::array<double, 9> values{1., 2., 3., 4., 5., 6., 7., 8., 9.};
    arch::fill_array(values, values[4]);
    for (std::size_t index = 0; index < values.size(); ++index)
        if (values[index] != 5.) return false;
    return true;
}
} // namespace arch::test
