/**
 * @file RuntimeProbe.cpp
 * @brief Query driver/runtime capability without constructing a CUDA backend.
 *
 * Dynamically loaded APIs report the selected device, library versions and
 * primary-context state. Compiled SASS/PTX metadata is checked here; backend
 * resolution decides how to use the result, and simulation allocation occurs later.
 * Workflow:
 * 1. Read requested EOS, network, solver and backend capabilities.
 * 2. Validate the combination before owning runtime resources.
 * 3. Return an explicit supported plan or an actionable error.
 */

#include "driver/dispatch/capability/RuntimeProbe.h"

#include <charconv>
#include <filesystem>

#ifndef ARCH_CUDA_CODE_IMAGES
#define ARCH_CUDA_CODE_IMAGES ""
#endif
#ifndef ARCH_CUDA_PTX_VERSION
#define ARCH_CUDA_PTX_VERSION 0
#endif

// Exact runtime libraries selected by the configured optional provider.
#ifndef ARCH_CUDSS_RUNTIME_LIBRARY
#define ARCH_CUDSS_RUNTIME_LIBRARY ""
#endif
#ifndef ARCH_CUBLAS_RUNTIME_LIBRARY
#define ARCH_CUBLAS_RUNTIME_LIBRARY ""
#endif
#ifndef ARCH_CUBLASLT_RUNTIME_LIBRARY
#define ARCH_CUBLASLT_RUNTIME_LIBRARY ""
#endif

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace arch::dispatch
{

bool cuda_image_compatible(std::string_view images, int major, int minor,
                           int driver_version, int ptx_version) noexcept
{
    if (major <= 0 || major > 99 || minor < 0 || minor > 9) return false;
    const int device = major * 10 + minor;
    bool compatible = false;
    while (!images.empty()) {
        const auto comma = images.find(',');
        const auto image = images.substr(0, comma);
        const auto dash = image.find('-');
        const auto number = image.substr(0, dash);
        int architecture = 0;
        const auto parsed = std::from_chars(number.data(), number.data() + number.size(), architecture);
        if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size()
            || architecture < 10 || architecture > 999) return false;
        const auto kind = dash == std::string_view::npos ? std::string_view{} : image.substr(dash);
        if (!kind.empty() && kind != "-real" && kind != "-virtual") return false;
        const bool sass = kind != "-virtual" && architecture / 10 == major
                       && device >= architecture;
        const bool ptx = kind != "-real" && device >= architecture
                      && ptx_version > 0 && driver_version >= ptx_version;
        compatible = compatible || sass || ptx;
        if (comma == std::string_view::npos) break;
        images.remove_prefix(comma + 1);
        if (images.empty()) return false;
    }
    return compatible;
}

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

LibraryHandle open_bound_library(const char* path) noexcept
{
    LibraryHandle library = nullptr;
    try {
        std::error_code error;
#if defined(_WIN32)
        // Windows imports the provider at startup. Validate the module actually
        // loaded for that import, including a legitimate relocated deployment.
        const std::string_view configured(path);
        const auto separator = configured.find_last_of("/\\");
        const std::string basename(configured.substr(separator == std::string_view::npos
            ? 0 : separator + 1));
        if (basename.empty() || !GetModuleHandleExA(0, basename.c_str(), &library))
            return nullptr;
        std::array<wchar_t, 32768> actual{};
        const DWORD length = GetModuleFileNameW(library, actual.data(),
                                               static_cast<DWORD>(actual.size()));
        if (length == 0 || length >= actual.size()
            || !std::filesystem::is_regular_file(
                std::filesystem::path(std::wstring_view(actual.data(), length)), error) || error) {
            close_library(library);
            return nullptr;
        }
        return library;
#else
        // dlopen may return an already-loaded image after its file was deleted.
        // Linux's provider loads this exact configured path on demand.
        if (!std::filesystem::is_regular_file(path, error) || error) return nullptr;
        return open_library(path);
#endif
    } catch (...) {
        close_library(library);
        return nullptr;
    }
}

class NativeRuntimeLoader final : public RuntimeLoader
{
public:
    bool cudss_available() noexcept override
    {
        // Retain BLAS while loading cuDSS, as the production provider does for
        // toolkit installs whose directory is not on the dynamic loader path.
        const auto blas_lt = open_bound_library(ARCH_CUBLASLT_RUNTIME_LIBRARY);
        const auto blas = blas_lt ? open_bound_library(ARCH_CUBLAS_RUNTIME_LIBRARY) : nullptr;
        const auto sparse = blas ? open_bound_library(ARCH_CUDSS_RUNTIME_LIBRARY) : nullptr;
        bool ready = blas_lt && blas && sparse;
        if (ready) {
            ready = load_symbol<void (*)()>(blas_lt, "cublasLtCreate")
                && load_symbol<void (*)()>(blas, "cublasCreate_v2");
            constexpr const char* sparse_symbols[] = {
                "cudssConfigCreate", "cudssConfigDestroy", "cudssConfigSet", "cudssCreate",
                "cudssDataCreate", "cudssDataDestroy", "cudssDataGet", "cudssDestroy",
                "cudssExecute", "cudssGetProperty", "cudssMatrixCreateCsr", "cudssMatrixCreateDn",
                "cudssMatrixDestroy", "cudssSetStream"};
            for (const char* name : sparse_symbols)
                ready = ready && load_symbol<void (*)()>(sparse, name) != nullptr;
        }
        close_library(sparse);
        close_library(blas);
        close_library(blas_lt);
        return ready;
    }

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
        result.device.compiled_image_available = cuda_image_compatible(
            ARCH_CUDA_CODE_IMAGES, result.device.compute_major,
            result.device.compute_minor, result.device.driver_version, ARCH_CUDA_PTX_VERSION);
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
    auto result = loader.query(request.device_ordinal);
    result.cudss = ProviderProbeState::NotRequested;
    if (request.require_cudss && result.state == RuntimeProbeState::Available)
        result.cudss = loader.cudss_available()
            ? ProviderProbeState::Available : ProviderProbeState::Unavailable;
    return result;
}

RuntimeProbeResult probe_runtime_native(
    RuntimeProbeRequest request) noexcept
{
    NativeRuntimeLoader loader;
    return probe_runtime(request, loader);
}

} // namespace arch::dispatch
