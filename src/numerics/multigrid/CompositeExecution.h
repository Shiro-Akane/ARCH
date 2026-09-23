/** Backend storage and loop execution for the shared composite solver.
 * Work descriptors contain the ONLY scalar arithmetic used by both backends.
 */
#pragma once
#include "core/CompensatedSum.h"
#include "numerics/elliptic/CompositePoisson.h"
#include <memory>
#include <span>
#include <variant>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <limits>
namespace arch::multigrid {
template<class T> struct Array {
    std::shared_ptr<void> owner;
    T* data=nullptr;
    int size=0;
};
using Vector=Array<double>;
struct LinearWork {
    int size; double* out; const double* x; const double* y;
    double a=1.,b=0.,c=0.;
    ARCH_INLINE void operator()(int i) const { out[i]=(x?a*x[i]:0.)+(y?b*y[i]:0.)+c; }
};
struct SparseView { const int* offsets; const int* columns; const double* values; };
struct RowsWork {
    int size; SparseView rows; const double* x; double* out; double alpha=1.,beta=0.;
    ARCH_INLINE void operator()(int i) const {
        math::CompensatedSum sum;
        for(int k=rows.offsets[i];k<rows.offsets[i+1];++k) sum.add(rows.values[k]*x[rows.columns[k]]);
        out[i]=alpha*sum.value()+(beta?beta*out[i]:0.);
    }
};
struct FaceView { SparseView stencil; const int* anchor; const double* boundary; };
struct GradientWork {
    int size; FaceView faces; const double* x; const double* boundary; double* out;
    ARCH_INLINE void operator()(int f) const {
        const int start=faces.stencil.offsets[f];
        out[f]=elliptic::composite_face_gradient(x,faces.anchor[f],faces.stencil.columns+start,
            faces.stencil.values+start,faces.stencil.offsets[f+1]-start,faces.boundary[f],boundary?boundary[f]:0.);
    }
};
struct JacobiWork {
    int size; double* u; const double* rhs; const double* applied; const double* diagonal;
    ARCH_INLINE void operator()(int i) const { u[i]+=0.6*(rhs[i]-applied[i])/diagonal[i]; }
};
struct ProjectWork {
    int size;double* x;const double* scale;const double* sum;
    ARCH_INLINE void operator()(int i) const {x[i]-=(*scale)*(*sum);}
};
using CompositeWork=std::variant<LinearWork,RowsWork,GradientWork,JacobiWork,ProjectWork>;
enum class ReductionKind { Maximum, Product };
struct Reduction {
    const double* x; const double* y; const double* weights; int size;
    ReductionKind kind; double scale_x=1.,scale_y=1.;
    const double* normalization=nullptr;
    static constexpr int chunk=64;
    ARCH_INLINE double partial(int block) const {
        math::CompensatedSum sum; double maximum=0.;
        const double normal=normalization?(*normalization==0.?1.:*normalization):scale_x;
        const int end=(block+1)*chunk<size?(block+1)*chunk:size;
        for(int i=block*chunk;i<end;++i) {
            if(!std::isfinite(x[i]) || (y && !std::isfinite(y[i]))) return std::numeric_limits<double>::infinity();
            if(kind==ReductionKind::Maximum) { const double v=std::abs(x[i]); if(v>maximum) maximum=v; }
            else sum.add((weights?weights[i]:1.)*(x[i]/normal)*(y?y[i]/scale_y:1.));
        }
        return kind==ReductionKind::Maximum?maximum:sum.value();
    }
    ARCH_INLINE double finish(const double* partials,int count) const {
        math::CompensatedSum sum; double maximum=0.;
        for(int i=0;i<count;++i) {
            if(!std::isfinite(partials[i])) return std::numeric_limits<double>::infinity();
            if(kind==ReductionKind::Maximum) { if(partials[i]>maximum) maximum=partials[i]; }
            else sum.add(partials[i]);
        }
        return kind==ReductionKind::Maximum?maximum:sum.value();
    }
};
enum class Transfer { Upload, Download, Local };
struct ExecutionCounters { std::size_t kernels=0,bytes_h2d=0,bytes_d2h=0,synchronizations=0; };
class CompositeExecution {
public:
    virtual ~CompositeExecution()=default;
    virtual bool device() const=0;
    virtual std::shared_ptr<void> allocate(std::size_t bytes)=0;
    virtual void copy(void* to,const void* from,std::size_t bytes,Transfer)=0;
    virtual void run(const CompositeWork&)=0;
    virtual double reduce(const Reduction&)=0;
    virtual void fence()=0;
    virtual void project(Vector& x,const Vector& weights)=0;
    virtual ExecutionCounters counters() const { return {}; }
    template<class T> Array<T> array(int size) {
        if(size<0) throw std::invalid_argument("Negative execution array size");
        auto owner=allocate(sizeof(T)*static_cast<std::size_t>(size));
        auto* pointer=static_cast<T*>(owner.get()); return {std::move(owner),pointer,size};
    }
    template<class T> Array<T> upload(std::span<const T> source) {
        auto result=array<T>(source.size());
        if(!source.empty()) copy(result.data,source.data(),source.size_bytes(),Transfer::Upload);
        return result;
    }
    template<class T> Array<T> upload(const std::vector<T>& source) { return upload<T>(std::span<const T>(source)); }
    template<class T> std::vector<T> download(const Array<T>& source) {
        std::vector<T> result(source.size);
        if(source.size) copy(result.data(),source.data,sizeof(T)*source.size,Transfer::Download);
        fence(); return result;
    }
    void linear(Vector& out,double a,const Vector& x,double b=0.,const Vector& y={},double c=0.) {
        run(LinearWork{out.size,out.data,x.data,y.data,a,b,c});
    }
    void fill(Vector& out,double value=0.) { linear(out,0.,{},0.,{},value); }
    double maximum(const Vector& x) { return reduce({x.data,nullptr,nullptr,x.size,ReductionKind::Maximum}); }
};
std::shared_ptr<CompositeExecution> make_host_composite_execution();
struct SparseStorage {
    std::vector<int> offsets{0},columns; std::vector<double> values;
    void row(std::span<const int> c,std::span<const double> v) {
        columns.insert(columns.end(),c.begin(),c.end()); values.insert(values.end(),v.begin(),v.end());
        offsets.push_back(columns.size());
    }
};
struct SparseArray {
    Array<int> offsets,columns; Vector values;
    SparseView view() const { return {offsets.data,columns.data,values.data}; }
    SparseArray()=default;
    SparseArray(CompositeExecution& e,const SparseStorage& s)
        :offsets(e.upload(s.offsets)),columns(e.upload(s.columns)),values(e.upload(s.values)){}
};
}
