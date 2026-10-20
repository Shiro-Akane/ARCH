/**
 * Linux/GNU-only diagnostic interposer for actual application OpenMP teams.
 * ABI: GCC 13.3 libgomp/parallel.c, GOMP_parallel (GOMP_4.0).
 * No numerical logic, scheduling choice or caller arguments are changed.
 * A trusted harness owns ARCH_GOMP_TRACE_FD and the inherited output file.
 * The first 128 regions retain per-member observations; the final count makes
 * truncation explicit. Never use an instrumented process for formal timing.
 */
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <omp.h>
#include <mutex>
#include <sched.h>
#include <unistd.h>

namespace {
using Parallel = void (*)(void (*)(void*), void*, unsigned, unsigned);
std::atomic<unsigned long> total{0};
constexpr unsigned limit = 128;
std::mutex output_mutex;
constexpr int members_limit = 1024;
std::atomic<unsigned long> team_counts[members_limit+1]{};
std::atomic<unsigned long> oversized{0};
struct Callable { void (*fn)(void*); void* data; };
struct Member { int cpu=-1, place=-1, readable=0; cpu_set_t mask{}; };
struct Context {
    void (*fn)(void*);
    void* data;
    int team=0;
    Member members[members_limit];
};
int output_fd() {
    const char* v=std::getenv("ARCH_GOMP_TRACE_FD");
    if(!v||!*v)return -1;
    char* end=nullptr;
    const long fd=std::strtol(v,&end,10);
    if(*end||fd<3||fd>1048575)_exit(125);
    return static_cast<int>(fd);
}
Parallel real_parallel() {
    static Parallel fn=[]{
        void* p=dlvsym(RTLD_NEXT,"GOMP_parallel","GOMP_4.0");
        if(!p)_exit(125);
        Parallel result;
        static_assert(sizeof(result)==sizeof(p));
        std::memcpy(&result,&p,sizeof(result));
        return result;
    }();
    return fn;
}
void count_team() {
    if(omp_get_thread_num()!=0)return;
    const int n=omp_get_num_threads();
    if(n>=1&&n<=members_limit)team_counts[n].fetch_add(1);
    else oversized.fetch_add(1);
}
void count_only(void* data) {
    auto& c=*static_cast<Callable*>(data);
    count_team();c.fn(c.data);
}
void observe(void* data) {
    count_team();
    auto& c=*static_cast<Context*>(data);
    const int tid=omp_get_thread_num();
    if(tid==0)c.team=omp_get_num_threads();
    if(tid>=0&&tid<members_limit) {
        auto& m=c.members[tid];
        m.cpu=sched_getcpu();m.place=omp_get_place_num();
        m.readable=sched_getaffinity(0,sizeof(m.mask),&m.mask)==0;
    }
    c.fn(c.data);
}
}
extern "C" void GOMP_parallel(void (*fn)(void*),void* data,unsigned requested,unsigned flags) {
    const auto real=real_parallel();
    const int fd=output_fd();
    if(fd<0){real(fn,data,requested,flags);return;}
    const auto id=total.fetch_add(1);
    if(id>=limit){Callable c{fn,data};real(count_only,&c,requested,flags);return;}
    Context c{fn,data,0,{}};
    real(observe,&c,requested,flags);
    // real GOMP_parallel joins the team before returning; no added barrier.
    // Serialize complete records even when nested teams return concurrently.
    const std::lock_guard<std::mutex> lock(output_mutex);
    FILE* f=fdopen(dup(fd),"a");
    if(!f)_exit(125);
    flockfile(f);
    std::fprintf(f,"{\"region\":%lu,\"requestedArgument\":%u,\"flags\":%u,\"team\":%d,\"members\":[",id,requested,flags,c.team);
    for(int t=0;t<c.team&&t<members_limit;++t) {
        const auto& m=c.members[t];
        std::fprintf(f,"%s{\"thread\":%d,\"cpu\":%d,\"place\":%d,\"readable\":%s,\"affinity\":[",t?",":"",t,m.cpu,m.place,m.readable?"true":"false");
        bool comma=false;
        for(int cpu=0;cpu<CPU_SETSIZE;++cpu)if(CPU_ISSET(cpu,&m.mask)){
            std::fprintf(f,"%s%d",comma?",":"",cpu);comma=true;
        }
        std::fprintf(f,"]}");
    }
    std::fprintf(f,"]}\n");
    funlockfile(f);
    if(std::fclose(f)!=0)_exit(125);
}
__attribute__((destructor)) static void finish() {
    const int fd=output_fd();
    if(fd<0)return;
    if(dprintf(fd,"{\"summary\":true,\"interceptedRegions\":%lu,\"recordLimit\":%u,\"oversizedTeams\":%lu,\"teamSizeCounts\":{",total.load(),limit,oversized.load())<0)_exit(125);
    bool comma=false;
    for(int n=1;n<=members_limit;++n)if(const auto count=team_counts[n].load()){
        if(dprintf(fd,"%s\"%d\":%lu",comma?",":"",n,count)<0)_exit(125);
        comma=true;
    }
    if(dprintf(fd,"}}\n")<0)_exit(125);
}
