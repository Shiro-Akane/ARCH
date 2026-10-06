/**
 * @file GravityExecution.cpp
 * @brief Execute the common gravity task descriptors on CPU-resident data.
 *
 * Workflow:
 * 1. Reuse CompositeExecution for allocations, row operations and Poisson.
 * 2. Visit density gathers, moment updates, boundary evaluations and force
 *    tasks without duplicating the host/device mathematical formulas.
 * 3. Balance variable-cost boundary traversals with dynamic chunks; keep
 *    simple cell work static and small tasks serial to amortize scheduling.
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
            if constexpr (std::is_same_v<Task,EvaluateRingBoundary>) {
                if(!task.result)throw std::invalid_argument("Missing ring work output");
                // A rejected/stale request cannot leave an earlier bounded
                // result available under this invocation's output reference.
                *task.result=RingBoundaryEvaluation{};
                if(!task.source||!task.op||!task.identity||!task.control)
                    throw std::invalid_argument("Incomplete ring work descriptor");
                *task.result=task.source->ring_boundary(*task.op,*task.identity,*task.control);
            } else {
                if constexpr (std::is_same_v<Task,EvaluateBoundary>)
                    validate_legacy_boundary_work(task);
                // Isolated boundary evaluation traverses a mass tree and may
                // integrate nearby leaves; it has much more work per item than
                // simple gather/acceleration tasks.
                constexpr int parallel_threshold=std::is_same_v<Task,EvaluateBoundary>?256:32768;
                if constexpr (std::is_same_v<Task,EvaluateBoundary>) {
                    // Equal face counts do not imply equal tree/near-leaf work.
                    // Small contiguous chunks share that work among available
                    // workers. Each face owns its output, and its traversal and
                    // compensated summation order remain entirely unchanged.
                    #pragma omp parallel for if(task.size>parallel_threshold) schedule(dynamic,16)
                    for(int i=0;i<task.size;++i)task(i);
                } else {
                    #pragma omp parallel for if(task.size>parallel_threshold) schedule(static)
                    for(int i=0;i<task.size;++i)task(i);
                }
            }
        },work);
    }
};
}
std::shared_ptr<GravityExecution> make_host_gravity_execution(){return std::make_shared<HostGravityExecution>();}
}
