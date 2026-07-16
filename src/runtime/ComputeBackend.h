#pragma once

#include <string_view>

namespace arch::runtime {

/**
 * @brief Execution backend requested by the runtime configuration.
 *
 * Auto means "select CUDA only when the binary contains CUDA support and a
 * usable device is present".  The final selection is made by the caller; this
 * enum never implies a silent fallback from an explicit Cuda request.
 */
enum class ComputeBackend
{
    Cpu,
    Cuda,
    Auto
};

/**
 * @brief Parse the exact configuration values "cpu", "cuda", and "auto".
 * @throws std::invalid_argument for every other value.
 */
ComputeBackend parse_compute_backend(std::string_view value);

/** @brief Return the canonical lower-case configuration spelling. */
std::string_view compute_backend_name(ComputeBackend backend) noexcept;

} // namespace arch::runtime
