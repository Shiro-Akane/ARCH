// Test-only LD_PRELOAD diagnostic. Native Host API latency is NOT GPU kernel
// time and is never mixed into formal speedup samples. No extra fences or
// kernel work; one successful function-name query is required per new kernel.
#include <cuda_runtime_api.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <map>
#include <mutex>
#include <string>

namespace {
using Clock=std::chrono::steady_clock;
struct Row { unsigned long long calls=0,bytes=0,blocks=0,threads=0; double seconds=0,first_seconds=0; };
struct Registry {
    std::mutex mutex;
    std::map<std::string,Row> rows;
    std::map<const void*,std::string> names;
    std::map<cudaStream_t,std::string> last_kernel;
    unsigned long long launches=0;
};
Registry& registry() { static auto* value=new Registry; return *value; }
template<class T> T symbol(const char* name) {
    auto result=reinterpret_cast<T>(dlsym(RTLD_NEXT,name));
    if (!result) { std::fprintf(stderr,"CUDA_OBSERVER_UNAVAILABLE,%s\n",name); std::abort(); }
    return result;
}
void add(const std::string& key,Clock::time_point start,Clock::time_point end,unsigned long long bytes=0,
         unsigned long long blocks=0,unsigned long long threads=0) {
    const double elapsed=std::chrono::duration<double>(end-start).count();
    auto& r=registry(); std::lock_guard<std::mutex> lock(r.mutex);
    auto& row=r.rows[key];
    if (row.calls++==0) row.first_seconds=elapsed;
    row.seconds+=elapsed; row.bytes+=bytes; row.blocks+=blocks; row.threads+=threads;
    // Prefix-only diagnostic: a timeout may use SIGKILL, so retain snapshots
    // without adding a CUDA event, query, fence or production state mutation.
    if (key.rfind("launch:",0)==0 && ++r.launches % 10000 == 0) {
        std::fprintf(stderr,"CUDA_PROGRESS_BEGIN,%llu\n",r.launches);
        for (const auto& [name,value]:r.rows)
            std::fprintf(stderr,"CUDA_PROGRESS,%s,%llu,%.9f,%.9f,%llu,%llu,%llu\n",name.c_str(),
                value.calls,value.seconds,value.first_seconds,value.bytes,value.blocks,value.threads);
        std::fprintf(stderr,"CUDA_PROGRESS_END,%llu\n",r.launches);
    }
}
std::string kernel_name(const void* function) {
    auto& r=registry(); std::lock_guard<std::mutex> lock(r.mutex);
    auto found=r.names.find(function);
    if (found!=r.names.end()) return found->second;
    using Query=cudaError_t (*)(const char**,const void*);
    static auto query=symbol<Query>("cudaFuncGetName");
    const char* name=nullptr;
    const auto start=Clock::now();
    const auto status=query(&name,function);
    const double elapsed=std::chrono::duration<double>(Clock::now()-start).count();
    auto& row=r.rows["kernel_name_lookup"];
    if (row.calls++==0) row.first_seconds=elapsed;
    row.seconds+=elapsed;
    if (status!=cudaSuccess || !name) {
        std::fprintf(stderr,"CUDA_OBSERVER_UNAVAILABLE,kernel_name\n"); std::abort();
    }
    return r.names.emplace(function,name).first->second;
}
__attribute__((destructor)) void report() {
    auto& r=registry(); std::lock_guard<std::mutex> lock(r.mutex);
    std::fprintf(stderr,"CUDA_OBSERVER_HEADER,key,calls,host_seconds,first_host_seconds,bytes,blocks,threads\n");
    for (const auto& [key,row]:r.rows)
        std::fprintf(stderr,"CUDA_OBSERVER,%s,%llu,%.9f,%.9f,%llu,%llu,%llu\n",key.c_str(),
            row.calls,row.seconds,row.first_seconds,row.bytes,row.blocks,row.threads);
}
}

extern "C" cudaError_t cudaLaunchKernel(const void* function,dim3 grid,dim3 block,
    void** arguments,size_t shared,cudaStream_t stream) {
    using Fn=decltype(&cudaLaunchKernel);
    static auto native=symbol<Fn>("cudaLaunchKernel");
    const auto name=kernel_name(function);
    { auto& r=registry(); std::lock_guard<std::mutex> lock(r.mutex); r.last_kernel[stream]=name; }
    const auto start=Clock::now();
    const auto result=native(function,grid,block,arguments,shared,stream);
    const auto end=Clock::now();
    add("launch:"+name,start,end,0,static_cast<unsigned long long>(grid.x)*grid.y*grid.z,
        static_cast<unsigned long long>(grid.x)*grid.y*grid.z*block.x*block.y*block.z);
    return result;
}
extern "C" cudaError_t cudaStreamSynchronize(cudaStream_t stream) {
    static auto native=symbol<decltype(&cudaStreamSynchronize)>("cudaStreamSynchronize");
    std::string name;
    { auto& r=registry(); std::lock_guard<std::mutex> lock(r.mutex); name=r.last_kernel[stream]; }
    const auto start=Clock::now(); const auto result=native(stream);
    const auto end=Clock::now();
    add("stream_sync_after:"+name,start,end); return result;
}
extern "C" cudaError_t cudaMemcpyAsync(void* destination,const void* source,size_t bytes,
    cudaMemcpyKind kind,cudaStream_t stream) {
    static auto native=symbol<decltype(&cudaMemcpyAsync)>("cudaMemcpyAsync");
    const auto start=Clock::now(); const auto result=native(destination,source,bytes,kind,stream);
    const auto end=Clock::now();
    add("memcpy_async:"+std::to_string(static_cast<int>(kind)),start,end,bytes); return result;
}
extern "C" cudaError_t cudaMalloc(void** pointer,size_t bytes) {
    static auto native=symbol<decltype(&cudaMalloc)>("cudaMalloc");
    const auto start=Clock::now(); const auto result=native(pointer,bytes);
    const auto end=Clock::now();
    add("malloc",start,end,bytes); return result;
}
extern "C" cudaError_t cudaFree(void* pointer) {
    static auto native=symbol<decltype(&cudaFree)>("cudaFree");
    const auto start=Clock::now(); const auto result=native(pointer);
    const auto end=Clock::now();
    add("free",start,end); return result;
}
