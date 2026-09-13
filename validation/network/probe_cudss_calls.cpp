// Test-only LD_PRELOAD observer. No added synchronization and no changed status.
// Host API durations are NOT GPU kernel times; sync time can overlap their work.
#include <cuda_runtime_api.h>
#include <cudss.h>
#include <dlfcn.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace {
using Clock = std::chrono::steady_clock;
struct Counter { std::atomic<unsigned long long> count{0}, ns{0}; };
std::array<Counter,4> counters;
void record(int index, Clock::time_point start) {
    counters[index].count.fetch_add(1,std::memory_order_relaxed);
    counters[index].ns.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count(),std::memory_order_relaxed);
}
void* next(const char* name) {
    auto p = dlsym(RTLD_NEXT,name);
    if (!p) { std::fprintf(stderr,"NATIVE_OBSERVER_MISSING,%s\n",name); std::abort(); }
    return p;
}
__attribute__((destructor)) void report() {
    const char* names[]{"analysis","factorization","solve","stream_sync"};
    for (int i=0;i<4;++i)
        std::fprintf(stderr,"NATIVE_OBSERVER,%s,%llu,%.9f\n",names[i],
            counters[i].count.load(),counters[i].ns.load()*1e-9);
}
}
extern "C" cudssStatus_t cudssExecute(cudssHandle_t handle,int phase,cudssConfig_t config,
    cudssData_t data,cudssMatrix_t a,cudssMatrix_t x,cudssMatrix_t b) {
    static auto real = reinterpret_cast<decltype(&cudssExecute)>(next("cudssExecute"));
    auto start=Clock::now();
    auto result=real(handle,phase,config,data,a,x,b);
    if (phase==CUDSS_PHASE_ANALYSIS) record(0,start);
    else if (phase==CUDSS_PHASE_FACTORIZATION) record(1,start);
    else if (phase==CUDSS_PHASE_SOLVE) record(2,start);
    return result;
}
extern "C" cudaError_t cudaStreamSynchronize(cudaStream_t stream) {
    static auto real = reinterpret_cast<decltype(&cudaStreamSynchronize)>(next("cudaStreamSynchronize"));
    auto start=Clock::now();
    auto result=real(stream);
    record(3,start);
    return result;
}
extern "C" cudssStatus_t cudssDataGet(cudssHandle_t handle,cudssData_t data,cudssDataParam_t param,
    void* value,size_t bytes,size_t* written) {
    static auto real = reinterpret_cast<decltype(&cudssDataGet)>(next("cudssDataGet"));
    const auto result=real(handle,data,param,value,bytes,written);
    if (param==CUDSS_DATA_MEMORY_ESTIMATES && result==CUDSS_STATUS_SUCCESS &&
        value && bytes>=2*sizeof(int64_t)) {
        const auto* estimates=static_cast<const int64_t*>(value);
        std::fprintf(stderr,"NATIVE_FACTOR_ESTIMATE,%lld,%lld\n",
            static_cast<long long>(estimates[0]),static_cast<long long>(estimates[1]));
    }
    return result;
}
