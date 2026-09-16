/**
 * @file CudaBackendTypes.h
 * @brief Lightweight host/device records shared by CUDA control units.
 */

#pragma once

#include <cstdint>

namespace arch::cuda {

struct DeviceBurnSummary {
    double limiter = 0.0;
    std::uint64_t failed_cells = 0;
    int status = 0;
};

} // namespace arch::cuda
