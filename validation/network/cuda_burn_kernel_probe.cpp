// Diagnostic metadata probe, never part of production or formal speed samples.
// Runtime function attributes are queried once per observed ARCH launch entry.
#include <cuda_runtime_api.h>
#include <algorithm>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>

namespace {
struct Entry {
    cudaFuncAttributes attributes{};
    cudaError_t query_status = cudaSuccess;
    unsigned long long calls = 0, max_grid = 0, max_threads = 0;
};
// A process-lifetime diagnostic table avoids CUDA work from global destructors.
auto& entries() { static auto* value = new std::map<const void*, Entry>; return *value; }
auto& lock() { static auto* value = new std::mutex; return *value; }
thread_local bool inside = false;
template<class F> F resolve(const char* name) {
    auto result = reinterpret_cast<F>(dlsym(RTLD_NEXT, name));
    if (!result) { std::fprintf(stderr, "kernel probe unresolved %s\n", name); std::abort(); }
    return result;
}
__attribute__((destructor)) void report() {
    const char* path = std::getenv("ARCH_CUDA_KERNEL_PROBE");
    if (!path) return;
    FILE* output = std::fopen(path, "w");
    if (!output) return;
    std::fputs("scope\tdiagnostic_runtime_kernel_attributes_not_execution_time\n", output);
    std::fputs("function\timage_offset\tcalls\tlocal_bytes_per_thread\tregisters\tstatic_shared_bytes\tmax_grid_blocks\tmax_launch_threads\tquery_status\n", output);
    std::lock_guard<std::mutex> guard(lock());
    for (const auto& [function, item] : entries()) {
        Dl_info info{};
        dladdr(function, &info);
        const auto offset = reinterpret_cast<std::size_t>(function) - reinterpret_cast<std::size_t>(info.dli_fbase);
        std::fprintf(output, "%s\t%zx\t%llu\t%zu\t%d\t%zu\t%llu\t%llu\t%d\n",
            info.dli_sname ? info.dli_sname : "unexported", offset, item.calls,
            item.attributes.localSizeBytes, item.attributes.numRegs, item.attributes.sharedSizeBytes,
            item.max_grid, item.max_threads, static_cast<int>(item.query_status));
    }
    std::fclose(output);
}
}

extern "C" cudaError_t cudaLaunchKernel(const void* function, dim3 grid, dim3 block,
                                       void** args, size_t shared, cudaStream_t stream) {
    static auto original = resolve<decltype(&cudaLaunchKernel)>("cudaLaunchKernel");
    static auto attributes = resolve<decltype(&cudaFuncGetAttributes)>("cudaFuncGetAttributes");
    if (!inside) {
        inside = true;
        {
            std::lock_guard<std::mutex> guard(lock());
            auto& entry = entries()[function];
            if (entry.calls == 0) entry.query_status = attributes(&entry.attributes, function);
            ++entry.calls;
            entry.max_grid = std::max(entry.max_grid, 1ULL * grid.x * grid.y * grid.z);
            entry.max_threads = std::max(entry.max_threads, 1ULL * block.x * block.y * block.z);
        }
        inside = false;
    }
    return original(function, grid, block, args, shared, stream);
}
