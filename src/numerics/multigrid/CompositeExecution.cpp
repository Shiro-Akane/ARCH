/**
 * @file CompositeExecution.cpp
 * @brief Provide host storage, loops and reductions for shared composite arithmetic.
 *
 * Workflow:
 * 1. Receive an explicit mesh/operator and signed cell-centered fields.
 * 2. Provide host storage, loops and reductions for shared composite arithmetic.
 * 3. Return corrections or fluxes through the shared numerical contract.
 * 4. Dispatch each per-index body serially or to a host team according to the
 *    work class, without touching any scalar arithmetic body.
 */

#include <cstring>
#include <type_traits>
#ifdef _OPENMP
#include <omp.h>
#endif

#include "numerics/multigrid/CompositeExecution.h"

namespace arch::multigrid {
namespace {
// Host dispatch policy: the scalar bodies in CompositeExecution.h remain the
// ONLY arithmetic, and the host only decides which indices a team evaluates
// (task(i) is index-independent, so results are bitwise identical for any team
// size, including the serial loop). Two work classes are distinguished by cost
// per element, using only general work size/type:
//  - Vector-like work (linear combination, gauge projection, damped Jacobi) is
//    memory-bound: a few loads/stores per element, so a team only pays off on
//    large domains (the previous global host threshold, unchanged).
//  - Compensated sparse work (SparseRows apply/residual rows and both face
//    gradient specializations) performs a Neumaier accumulation over a stencil
//    per element, i.e. about an order of magnitude more arithmetic per element,
//    so the same team launch is amortized at a much smaller domain. This lets
//    medium multigrid levels run in parallel while tiny/coarse levels stay
//    serial.
// The thresholds are internal scheduler controls, not user configuration.
constexpr int kVectorSerialBelow=32768;
constexpr int kSparseSerialBelow=4096;
template<class T> constexpr int host_serial_below(){
    return std::is_same_v<T,RowsWork>||std::is_same_v<T,GradientWork>||std::is_same_v<T,LegacyGradientWork>
        ?kSparseSerialBelow:kVectorSerialBelow;
}
// Threads a new host team may use right now: 1 when OpenMP is unavailable or
// the calling thread already sits inside a team, so a nested or oversubscribed
// team is never created and single-thread runs skip team launch entirely.
int host_team_threads(){
#ifdef _OPENMP
    return omp_in_parallel()?1:omp_get_max_threads();
#else
    return 1;
#endif
}
class HostExecution final:public CompositeExecution {
public:
    bool device() const override { return false; }
    std::shared_ptr<void> allocate(std::size_t n) override {
        return {n?::operator new(n):nullptr,[](void* p){::operator delete(p);}};
    }
    void copy(void* to,const void* from,std::size_t bytes,Transfer) override { std::memcpy(to,from,bytes); }
    void run(const CompositeWork& work) override {
        const int threads=host_team_threads();
        std::visit([threads](auto task){
            // Explicit serial dispatch below the class threshold: no OpenMP
            // region is entered (and serialized) for tiny or coarse work.
            if(threads<=1 || task.size<host_serial_below<decltype(task)>()) {
                for(int i=0;i<task.size;++i) task(i);
                return;
            }
            #pragma omp parallel for schedule(static)
            for(int i=0;i<task.size;++i) task(i);
        },work);
    }
    double reduce(const Reduction& task) override {
        const int n=(task.size+Reduction::chunk-1)/Reduction::chunk;
        partials_.resize(n);
        const int threads=host_team_threads();
        // Partial blocks keep their fixed chunk=64 layout and the serial finish
        // order; only the block scheduler changes, so the sum is bitwise
        // unchanged for every team size.
        if(threads>1 && n>512) {
            #pragma omp parallel for schedule(static)
            for(int i=0;i<n;++i) partials_[i]=task.partial(i);
        } else {
            for(int i=0;i<n;++i) partials_[i]=task.partial(i);
        }
        return task.finish(partials_.data(),n);
    }
    void project(Vector& x,const Vector& weights) override {
        const double scale=maximum(x);
        const double sum=scale==0.?0.:reduce({x.data,nullptr,weights.data,x.size,ReductionKind::Product,scale});
        run(ProjectWork{x.size,x.data,&scale,&sum});
    }
    void fence() override {}
private: std::vector<double> partials_;
};
}
std::shared_ptr<CompositeExecution> make_host_composite_execution(){return std::make_shared<HostExecution>();}
}
