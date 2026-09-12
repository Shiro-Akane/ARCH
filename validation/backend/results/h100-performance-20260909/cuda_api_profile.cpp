// Diagnostic LD_PRELOAD observer. No extra CUDA synchronization or events.
// Records host API latency, not kernel execution time. Profiled runs are kept
// separate from the uninstrumented CPU/CUDA benchmark medians.
#include <cuda_runtime_api.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <time.h>

namespace {
enum Api { Launch, CopyAsync, Copy, SetAsync, Set, Allocate, Release,
           StreamSync, DeviceSync, HostAllocate, HostRelease, EventSync, Count };
const char* names[Count] = {"cudaLaunchKernel", "cudaMemcpyAsync", "cudaMemcpy",
    "cudaMemsetAsync", "cudaMemset", "cudaMalloc", "cudaFree",
    "cudaStreamSynchronize", "cudaDeviceSynchronize", "cudaHostAlloc",
    "cudaFreeHost", "cudaEventSynchronize"};
struct Stats {
    std::atomic<unsigned long long> calls{0}, ns{0}, maximum{0}, bytes{0}, errors{0}, nested{0};
};
Stats stats[Count];
Stats async_copies[10];
const char* copy_names[10] = {"host_to_host_le16", "host_to_host_gt16",
    "host_to_device_le16", "host_to_device_gt16", "device_to_host_le16", "device_to_host_gt16",
    "device_to_device_le16", "device_to_device_gt16", "default_le16", "default_gt16"};
thread_local unsigned int nesting = 0;
unsigned long long now() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<unsigned long long>(t.tv_sec) * 1000000000ULL + t.tv_nsec;
}
const unsigned long long started = now();
void record(Stats& value, size_t bytes, unsigned long long elapsed, cudaError_t result, bool nested) {
    value.calls.fetch_add(1, std::memory_order_relaxed);
    value.ns.fetch_add(elapsed, std::memory_order_relaxed);
    value.bytes.fetch_add(bytes, std::memory_order_relaxed);
    if (result != cudaSuccess) value.errors.fetch_add(1, std::memory_order_relaxed);
    if (nested) value.nested.fetch_add(1, std::memory_order_relaxed);
    auto maximum = value.maximum.load(std::memory_order_relaxed);
    while (maximum < elapsed && !value.maximum.compare_exchange_weak(maximum, elapsed)) {}
}
template<class F> cudaError_t measure(Api api, size_t bytes, F invoke, int copy_bucket = -1) {
    const bool nested = nesting++ != 0;
    const auto begin = now();
    const auto result = invoke();
    const auto elapsed = now() - begin;
    --nesting;
    record(stats[api], bytes, elapsed, result, nested);
    if (copy_bucket >= 0 && copy_bucket < 10)
        record(async_copies[copy_bucket], bytes, elapsed, result, nested);
    return result;
}
void print_stats(FILE* out, const char* name, Stats& value) {
    std::fprintf(out, "{\"name\":\"%s\",\"calls\":%llu,\"seconds\":%.9f,\"max_seconds\":%.9f,\"bytes\":%llu,\"errors\":%llu,\"nested_calls\":%llu}",
        name, value.calls.load(), value.ns.load() * 1e-9, value.maximum.load() * 1e-9,
        value.bytes.load(), value.errors.load(), value.nested.load());
}
template<class F> F resolve(const char* name) {
    auto result = reinterpret_cast<F>(dlsym(RTLD_NEXT, name));
    if (!result) { std::fprintf(stderr, "profile unresolved API: %s\n", name); std::abort(); }
    return result;
}
__attribute__((destructor)) void report() {
    const char* path = std::getenv("ARCH_CUDA_API_PROFILE");
    if (!path) return;
    FILE* out = std::fopen(path, "w");
    if (!out) return;
    std::fprintf(out, "{\"scope\":\"host CUDA API inclusive latency; no injected synchronization\",\"process_seconds\":%.9f,\"apis\":[",
                 (now() - started) * 1e-9);
    for (int i = 0; i < Count; ++i) {
        if (i) std::fputs(",", out);
        print_stats(out, names[i], stats[i]);
    }
    std::fputs("],\"async_copy_categories\":[", out);
    for (int i = 0; i < 10; ++i) {
        if (i) std::fputs(",", out);
        print_stats(out, copy_names[i], async_copies[i]);
    }
    std::fprintf(out, "]}\n");
    std::fclose(out);
}
}
#define REAL(name) static auto original = resolve<decltype(&name)>(#name)
extern "C" cudaError_t cudaLaunchKernel(const void* f, dim3 g, dim3 b, void** a, size_t n, cudaStream_t s) {
    REAL(cudaLaunchKernel); return measure(Launch, 0, [&]{ return original(f,g,b,a,n,s); });
}
extern "C" cudaError_t cudaMemcpyAsync(void* d, const void* s, size_t n, cudaMemcpyKind k, cudaStream_t q) {
    REAL(cudaMemcpyAsync); return measure(CopyAsync,n,[&]{ return original(d,s,n,k,q); },
        static_cast<int>(k) * 2 + (n > 16 ? 1 : 0));
}
extern "C" cudaError_t cudaMemcpy(void* d, const void* s, size_t n, cudaMemcpyKind k) {
    REAL(cudaMemcpy); return measure(Copy,n,[&]{ return original(d,s,n,k); });
}
extern "C" cudaError_t cudaMemsetAsync(void* d, int v, size_t n, cudaStream_t q) {
    REAL(cudaMemsetAsync); return measure(SetAsync,n,[&]{ return original(d,v,n,q); });
}
extern "C" cudaError_t cudaMemset(void* d, int v, size_t n) {
    REAL(cudaMemset); return measure(Set,n,[&]{ return original(d,v,n); });
}
extern "C" cudaError_t cudaMalloc(void** d, size_t n) {
    REAL(cudaMalloc); return measure(Allocate,n,[&]{ return original(d,n); });
}
extern "C" cudaError_t cudaFree(void* d) {
    REAL(cudaFree); return measure(Release,0,[&]{ return original(d); });
}
extern "C" cudaError_t cudaStreamSynchronize(cudaStream_t q) {
    REAL(cudaStreamSynchronize); return measure(StreamSync,0,[&]{ return original(q); });
}
extern "C" cudaError_t cudaDeviceSynchronize() {
    REAL(cudaDeviceSynchronize); return measure(DeviceSync,0,[&]{ return original(); });
}
extern "C" cudaError_t cudaHostAlloc(void** p, size_t n, unsigned int f) {
    REAL(cudaHostAlloc); return measure(HostAllocate,n,[&]{ return original(p,n,f); });
}
extern "C" cudaError_t cudaFreeHost(void* p) {
    REAL(cudaFreeHost); return measure(HostRelease,0,[&]{ return original(p); });
}
extern "C" cudaError_t cudaEventSynchronize(cudaEvent_t e) {
    REAL(cudaEventSynchronize); return measure(EventSync,0,[&]{ return original(e); });
}
