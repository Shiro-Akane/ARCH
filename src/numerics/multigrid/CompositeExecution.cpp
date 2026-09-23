/**
 * @file CompositeExecution.cpp
 * @brief Provide host storage, loops and reductions for shared composite arithmetic.
 *
 * Workflow:
 * 1. Receive an explicit mesh/operator and signed cell-centered fields.
 * 2. Provide host storage, loops and reductions for shared composite arithmetic.
 * 3. Return corrections or fluxes through the shared numerical contract.
 */

#include <cstring>

#include "numerics/multigrid/CompositeExecution.h"

namespace arch::multigrid {
namespace {
class HostExecution final:public CompositeExecution {
public:
    bool device() const override { return false; }
    std::shared_ptr<void> allocate(std::size_t n) override {
        return {n?::operator new(n):nullptr,[](void* p){::operator delete(p);}};
    }
    void copy(void* to,const void* from,std::size_t bytes,Transfer) override { std::memcpy(to,from,bytes); }
    void run(const CompositeWork& work) override {
        std::visit([](auto task){
            #pragma omp parallel for if(task.size>32768) schedule(static)
            for(int i=0;i<task.size;++i) task(i);
        },work);
    }
    double reduce(const Reduction& task) override {
        const int n=(task.size+Reduction::chunk-1)/Reduction::chunk;
        partials_.resize(n);
        #pragma omp parallel for if(n>512) schedule(static)
        for(int i=0;i<n;++i) partials_[i]=task.partial(i);
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
