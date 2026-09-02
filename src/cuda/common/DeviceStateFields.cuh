/**
 * @file DeviceStateFields.cuh
 * @brief Backend-local field selection for SoA CUDA state memory.
 */

#pragma once

#include "CudaCommon.cuh"

#include <cstddef>

namespace arch::cuda {

ARCH_INLINE double* device_state_field(DeviceStateView state, int field)
{
    if (field == 0) return state.rho;
    if (field == 1) return state.mom_u;
    if (field == 2) return state.mom_v;
    if (field == 3) return state.mom_w;
    if (field == 4) return state.eng;
    if (field == 5) return state.enuc_rate;
    return state.mass_fractions
        + static_cast<std::size_t>(field - 6) * state.total_size;
}

} // namespace arch::cuda
