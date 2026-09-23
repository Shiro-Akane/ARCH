#include "physics/gravity/GravityExecution.h"
namespace Physical::Gravity {
namespace {
class HostGravityExecution final:public GravityExecution {
    std::shared_ptr<arch::multigrid::CompositeExecution> execution_=arch::multigrid::make_host_composite_execution();
public:
    auto numeric() const -> std::shared_ptr<arch::multigrid::CompositeExecution> override {return execution_;}
    void run(const GravityWork& work) override {
        std::visit([](auto task){
            #pragma omp parallel for if(task.size>32768) schedule(static)
            for(int i=0;i<task.size;++i)task(i);
        },work);
    }
};
}
std::shared_ptr<GravityExecution> make_host_gravity_execution(){return std::make_shared<HostGravityExecution>();}
}
