/**
 * @file CudaGravityExecution.cu
 * @brief Launch shared gravity arithmetic and reductions on the backend stream.
 *
 * Workflow:
 * 1. Use the selected CUDA device, stream and backend state lease.
 * 2. Launch shared gravity arithmetic and reductions on the backend stream.
 * 3. Publish or retire resident results only after the required stream ordering.
 */

#include "cuda/runtime/gravity/CudaGravityExecution.h"

#include "cuda/common/DeviceAllocation.h"

namespace arch::cuda {
namespace {
using namespace arch::multigrid;
/** Apply one shared arithmetic task per resident index. */
template<class Task> __global__ void execute_work(Task task) {
    const int i=blockIdx.x*blockDim.x+threadIdx.x;if(i<task.size)task(i);
}
/** Form per-chunk compensated scalar partials on the gravity stream. */
__global__ void reduce_chunks(Reduction task,double* partials,int count) {
    const int i=blockIdx.x*blockDim.x+threadIdx.x;if(i<count)partials[i]=task.partial(i);
}
/** Finish a shared scalar reduction without duplicating its formula. */
__global__ void reduce_finish(Reduction task,const double* partials,int count,double* result) {
    *result=task.finish(partials,count);
}
// The original partial reduction launches 128 threads. When all partials fit
// that same block, block barriers can replace inter-kernel stream ordering.
constexpr int reduction_threads = 128;

/** Finish a small reduction in one block without changing chunk/order/math. */
__global__ void reduce_one_block(Reduction task, int count, double* result) {
    __shared__ double partials[reduction_threads];
    const int lane = threadIdx.x;
    if (lane < count) partials[lane] = task.partial(lane);
    __syncthreads();
    if (lane == 0) *result = task.finish(partials, count);
}

/** Project a small periodic vector using the original three shared leaves.
 * Workflow: reduce max|x|, reduce the normalized weighted mean, then subtract
 * that mean. All partials retain the original 64-element compensated order;
 * barriers publish each scalar before its dependent work starts. No host
 * transfer or alternative nullspace formula is introduced.
 */
__global__ void project_one_block(double* values, const double* weights,
                                  int size, int count) {
    __shared__ double partials[reduction_threads], scale, sum;
    const int lane = threadIdx.x;
    const Reduction maximum{values,nullptr,nullptr,size,ReductionKind::Maximum};
    if (lane < count) partials[lane] = maximum.partial(lane);
    __syncthreads();
    if (lane == 0) scale = maximum.finish(partials,count);
    __syncthreads();
    const Reduction mean{values,nullptr,weights,size,ReductionKind::Product,1.,1.,&scale};
    if (lane < count) partials[lane] = mean.partial(lane);
    __syncthreads();
    if (lane == 0) sum = mean.finish(partials,count);
    __syncthreads();
    const ProjectWork project{size,values,&scale,&sum};
    for (int cell = lane; cell < size; cell += blockDim.x) project(cell);
}

struct StreamLease {cudaStream_t stream;int device;std::shared_ptr<void> lifetime;};
class CudaExecution final:public CompositeExecution {
    std::shared_ptr<StreamLease> lease_;
    ReusableDeviceAllocation<double> partials_;
    DeviceAllocation<double> result_,scale_,sum_;
    ExecutionCounters counters_;
public:
    /** Bind one device and stream and allocate reusable scalar reductions. */
    CudaExecution(cudaStream_t stream,int device,std::shared_ptr<void> lifetime)
        :lease_(std::make_shared<StreamLease>(StreamLease{stream,device,std::move(lifetime)})) {
        check_cuda(cudaSetDevice(device),"select gravity device");result_.allocate(1);scale_.allocate(1);sum_.allocate(1);
    }
    /** Quiesce the leased stream before releasing device arrays. */
    ~CudaExecution() override {set_device_and_quiesce_or_terminate(lease_->device,lease_->stream);}
    /** Identify this numerical executor as device-resident. */
    bool device() const override{return true;}
    /** Allocate stream-leased device storage for a shared work vector. */
    std::shared_ptr<void> allocate(std::size_t bytes) override {
        if(!bytes)return {};
        check_cuda(cudaSetDevice(lease_->device),"select gravity device");
        auto allocation=std::make_shared<DeviceAllocation<std::byte>>();allocation->allocate(bytes);
        return {allocation->get(),[allocation,lease=lease_](void*)mutable {
            set_device_and_quiesce_or_terminate(lease->device,lease->stream);allocation.reset();
        }};
    }
    /** Queue an explicit host/device or device/device transfer on the leased stream. */
    void copy(void* to,const void* from,std::size_t bytes,Transfer transfer) override {
        const auto kind=transfer==Transfer::Upload?cudaMemcpyHostToDevice:transfer==Transfer::Download?cudaMemcpyDeviceToHost:cudaMemcpyDeviceToDevice;
        check_cuda(cudaMemcpyAsync(to,from,bytes,kind,lease_->stream),"copy gravity storage");
        if(transfer==Transfer::Upload){counters_.bytes_h2d+=bytes;fence();} // borrowed host input lifetime
        if(transfer==Transfer::Download)counters_.bytes_d2h+=bytes;
    }
    /** Visit a shared work descriptor and launch its scalar kernel. */
    template<class Variant> void launch(const Variant& work) {
        std::visit([&](auto task){
            using Task=std::decay_t<decltype(task)>;
            if constexpr(std::is_same_v<Task,Physical::Gravity::EvaluateRingBoundary>) {
                // The result is a Host-owned control object, never device storage.
                // Retire any old certificate even when this provider declines.
                if(task.result)*task.result=Physical::Gravity::RingBoundaryEvaluation{};
                // No Host fallback or device launch before CPU qualification.
                throw std::logic_error("CUDA finite-ring boundary execution is not qualified");
            } else {
                if constexpr(std::is_same_v<Task,Physical::Gravity::EvaluateBoundary>)
                    Physical::Gravity::validate_legacy_boundary_work(task);
                if(!task.size)return;
                execute_work<<<(task.size+127)/128,128,0,lease_->stream>>>(task);
                check_cuda(cudaGetLastError(),"launch shared gravity arithmetic");++counters_.kernels;
            }
        },work);
    }
    /** Launch a composite numerical work descriptor. */
    void run(const CompositeWork& work) override {launch(work);}
    /** Queue both reduction passes with reusable partial storage. */
    void reduce_device(const Reduction& task,double* out) {
        const int n=(task.size+Reduction::chunk-1)/Reduction::chunk;
        if (n > 0 && n <= reduction_threads) {
            reduce_one_block<<<1,reduction_threads,0,lease_->stream>>>(task,n,out);
            check_cuda(cudaGetLastError(),"reduce shared gravity scalar in one block");
            ++counters_.kernels;
            return;
        }
        partials_.reserve(n);
        reduce_chunks<<<(n+127)/128,128,0,lease_->stream>>>(task,partials_.get(),n);
        reduce_finish<<<1,1,0,lease_->stream>>>(task,partials_.get(),n,out);
        check_cuda(cudaGetLastError(),"reduce shared gravity scalars");counters_.kernels+=2;
    }
    /** Download a scalar only where host convergence control needs it. */
    double reduce(const Reduction& task) override {
        reduce_device(task,result_.get());
        double result=0.;copy(&result,result_.get(),sizeof(double),Transfer::Download);fence();return result;
    }
    /** Remove a periodic mean entirely on device after two reductions. */
    void project(Vector& x,const Vector& weights) override {
        // Same normalization/sum/subtraction as Host, ordered on the existing
        // stream. Projection has no Host control dependency or scalar download.
        const int n = (x.size + Reduction::chunk - 1) / Reduction::chunk;
        if (n > 0 && n <= reduction_threads) {
            project_one_block<<<1,reduction_threads,0,lease_->stream>>>(x.data,weights.data,x.size,n);
            check_cuda(cudaGetLastError(),"project shared gravity vector in one block");
            ++counters_.kernels;
            return;
        }
        reduce_device({x.data,nullptr,nullptr,x.size,ReductionKind::Maximum},scale_.get());
        reduce_device({x.data,nullptr,weights.data,x.size,ReductionKind::Product,1.,1.,scale_.get()},sum_.get());
        run(ProjectWork{x.size,x.data,scale_.get(),sum_.get()});
    }
    /** Wait for all work on the leased gravity stream. */
    void fence() override {check_cuda(cudaStreamSynchronize(lease_->stream),"complete gravity execution");++counters_.synchronizations;}
    /** Return transfer, kernel and synchronization evidence. */
    ExecutionCounters counters() const override{return counters_;}
};
class CudaGravityExecution final:public Physical::Gravity::GravityExecution {
    std::shared_ptr<CudaExecution> execution_;
public:
    /** Pair physical gravity tasks with the same numerical stream owner. */
    CudaGravityExecution(cudaStream_t s,int d,std::shared_ptr<void> l):execution_(std::make_shared<CudaExecution>(s,d,std::move(l))){}
    /** Expose the shared numerical execution interface. */
    std::shared_ptr<CompositeExecution> numeric() const override{return execution_;}
    /** Launch a physical gravity task without changing its scalar formula. */
    void run(const Physical::Gravity::GravityWork& work) override{execution_->launch(work);}
};
}
/** Construct the CUDA gravity executor after backend device selection. */
std::shared_ptr<Physical::Gravity::GravityExecution> make_cuda_gravity_execution(
    cudaStream_t stream,int device,std::shared_ptr<void> lifetime) {
    return std::make_shared<CudaGravityExecution>(stream,device,std::move(lifetime));
}
}
