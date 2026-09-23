#include "cuda/runtime/gravity/CudaGravityExecution.h"
#include "cuda/common/DeviceAllocation.h"
namespace arch::cuda {
namespace {
using namespace arch::multigrid;
template<class Task> __global__ void execute_work(Task task) {
    const int i=blockIdx.x*blockDim.x+threadIdx.x;if(i<task.size)task(i);
}
__global__ void reduce_chunks(Reduction task,double* partials,int count) {
    const int i=blockIdx.x*blockDim.x+threadIdx.x;if(i<count)partials[i]=task.partial(i);
}
__global__ void reduce_finish(Reduction task,const double* partials,int count,double* result) {
    *result=task.finish(partials,count);
}
struct StreamLease {cudaStream_t stream;int device;std::shared_ptr<void> lifetime;};
class CudaExecution final:public CompositeExecution {
    std::shared_ptr<StreamLease> lease_;
    ReusableDeviceAllocation<double> partials_;
    DeviceAllocation<double> result_,scale_,sum_;
    ExecutionCounters counters_;
public:
    CudaExecution(cudaStream_t stream,int device,std::shared_ptr<void> lifetime)
        :lease_(std::make_shared<StreamLease>(StreamLease{stream,device,std::move(lifetime)})) {
        check_cuda(cudaSetDevice(device),"select gravity device");result_.allocate(1);scale_.allocate(1);sum_.allocate(1);
    }
    ~CudaExecution() override {set_device_and_quiesce_or_terminate(lease_->device,lease_->stream);}
    bool device() const override{return true;}
    std::shared_ptr<void> allocate(std::size_t bytes) override {
        if(!bytes)return {};
        check_cuda(cudaSetDevice(lease_->device),"select gravity device");
        auto allocation=std::make_shared<DeviceAllocation<std::byte>>();allocation->allocate(bytes);
        return {allocation->get(),[allocation,lease=lease_](void*)mutable {
            set_device_and_quiesce_or_terminate(lease->device,lease->stream);allocation.reset();
        }};
    }
    void copy(void* to,const void* from,std::size_t bytes,Transfer transfer) override {
        const auto kind=transfer==Transfer::Upload?cudaMemcpyHostToDevice:transfer==Transfer::Download?cudaMemcpyDeviceToHost:cudaMemcpyDeviceToDevice;
        check_cuda(cudaMemcpyAsync(to,from,bytes,kind,lease_->stream),"copy gravity storage");
        if(transfer==Transfer::Upload){counters_.bytes_h2d+=bytes;fence();} // borrowed host input lifetime
        if(transfer==Transfer::Download)counters_.bytes_d2h+=bytes;
    }
    template<class Variant> void launch(const Variant& work) {
        std::visit([&](auto task){if(!task.size)return;
            execute_work<<<(task.size+127)/128,128,0,lease_->stream>>>(task);
            check_cuda(cudaGetLastError(),"launch shared gravity arithmetic");++counters_.kernels;
        },work);
    }
    void run(const CompositeWork& work) override {launch(work);}
    void reduce_device(const Reduction& task,double* out) {
        const int n=(task.size+Reduction::chunk-1)/Reduction::chunk;
        partials_.reserve(n);
        reduce_chunks<<<(n+127)/128,128,0,lease_->stream>>>(task,partials_.get(),n);
        reduce_finish<<<1,1,0,lease_->stream>>>(task,partials_.get(),n,out);
        check_cuda(cudaGetLastError(),"reduce shared gravity scalars");counters_.kernels+=2;
    }
    double reduce(const Reduction& task) override {
        reduce_device(task,result_.get());
        double result=0.;copy(&result,result_.get(),sizeof(double),Transfer::Download);fence();return result;
    }
    void project(Vector& x,const Vector& weights) override {
        // Same normalization/sum/subtraction as Host, ordered on the existing
        // stream. Projection has no Host control dependency or scalar download.
        reduce_device({x.data,nullptr,nullptr,x.size,ReductionKind::Maximum},scale_.get());
        reduce_device({x.data,nullptr,weights.data,x.size,ReductionKind::Product,1.,1.,scale_.get()},sum_.get());
        run(ProjectWork{x.size,x.data,scale_.get(),sum_.get()});
    }
    void fence() override {check_cuda(cudaStreamSynchronize(lease_->stream),"complete gravity execution");++counters_.synchronizations;}
    ExecutionCounters counters() const override{return counters_;}
};
class CudaGravityExecution final:public Physical::Gravity::GravityExecution {
    std::shared_ptr<CudaExecution> execution_;
public:
    CudaGravityExecution(cudaStream_t s,int d,std::shared_ptr<void> l):execution_(std::make_shared<CudaExecution>(s,d,std::move(l))){}
    std::shared_ptr<CompositeExecution> numeric() const override{return execution_;}
    void run(const Physical::Gravity::GravityWork& work) override{execution_->launch(work);}
};
}
std::shared_ptr<Physical::Gravity::GravityExecution> make_cuda_gravity_execution(
    cudaStream_t stream,int device,std::shared_ptr<void> lifetime) {
    return std::make_shared<CudaGravityExecution>(stream,device,std::move(lifetime));
}
}
