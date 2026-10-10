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

#include <limits>
#include <type_traits>
#include <vector>

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

using namespace Physical::Gravity;
using namespace Physical::Gravity::ring_boundary_detail;
/** Compact receipts retain original surface indices and every output component. */
struct RingBoundaryReceipt {
    int face=0;double value=0.,lower=0.,upper=0.,far_truncation_upper=0.,far_evaluation_width_upper=0.;
    arch::elliptic::BoundaryPotentialError error;
};
static_assert(std::is_trivially_copyable_v<RingBoundaryReadView>
    &&std::is_trivially_copyable_v<RingBoundaryOutputView>
    &&std::is_trivially_copyable_v<RingBoundaryScalars>
    &&std::is_trivially_copyable_v<ColdRingLeafEvaluator>
    &&std::is_trivially_copyable_v<RingBoundaryReceipt>
    &&std::is_trivially_copyable_v<BoundaryTreeNode>
    &&std::is_trivially_copyable_v<RingMomentEnclosure>
    &&std::is_trivially_copyable_v<RingLeafEdges>
    &&std::is_trivially_copyable_v<RingBoundaryFace>
    &&std::is_trivially_copyable_v<UniformRingQuartet>
    &&std::is_trivially_copyable_v<arch::elliptic::BoundaryPotentialError>);
/** One serial lane owns all original faces, DFS visits and the GLOBAL cap.
 * Only actual Device views/control/scratch enter; Host owners remain outside.
 * Receipt packing is a copy after the same controller, not a second formula.
 */
__global__ void execute_ring_boundary(RingBoundaryReadView input,RingBoundaryControl control,
    RingBoundaryOutputView output,RingBoundaryScalars* scalars,ColdRingLeafEvaluator leaf,
    RingBoundaryReceipt* receipts) {
    if(blockIdx.x||threadIdx.x)return;
    ring_boundary_shared(input,control,output,*scalars,leaf);
    std::size_t surface=0;
    for(std::size_t face=0;face<input.face_count;++face)if(input.faces[face].boundary_side>=0)
        receipts[surface++]={static_cast<int>(face),output.values[face],output.lower[face],output.upper[face],
            output.far_truncation_upper[face],output.far_evaluation_width_upper[face],output.errors[face]};
}
/** Check every element-byte extent before allocation or transfer. */
template<class T> std::size_t ring_bytes(std::size_t count) {
    if(count>std::numeric_limits<std::size_t>::max()/sizeof(T))
        throw std::overflow_error("Ring Device byte extent overflow");
    return count*sizeof(T);
}
/** Reusable resident arrays belong to the existing stream owner. One leaf
 * scratch span serves the complete serial controller, never faces*max_boxes.
 */
struct RingDeviceStorage {
    ReusableDeviceAllocation<BoundaryTreeNode> tree;
    ReusableDeviceAllocation<RingMomentEnclosure> moments;
    ReusableDeviceAllocation<RingLeafEdges> edges;
    ReusableDeviceAllocation<double> density;
    ReusableDeviceAllocation<RingBoundaryFace> faces;
    ReusableDeviceAllocation<UniformRingQuartet> quartets;
    ReusableDeviceAllocation<double> values,lower,upper,far_truncation,far_evaluation;
    ReusableDeviceAllocation<arch::elliptic::BoundaryPotentialError> errors;
    ReusableDeviceAllocation<RingBoundaryScalars> scalars;
    ReusableDeviceAllocation<RingBoundaryReceipt> receipts;
    ReusableDeviceAllocation<finite_ring_detail::RingBox> boxes;
    ReusableDeviceAllocation<finite_ring_detail::RingBoxReductionView::Node> reduction;
};

struct StreamLease {cudaStream_t stream;int device;std::shared_ptr<void> lifetime;};
class CudaExecution final:public CompositeExecution {
    std::shared_ptr<StreamLease> lease_;
    ReusableDeviceAllocation<double> partials_;
    DeviceAllocation<double> result_,scale_,sum_;
    ExecutionCounters counters_;
    RingDeviceStorage ring_;
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
    /** Synchronous typed ring execution after complete Host authentication.
     * Cold Device history retains the accepted raw leaf body; this serial first
     * path may be slow. Compact O(surface) receipts preserve the original Host
     * result contract without downloading the complete interior face arrays.
     */
    void execute_ring(const EvaluateRingBoundary& task) {
        if(!task.result)throw std::invalid_argument("Missing ring work output");
        *task.result=RingBoundaryEvaluation{};
        if(!task.source||!task.op||!task.identity||!task.control)
            throw std::invalid_argument("Incomplete ring work descriptor");
        const auto packet=task.source->prepare_ring_boundary_inputs(*task.op,*task.identity,*task.control);
        const auto host=packet.view();
        const std::size_t tree_count=static_cast<std::size_t>(host.node_count);
        const std::size_t cell_count=static_cast<std::size_t>(host.cell_count);
        const std::size_t face_count=host.face_count,surface_count=packet.surface_faces.size();
        const std::size_t box_count=static_cast<std::size_t>(packet.control.maximum_boxes_per_leaf);
        const std::size_t capacity=finite_ring_detail::ring_reduction_capacity(box_count);
        if(!capacity||capacity>std::numeric_limits<std::size_t>::max()/2
            ||surface_count==0||surface_count>face_count)
            throw std::overflow_error("Invalid ring Device storage shape");
        const std::size_t reduction_count=2*capacity;
        const auto tree_bytes=ring_bytes<BoundaryTreeNode>(tree_count);
        const auto moment_bytes=ring_bytes<RingMomentEnclosure>(tree_count);
        const auto edge_bytes=ring_bytes<RingLeafEdges>(cell_count);
        const auto density_bytes=ring_bytes<double>(cell_count);
        const auto face_bytes=ring_bytes<RingBoundaryFace>(face_count);
        const auto quartet_bytes=ring_bytes<UniformRingQuartet>(tree_count);
        (void)ring_bytes<double>(face_count);
        (void)ring_bytes<arch::elliptic::BoundaryPotentialError>(face_count);
        const auto scalar_bytes=ring_bytes<RingBoundaryScalars>(1);
        const auto receipt_bytes=ring_bytes<RingBoundaryReceipt>(surface_count);
        (void)ring_bytes<finite_ring_detail::RingBox>(box_count);
        (void)ring_bytes<finite_ring_detail::RingBoxReductionView::Node>(reduction_count);
        // Check cumulative transfer/counter extents before the first CUDA call.
        const auto checked_sum=[](std::size_t a,std::size_t b) {
            if(b>std::numeric_limits<std::size_t>::max()-a)
                throw std::overflow_error("Ring Device counter extent overflow");
            return a+b;
        };
        auto upload_bytes=checked_sum(tree_bytes,moment_bytes);
        upload_bytes=checked_sum(upload_bytes,edge_bytes);upload_bytes=checked_sum(upload_bytes,density_bytes);
        upload_bytes=checked_sum(upload_bytes,face_bytes);upload_bytes=checked_sum(upload_bytes,quartet_bytes);
        (void)checked_sum(counters_.bytes_h2d,upload_bytes);
        (void)checked_sum(counters_.bytes_d2h,checked_sum(scalar_bytes,receipt_bytes));
        (void)checked_sum(counters_.kernels,1);
        (void)checked_sum(counters_.synchronizations,9); // start, six uploads, finish, failure join
        RingBoundaryEvaluation completed;completed.source=packet.source;
        completed.values.assign(face_count,0.);completed.lower.assign(face_count,0.);completed.upper.assign(face_count,0.);
        completed.far_truncation_upper.assign(face_count,0.);completed.far_evaluation_width_upper.assign(face_count,0.);
        completed.errors.resize(face_count);
        RingBoundaryScalars scalars;
        std::vector<RingBoundaryReceipt> receipts(surface_count);
        // Incomplete/stale/overflow/Host-allocation failures above do no CUDA work.
        check_cuda(cudaSetDevice(lease_->device),"select ring gravity device");
        fence(); // Quiesce actual users before growth/reuse of stream-owned arrays.
        ring_.tree.reserve(tree_count);ring_.moments.reserve(tree_count);ring_.edges.reserve(cell_count);
        ring_.density.reserve(cell_count);ring_.faces.reserve(face_count);ring_.quartets.reserve(tree_count);
        ring_.values.reserve(face_count);ring_.lower.reserve(face_count);ring_.upper.reserve(face_count);
        ring_.far_truncation.reserve(face_count);ring_.far_evaluation.reserve(face_count);ring_.errors.reserve(face_count);
        ring_.scalars.reserve(1);ring_.receipts.reserve(surface_count);
        ring_.boxes.reserve(box_count);ring_.reduction.reserve(reduction_count);
        const RingBoundaryReadView input{ring_.tree.get(),host.node_count,ring_.moments.get(),ring_.edges.get(),
            ring_.density.get(),host.cell_count,ring_.faces.get(),face_count,ring_.quartets.get(),
            host.source_generation,host.gravitational_constant};
        const RingBoundaryOutputView output{ring_.values.get(),ring_.lower.get(),ring_.upper.get(),
            ring_.far_truncation.get(),ring_.far_evaluation.get(),ring_.errors.get(),face_count};
        const ColdRingLeafEvaluator leaf{ring_.boxes.get(),box_count,ring_.reduction.get(),reduction_count};
        try {
            copy(ring_.tree.get(),host.nodes,tree_bytes,Transfer::Upload);
            copy(ring_.moments.get(),host.moment_bounds,moment_bytes,Transfer::Upload);
            copy(ring_.edges.get(),host.edges,edge_bytes,Transfer::Upload);
            copy(ring_.density.get(),host.density,density_bytes,Transfer::Upload);
            copy(ring_.faces.get(),host.faces,face_bytes,Transfer::Upload);
            copy(ring_.quartets.get(),host.quartets,quartet_bytes,Transfer::Upload);
            execute_ring_boundary<<<1,1,0,lease_->stream>>>(input,packet.control,output,ring_.scalars.get(),leaf,ring_.receipts.get());
            check_cuda(cudaGetLastError(),"launch shared GLOBAL ring boundary controller");++counters_.kernels;
            copy(&scalars,ring_.scalars.get(),scalar_bytes,Transfer::Download);
            copy(receipts.data(),ring_.receipts.get(),receipt_bytes,Transfer::Download);
            fence();
        } catch(...) {
            // Keep every borrowed upload/download buffer alive until its join.
            try {fence();}catch(...) {std::terminate();}
            throw;
        }
        if(scalars.source_generation!=packet.source_generation||scalars.memo_hits||scalars.memo_admissions
            ||(scalars.status!=RingBoundaryStatus::Bounded&&scalars.status!=RingBoundaryStatus::WorkLimit
                &&scalars.status!=RingBoundaryStatus::PrecisionLimit))
            throw std::logic_error("Invalid ring Device source/completion receipt");
        static_cast<RingBoundaryScalars&>(completed)=scalars;
        const auto quality=scalars.status==RingBoundaryStatus::Bounded
            ?arch::elliptic::BoundaryErrorQuality::CertifiedAbsolute:arch::elliptic::BoundaryErrorQuality::Unknown;
        for(auto& error:completed.errors)error.quality=quality;
        for(std::size_t surface=0;surface<surface_count;++surface) {
            const auto& receipt=receipts[surface];
            if(receipt.face!=packet.surface_faces[surface]||receipt.error.quality!=quality)
                throw std::logic_error("Invalid ring Device surface receipt order/quality");
            const auto face=static_cast<std::size_t>(receipt.face);
            completed.values[face]=receipt.value;completed.lower[face]=receipt.lower;completed.upper[face]=receipt.upper;
            completed.far_truncation_upper[face]=receipt.far_truncation_upper;
            completed.far_evaluation_width_upper[face]=receipt.far_evaluation_width_upper;
            completed.errors[face]=receipt.error;
        }
        *task.result=std::move(completed); // One publication, after actual completion and receipt checks.
    }
    /** Visit a shared work descriptor and launch its scalar kernel. */
    template<class Variant> void launch(const Variant& work) {
        std::visit([&](auto task){
            using Task=std::decay_t<decltype(task)>;
            if constexpr(std::is_same_v<Task,Physical::Gravity::EvaluateRingBoundary>) {
                execute_ring(task);
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
