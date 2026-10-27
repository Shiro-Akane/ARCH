/** Assignment-only fill for fixed Host/Device buffers. */
#pragma once
#include <array>
#include <cstddef>
#include "core/ArchPortability.h"

namespace arch {
// MSVC's constexpr array::fill calls non-device runtime helpers. Explicit
// assignments preserve value semantics without changing the array's type/ABI.
template<class T, std::size_t N>
ARCH_HOST_DEVICE constexpr void fill_array(std::array<T, N>& values, const T& value)
{
    for (std::size_t index = 0; index < N; ++index) values[index] = value;
}
} // namespace arch
