/**
 * @file RuntimeProbe.h
 * @brief Ordinary-C++ interface for querying CUDA runtime availability.
 *
 * An injectable loader reports device and compiled-image compatibility before
 * backend construction. The probe does not choose a physics policy or own the
 * simulation's CUDA stream and device allocations.
 */

#pragma once

#include "ResolvedExecutionPlan.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace arch::dispatch
{

enum class RuntimeProbeState : std::uint8_t
{
    NotRequested,
    BuildDisabled,
    Unavailable,
    Available,
};

enum class ProbeFailureCode : std::uint8_t
{
    None,
    LoaderUnavailable,
    DriverInitFailed,
    NoDevice,
    InvalidDeviceOrdinal,
    CapabilityQueryFailed,
};

struct DeviceCapability
{
    int ordinal{};
    int compute_major{};
    int compute_minor{};
    int runtime_version{};
    int driver_version{};
    std::array<char, 128> device_name{};
    bool primary_context_active_before{};
    bool primary_context_active_after{};
    bool compiled_image_available{};
};

struct RuntimeProbeRequest
{
    ComputeBackend requested{};
    bool cuda_build_enabled{};
    int device_ordinal{};
};

struct RuntimeProbeResult
{
    RuntimeProbeState state{};
    ProbeFailureCode failure{};
    DeviceCapability device{};
};

class RuntimeLoader
{
public:
    virtual ~RuntimeLoader() = default;
    virtual RuntimeProbeResult query(int device_ordinal) noexcept = 0;
};

RuntimeProbeResult probe_runtime(
    RuntimeProbeRequest request, RuntimeLoader& loader) noexcept;

RuntimeProbeResult probe_runtime_native(
    RuntimeProbeRequest request) noexcept;

// Ordinary numeric SASS/PTX images only. The configured compiler's PTX needs
// a driver that understands that toolkit version; native SASS has no JIT gate.
bool cuda_image_compatible(std::string_view images, int major, int minor,
                           int driver_version, int ptx_version) noexcept;

} // namespace arch::dispatch
