#include "RuntimeProbe.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace arch::dispatch
{

namespace
{

constexpr int kComputeCapabilityMajorAttribute = 75;
constexpr int kComputeCapabilityMinorAttribute = 76;

#if defined(_WIN32)
using LibraryHandle = HMODULE;

LibraryHandle open_library(const char* name) noexcept
{
    return LoadLibraryA(name);
}

void close_library(LibraryHandle handle) noexcept
{
    if (handle != nullptr) FreeLibrary(handle);
}

template <class Function>
Function load_symbol(LibraryHandle handle, const char* name) noexcept
{
    return reinterpret_cast<Function>(GetProcAddress(handle, name));
}
#else
using LibraryHandle = void*;

LibraryHandle open_library(const char* name) noexcept
{
    return dlopen(name, RTLD_NOW | RTLD_LOCAL);
}

void close_library(LibraryHandle handle) noexcept
{
    if (handle != nullptr) dlclose(handle);
}

template <class Function>
Function load_symbol(LibraryHandle handle, const char* name) noexcept
{
    return reinterpret_cast<Function>(dlsym(handle, name));
}
#endif

class NativeRuntimeLoader final : public RuntimeLoader
{
public:
    RuntimeProbeResult query(int device_ordinal) noexcept override
    {
#if defined(_WIN32)
        LibraryHandle driver = open_library("nvcuda.dll");
#else
        LibraryHandle driver = open_library("libcuda.so.1");
#endif
        if (driver == nullptr) return unavailable(ProbeFailureCode::LoaderUnavailable);

        const auto cu_init = load_symbol<int (*)(unsigned int)>(driver, "cuInit");
        const auto cu_device_get_count = load_symbol<int (*)(int*)>(driver, "cuDeviceGetCount");
        const auto cu_device_get = load_symbol<int (*)(int*, int)>(driver, "cuDeviceGet");
        const auto cu_device_get_attribute =
            load_symbol<int (*)(int*, int, int)>(driver, "cuDeviceGetAttribute");
        const auto cu_device_get_name =
            load_symbol<int (*)(char*, int, int)>(driver, "cuDeviceGetName");
        const auto cu_driver_get_version =
            load_symbol<int (*)(int*)>(driver, "cuDriverGetVersion");
        const auto cu_primary_get_state =
            load_symbol<int (*)(int, unsigned int*, int*)>(driver, "cuDevicePrimaryCtxGetState");
        if (cu_init == nullptr || cu_device_get_count == nullptr || cu_device_get == nullptr
            || cu_device_get_attribute == nullptr || cu_device_get_name == nullptr
            || cu_driver_get_version == nullptr || cu_primary_get_state == nullptr) {
            close_library(driver);
            return unavailable(ProbeFailureCode::LoaderUnavailable);
        }

        if (cu_init(0) != 0) {
            close_library(driver);
            return unavailable(ProbeFailureCode::DriverInitFailed);
        }
        int count = 0;
        if (cu_device_get_count(&count) != 0) {
            close_library(driver);
            return unavailable(ProbeFailureCode::CapabilityQueryFailed);
        }
        if (count == 0) {
            close_library(driver);
            return unavailable(ProbeFailureCode::NoDevice);
        }
        if (device_ordinal < 0 || device_ordinal >= count) {
            close_library(driver);
            return unavailable(ProbeFailureCode::InvalidDeviceOrdinal);
        }

        RuntimeProbeResult result{};
        result.state = RuntimeProbeState::Available;
        result.failure = ProbeFailureCode::None;
        result.device.ordinal = device_ordinal;
        int device = 0;
        unsigned int flags = 0;
        int active_before = 0;
        const bool capability_ok =
            cu_device_get(&device, device_ordinal) == 0
            && cu_primary_get_state(device, &flags, &active_before) == 0
            && cu_device_get_attribute(&result.device.compute_major,
                                       kComputeCapabilityMajorAttribute, device) == 0
            && cu_device_get_attribute(&result.device.compute_minor,
                                       kComputeCapabilityMinorAttribute, device) == 0
            && cu_device_get_name(result.device.device_name.data(),
                                  static_cast<int>(result.device.device_name.size()), device) == 0
            && cu_driver_get_version(&result.device.driver_version) == 0;
        if (!capability_ok) {
            close_library(driver);
            return unavailable(ProbeFailureCode::CapabilityQueryFailed);
        }
        result.device.device_name.back() = '\0';
        result.device.primary_context_active_before = active_before != 0;

#if defined(_WIN32)
        constexpr const char* runtime_names[] = {
            "cudart64_13.dll", "cudart64_12.dll", "cudart64_110.dll"};
#else
        constexpr const char* runtime_names[] = {
            "libcudart.so", "libcudart.so.13", "libcudart.so.12", "libcudart.so.11.0"};
#endif
        LibraryHandle runtime = nullptr;
        for (const char* name : runtime_names) {
            runtime = open_library(name);
            if (runtime != nullptr) break;
        }
        if (runtime == nullptr) {
            close_library(driver);
            return unavailable(ProbeFailureCode::LoaderUnavailable);
        }
        const auto runtime_get_version =
            load_symbol<int (*)(int*)>(runtime, "cudaRuntimeGetVersion");
        if (runtime_get_version == nullptr
            || runtime_get_version(&result.device.runtime_version) != 0) {
            close_library(runtime);
            close_library(driver);
            return unavailable(ProbeFailureCode::CapabilityQueryFailed);
        }
        int active_after = 0;
        if (cu_primary_get_state(device, &flags, &active_after) != 0) {
            close_library(runtime);
            close_library(driver);
            return unavailable(ProbeFailureCode::CapabilityQueryFailed);
        }
        result.device.primary_context_active_after = active_after != 0;
        close_library(runtime);
        close_library(driver);
        return result;
    }

private:
    static RuntimeProbeResult unavailable(ProbeFailureCode failure) noexcept
    {
        return {RuntimeProbeState::Unavailable, failure, {}};
    }
};

} // namespace

RuntimeProbeResult probe_runtime(
    RuntimeProbeRequest request, RuntimeLoader& loader) noexcept
{
    if (request.requested == ComputeBackend::Cpu)
        return {RuntimeProbeState::NotRequested, ProbeFailureCode::None, {}};
    if (!request.cuda_build_enabled)
        return {RuntimeProbeState::BuildDisabled, ProbeFailureCode::None, {}};
    return loader.query(request.device_ordinal);
}

RuntimeProbeResult probe_runtime_native(
    RuntimeProbeRequest request) noexcept
{
    if (request.requested == ComputeBackend::Cpu)
        return {RuntimeProbeState::NotRequested, ProbeFailureCode::None, {}};
    if (!request.cuda_build_enabled)
        return {RuntimeProbeState::BuildDisabled, ProbeFailureCode::None, {}};
    NativeRuntimeLoader loader;
    return loader.query(request.device_ordinal);
}

} // namespace arch::dispatch
