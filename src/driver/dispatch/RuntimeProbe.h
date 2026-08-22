#pragma once

#include "ResolvedExecutionPlan.h"

#include <array>
#include <cstdint>

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

} // namespace arch::dispatch
