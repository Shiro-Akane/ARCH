#pragma once
#include "core/MathClassification.h"
#include <array>
#include <cstdint>

namespace arch::test {
// finite=1, NaN=2, negative-sign=4. Expected values are fixed IEEE truths,
// independent of either implementation. Include quiet and signaling NaNs.
inline constexpr std::array<unsigned, 16> classification_expected = {
    1, 5, 1, 5, 1, 5, 1, 5, 1, 5, 0, 4, 2, 6, 2, 6};
template<class T> struct ClassificationCases;
template<> struct ClassificationCases<double> {
    inline static constexpr std::array<std::uint64_t, 16> bits = {
        0x0000000000000000ULL, 0x8000000000000000ULL,
        0x3ff0000000000000ULL, 0xbff0000000000000ULL,
        0x0000000000000001ULL, 0x8000000000000001ULL,
        0x0010000000000000ULL, 0x8010000000000000ULL,
        0x7fefffffffffffffULL, 0xffefffffffffffffULL,
        0x7ff0000000000000ULL, 0xfff0000000000000ULL,
        0x7ff8000000000001ULL, 0xfff8000000000001ULL,
        0x7ff0000000000001ULL, 0xfff0000000000001ULL};
};
template<> struct ClassificationCases<float> {
    inline static constexpr std::array<std::uint32_t, 16> bits = {
        0x00000000u, 0x80000000u, 0x3f800000u, 0xbf800000u,
        0x00000001u, 0x80000001u, 0x00800000u, 0x80800000u,
        0x7f7fffffu, 0xff7fffffu, 0x7f800000u, 0xff800000u,
        0x7fc00001u, 0xffc00001u, 0x7f800001u, 0xff800001u};
};
template<class T> ARCH_HOST_DEVICE inline unsigned classify(T value)
{
    return (arch::math::isfinite(value) ? 1u : 0u)
        | (arch::math::isnan(value) ? 2u : 0u)
        | (arch::math::signbit(value) ? 4u : 0u);
}
} // namespace arch::test
