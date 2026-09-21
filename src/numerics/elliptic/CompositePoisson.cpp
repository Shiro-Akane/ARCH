#include "numerics/elliptic/CompositePoisson.h"
#include "numerics/linalg/DenseWrap.h"
#include "core/CompensatedSum.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace arch::elliptic {
std::size_t CompositeCellHash::operator()(const CompositeCell& c) const noexcept {
    std::size_t h = static_cast<std::size_t>(c.level);
    for (int i : c.index) h ^= static_cast<std::size_t>(i) + 0x9e3779b9u + (h<<6) + (h>>2);
    return h;
}
double CompositePoisson::width(int cell, int axis) const {
    return std::ldexp(base_.spacing[axis],-cells_[cell].level);
}
std::array<double,3> CompositePoisson::center(int cell) const {
    std::array<double,3> p{};
    for (int a=0;a<base_.dimension;++a) p[a]=base_.origin[a]+(cells_[cell].index[a]+0.5)*width(cell,a);
    return p;
}
CompositePoisson::CompositePoisson(CartesianMesh base, std::vector<CompositeCell> cells)
    : base_(base), cells_(std::move(cells)) {
    validate_mesh(base_);
    if (cells_.empty() || cells_.size()>static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("Composite mesh has invalid cell count");
    double base_volume=1.;
    for (int a=0;a<base_.dimension;++a) base_volume*=base_.spacing[a];
    if (!std::isfinite(base_volume) || base_volume<=0.) throw std::invalid_argument("Composite cell volume out of range");
    arch::math::CompensatedSum covered;
    for (int i=0;i<size();++i) {
        const auto& c=cells_[i];
        if (c.level<0 || c.level>15) throw std::invalid_argument("Composite refinement level out of range");
        for (int a=0;a<3;++a) {
            const auto extent=static_cast<long long>(base_.cells[a]) << (a<base_.dimension ? c.level : 0);
            if (extent>std::numeric_limits<int>::max() || c.index[a]<0 || c.index[a]>=extent)
                throw std::invalid_argument("Composite logical coordinate out of range");
        }
        if (!lookup_.emplace(c,i).second) throw std::invalid_argument("Duplicate composite leaf");
        max_level_=std::max(max_level_,c.level);
        const double fraction=std::ldexp(1.,-base_.dimension*c.level);
        const double volume=base_volume*fraction;
        if (!std::isfinite(volume) || volume<=0.) throw std::invalid_argument("Composite leaf volume out of range");
        volumes_.push_back(volume);
        weights_.push_back(fraction/base_.size());
        covered.add(fraction);
    }
    // Dyadic volumes are exactly representable; overlap and holes are rejected.
    if (covered.value()!=base_.size()) throw std::invalid_argument("Composite leaves do not cover the domain");
    for (const auto& cell:cells_) {
        auto parent=cell;
        while (parent.level>0) {
            --parent.level;
            for (int a=0;a<base_.dimension;++a) parent.index[a]/=2;
            if (lookup_.contains(parent)) throw std::invalid_argument("Composite leaves overlap an ancestor");
        }
    }
    build_faces();
}
int CompositePoisson::locate(std::array<double,3> point) const {
    for (int a=0;a<base_.dimension;++a) {
        point[a]=std::fmod(point[a],base_.cells[a]);
        if (point[a]<0.) point[a]+=base_.cells[a];
    }
    for (int level=0;level<=max_level_;++level) {
        CompositeCell cell; cell.level=level;
        for (int a=0;a<base_.dimension;++a) cell.index[a]=static_cast<int>(std::floor(std::ldexp(point[a],level)));
        const auto found=lookup_.find(cell);
        if (found!=lookup_.end()) return found->second;
    }
    throw std::invalid_argument("Composite mesh contains a hole");
}
void CompositePoisson::build_faces() {
    neighbors_.resize(size());
    for (int i=0;i<size();++i) for (int a=0;a<base_.dimension;++a) {
        const auto& c=cells_[i];
        const double unit=std::ldexp(1.,-c.level);
        std::array<double,3> point{};
        for (int t=0;t<base_.dimension;++t) point[t]=(c.index[t]+0.5)*unit;
        point[a]=(c.index[a]+1.125)*unit;
        const int adjacent=locate(point);
        if (std::abs(c.level-cells_[adjacent].level)>1) throw std::invalid_argument("Composite faces require 2:1 balance");
        const int pieces=cells_[adjacent].level>c.level ? (1<<(base_.dimension-1)) : 1;
        for (int piece=0;piece<pieces;++piece) {
            auto sample=point;
            int bit=0;
            for (int t=0;t<base_.dimension;++t) if (t!=a) {
                if (pieces>1) sample[t]=(c.index[t]+(((piece>>bit)&1) ? 0.75 : 0.25))*unit;
                ++bit;
            }
            const int j=locate(sample);
            if (std::abs(c.level-cells_[j].level)>1) throw std::invalid_argument("Composite face is not balanced");
            CompositeFace face; face.left=i; face.right=j; face.axis=a; face.area=1.;
            for (int t=0;t<base_.dimension;++t) {
                face.center[t]=base_.origin[t]+sample[t]*base_.spacing[t];
                if (t!=a) face.area*=std::min(width(i,t),width(j,t));
            }
            face.center[a]=base_.origin[a]+(c.index[a]+1.)*unit*base_.spacing[a];
            const double inverse=1./(0.5*width(i,a)+0.5*width(j,a));
            face.samples={i,j}; face.coefficients={-inverse,inverse};
            faces_.push_back(std::move(face));
            neighbors_[i].push_back(j); neighbors_[j].push_back(i);
        }
    }
    for (auto& neighbors:neighbors_) {
        std::sort(neighbors.begin(),neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(),neighbors.end()),neighbors.end());
    }
    for (auto& face:faces_) if (cells_[face.left].level!=cells_[face.right].level) fit_interface(face);
    diagonal_.assign(size(),0.);
    for (const auto& f:faces_) for (std::size_t k=0;k<f.samples.size();++k) {
        if (f.samples[k]==f.left) diagonal_[f.left]-=f.area*f.coefficients[k]/volumes_[f.left];
        if (f.samples[k]==f.right) diagonal_[f.right]+=f.area*f.coefficients[k]/volumes_[f.right];
    }
    for (double x:diagonal_) if (!std::isfinite(x) || x<=0.) throw std::invalid_argument("Invalid composite diagonal");
}

namespace {
std::array<double,10> polynomial(const std::array<double,3>& x,int dim) {
    std::array<double,10> p{}; p[0]=1.;
    for (int a=0;a<dim;++a) p[1+a]=x[a];
    int slot=1+dim;
    for (int a=0;a<dim;++a) for (int b=a;b<dim;++b) p[slot++]=x[a]*x[b];
    return p;
}
}
void CompositePoisson::fit_interface(CompositeFace& f) const {
    // Minimum weighted correction of the normal two-point gradient, subject
    // to reproduction of every quadratic polynomial. Tangential offsets matter.
    std::vector<int> samples{f.left,f.right};
    for (int depth=0;depth<2;++depth) {
        const auto current=samples;
        for (int i:current) samples.insert(samples.end(),neighbors_[i].begin(),neighbors_[i].end());
        std::sort(samples.begin(),samples.end());
        samples.erase(std::unique(samples.begin(),samples.end()),samples.end());
    }
    const double scale=std::max(width(f.left,f.axis),width(f.right,f.axis));
    const int dim=base_.dimension, terms=1+dim+dim*(dim+1)/2;
    std::vector<std::array<double,10>> basis;
    std::vector<double> weights, initial;
    DenseMatrixData<10> gram;
    double right[10]{}; right[1+f.axis]=1.;
    for (int i:samples) {
        auto delta=center(i);
        double distance=0.;
        for (int a=0;a<dim;++a) {
            const double length=base_.cells[a]*base_.spacing[a];
            delta[a]-=f.center[a];
            delta[a]-=std::floor(delta[a]/length+0.5)*length;
            delta[a]/=scale; distance+=delta[a]*delta[a];
        }
        const auto p=polynomial(delta,dim);
        const double weight=1./((1.+distance)*(1.+distance));
        const double value=i==f.left ? f.coefficients[0]*scale : i==f.right ? f.coefficients[1]*scale : 0.;
        basis.push_back(p); weights.push_back(weight); initial.push_back(value);
        for (int r=0;r<terms;++r) {
            right[r]-=value*p[r];
            for (int c=0;c<terms;++c) gram.data[r][c]+=weight*p[r]*p[c];
        }
    }
    const bool solved=dim==1 ? DenseLUSolver::solve<3,10>(gram,right)
        : dim==2 ? DenseLUSolver::solve<6,10>(gram,right) : DenseLUSolver::solve<10,10>(gram,right);
    if (!solved) throw std::invalid_argument("Degenerate composite interface interpolation");
    f.samples=std::move(samples); f.coefficients.resize(f.samples.size());
    double sum=0.; std::size_t anchor=0;
    for (std::size_t i=0;i<f.samples.size();++i) {
        double value=initial[i];
        for (int r=0;r<terms;++r) value+=weights[i]*basis[i][r]*right[r];
        f.coefficients[i]=value/scale;
        if (f.samples[i]==f.left) anchor=i; else sum+=f.coefficients[i];
    }
    f.coefficients[anchor]=-sum; // Preserve the exact constant nullspace.
}

double CompositePoisson::face_gradient(std::span<const double> x,const CompositeFace& f) const {
    arch::math::CompensatedSum sum;
    for (std::size_t k=0;k<f.samples.size();++k)
        sum.add(f.coefficients[k]*(x[f.samples[k]]-x[f.left]));
    return sum.value();
}
void CompositePoisson::apply(std::span<const double> x,std::span<double> out) const {
    if (x.size()!=cells_.size() || out.size()!=cells_.size() || x.data()==out.data())
        throw std::invalid_argument("Composite apply requires distinct matching arrays");
    std::fill(out.begin(),out.end(),0.);
    for (const auto& f:faces_) {
        const double flux=f.area*face_gradient(x,f);
        out[f.left]-=flux/volumes_[f.left];
        out[f.right]+=flux/volumes_[f.right];
    }
}
double CompositePoisson::mean(std::span<const double> x) const {
    if (x.size()!=cells_.size()) throw std::invalid_argument("Composite mean extent mismatch");
    double scale=0.;
    for (double v:x) { if (!std::isfinite(v)) return std::numeric_limits<double>::infinity(); scale=std::max(scale,std::abs(v)); }
    if (scale==0.) return 0.;
    arch::math::CompensatedSum sum;
    for (int i=0;i<size();++i) sum.add(weights_[i]*(x[i]/scale));
    return scale*sum.value();
}
double CompositePoisson::dot(std::span<const double> x,std::span<const double> y) const {
    if (x.size()!=cells_.size() || y.size()!=cells_.size()) throw std::invalid_argument("Composite dot extent mismatch");
    arch::math::CompensatedSum sum;
    for (int i=0;i<size();++i) sum.add(weights_[i]*x[i]*y[i]);
    return sum.value();
}
double CompositePoisson::norm(std::span<const double> x) const {
    if (x.size()!=cells_.size()) throw std::invalid_argument("Composite norm extent mismatch");
    double scale=0.;
    for (double v:x) { if (!std::isfinite(v)) return std::numeric_limits<double>::infinity(); scale=std::max(scale,std::abs(v)); }
    if (scale==0.) return 0.;
    arch::math::CompensatedSum sum;
    for (int i=0;i<size();++i) { const double q=x[i]/scale; sum.add(weights_[i]*q*q); }
    return scale*std::sqrt(sum.value());
}
void CompositePoisson::project(std::span<double> x) const {
    const double average=mean(x);
    for (double& v:x) v-=average;
}
} // namespace arch::elliptic
