/**
 * @file GravityExecution.cpp
 * @brief Execute the common gravity task descriptors on CPU-resident data.
 *
 * Workflow:
 * 1. Reuse CompositeExecution for allocations, row operations and Poisson.
 * 2. Visit density gathers, moment updates, boundary evaluations and force
 *    tasks without duplicating the host/device mathematical formulas.
 * 3. Parallelize boundary-heavy work above a smaller task threshold while
 *    leaving inexpensive tasks serial until they can amortize scheduling.
 */

#include "physics/gravity/GravityExecution.h"

#include <type_traits>

namespace Physical::Gravity {
namespace {
class HostGravityExecution final:public GravityExecution {
    std::shared_ptr<arch::multigrid::CompositeExecution> execution_=arch::multigrid::make_host_composite_execution();
public:
    auto numeric() const -> std::shared_ptr<arch::multigrid::CompositeExecution> override {return execution_;}
    void run(const GravityWork& work) override {
        std::visit([](auto task){
            using Task=std::decay_t<decltype(task)>;
            // Isolated boundary evaluation traverses a mass tree and may
            // integrate nearby leaves; it has much more work per item than
            // simple gather/acceleration tasks.
            constexpr int parallel_threshold=std::is_same_v<Task,EvaluateBoundary>?256:32768;
            #pragma omp parallel for if(task.size>parallel_threshold) schedule(static)
            for(int i=0;i<task.size;++i)task(i);
        },work);
    }
};
}
std::shared_ptr<GravityExecution> make_host_gravity_execution(){return std::make_shared<HostGravityExecution>();}
}
