#include "ComputeBackend.h"

#include <stdexcept>
#include <string>

namespace arch::runtime {

ComputeBackend parse_compute_backend(std::string_view value)
{
    if (value == "cpu") {
        return ComputeBackend::Cpu;
    }
    if (value == "cuda") {
        return ComputeBackend::Cuda;
    }
    if (value == "auto") {
        return ComputeBackend::Auto;
    }

    throw std::invalid_argument(
        "Invalid compute backend '" + std::string(value)
        + "'; expected exactly one of: cpu, cuda, auto");
}

std::string_view compute_backend_name(ComputeBackend backend) noexcept
{
    switch (backend) {
    case ComputeBackend::Cpu:
        return "cpu";
    case ComputeBackend::Cuda:
        return "cuda";
    case ComputeBackend::Auto:
        return "auto";
    }
    return "invalid";
}

} // namespace arch::runtime
