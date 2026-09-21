#include "numerics/multigrid/HostCompositeMG.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace arch::multigrid {
using elliptic::CompositeCell;
using elliptic::CompositeCellHash;
using elliptic::CompositePoisson;
HostCompositeMG::Level::Level(CompositePoisson value) : op(std::move(value)),
    u(op.size()), rhs(op.size()), residual(op.size()), scratch(op.size()) {}
HostCompositeMG::HostCompositeMG(elliptic::CartesianMesh base,std::vector<CompositeCell> cells) {
    levels_.emplace_back(CompositePoisson(base,std::move(cells)));
    for (;;) {
        const auto& fine=levels_.back().op;
        const int maximum=fine.max_level();
        auto coarse_base=fine.base();
        if (!maximum) {
            if (*std::min_element(coarse_base.cells.begin(),coarse_base.cells.begin()+base.dimension)<=4) break;
            for (int a=0;a<base.dimension;++a) { coarse_base.cells[a]/=2; coarse_base.spacing[a]*=2.; }
        }
        std::unordered_map<CompositeCell,int,CompositeCellHash> lookup;
        std::vector<CompositeCell> coarse;
        std::vector<int> parents;
        for (auto cell:fine.cells()) {
            if (cell.level==maximum) {
                if (maximum) --cell.level;
                for (int a=0;a<base.dimension;++a) cell.index[a]/=2;
            }
            auto [entry,added]=lookup.emplace(cell,static_cast<int>(coarse.size()));
            if (added) coarse.push_back(cell);
            parents.push_back(entry->second);
        }
        levels_.back().parent=std::move(parents);
        levels_.emplace_back(CompositePoisson(coarse_base,std::move(coarse)));
        const auto& child=levels_[levels_.size()-2]; const auto& parent=levels_.back();
        std::vector<double> volume(parent.op.size());
        for(int cell=0;cell<child.op.size();++cell) volume[child.parent[cell]]+=child.op.volumes()[cell];
        for(int cell=0;cell<parent.op.size();++cell)
            if (std::abs(volume[cell]-parent.op.volumes()[cell])>32.*std::numeric_limits<double>::epsilon()*parent.op.volumes()[cell])
                throw std::logic_error("Composite restriction does not preserve cell volume");
    }
    bottom_=std::make_unique<HostMultigrid>(levels_.back().op.base(),elliptic::BoundaryKind::Periodic);
}
void HostCompositeMG::smooth(Level& l) {
    constexpr int sweeps=3;
    constexpr double weight=0.6;
    for (int s=0;s<sweeps;++s) {
        l.op.apply(l.u,l.scratch);
        for (int i=0;i<l.op.size();++i) l.u[i]+=weight*(l.rhs[i]-l.scratch[i])/l.op.diagonal()[i];
        l.op.project(l.u);
    }
}
void HostCompositeMG::cycle(std::size_t index) {
    auto& fine=levels_[index];
    if (index+1==levels_.size()) {
        std::vector<double> rhs(fine.op.size());
        for (int i=0;i<fine.op.size();++i) rhs[fine.op.base().index(fine.op.cells()[i].index)]=fine.rhs[i];
        elliptic::project_mean(rhs);
        const double norm=elliptic::rms(rhs);
        const auto result=bottom_->solve(rhs,{}, {1e-12,std::max(norm*1e-14,std::numeric_limits<double>::denorm_min()),50});
        if (result.report.status!=SolveStatus::Converged) throw std::runtime_error("Composite bottom solve failed");
        for (int i=0;i<fine.op.size();++i) fine.u[i]=result.potential[fine.op.base().index(fine.op.cells()[i].index)];
        return;
    }
    smooth(fine);
    fine.op.apply(fine.u,fine.residual);
    for (int i=0;i<fine.op.size();++i) fine.residual[i]=fine.rhs[i]-fine.residual[i];
    fine.op.project(fine.residual);
    auto& coarse=levels_[index+1];
    std::fill(coarse.rhs.begin(),coarse.rhs.end(),0.);
    for (int i=0;i<fine.op.size();++i) {
        const int parent=fine.parent[i];
        coarse.rhs[parent]+=fine.residual[i]*(fine.op.volumes()[i]/coarse.op.volumes()[parent]);
    }
    coarse.op.project(coarse.rhs);
    std::fill(coarse.u.begin(),coarse.u.end(),0.);
    cycle(index+1);
    // Piecewise constant signed correction is conservative. The flexible outer
    // iteration controls the true fine residual; no hydro limiter is involved.
    for (int i=0;i<fine.op.size();++i) fine.u[i]+=coarse.u[fine.parent[i]];
    fine.op.project(fine.u);
    smooth(fine);
}
std::vector<double> HostCompositeMG::precondition(std::span<const double> rhs) {
    auto& fine=levels_.front();
    std::copy(rhs.begin(),rhs.end(),fine.rhs.begin()); fine.op.project(fine.rhs);
    std::fill(fine.u.begin(),fine.u.end(),0.);
    cycle(0);
    return fine.u;
}
SolveResult HostCompositeMG::solve(std::span<const double> rhs,SolveControl control) {
    const auto& a=op();
    elliptic::validate_values(rhs,a.size());
    if (!std::isfinite(control.relative_tolerance) || control.relative_tolerance<0. || control.relative_tolerance>=1. ||
        !std::isfinite(control.absolute_tolerance) || control.absolute_tolerance<0. || control.max_cycles<1 ||
        (control.relative_tolerance==0. && control.absolute_tolerance==0.))
        throw std::invalid_argument("Invalid composite convergence controls");
    SolveResult result;
    auto& report=result.report;
    report.rhs_rms=a.norm(rhs); report.removed_rhs_mean=a.mean(rhs);
    if (report.rhs_rms!=0. && std::abs(report.removed_rhs_mean/report.rhs_rms)>64.*std::numeric_limits<double>::epsilon())
        throw std::invalid_argument("Incompatible periodic composite RHS");
    std::vector<double> source(rhs.begin(),rhs.end()); a.project(source);
    const double scale=a.norm(source);
    report.rhs_rms=scale;
    report.target=std::max(control.absolute_tolerance,control.relative_tolerance*scale);
    report.initial_residual=report.residual=scale;
    if (scale<=report.target) {
        report.status=SolveStatus::Converged; result.potential.assign(a.size(),0.); return result;
    }
    for (double& v:source) v/=scale;
    std::vector<double> x(a.size(),0.), residual=source, work(a.size());
    const double target=report.target/scale;
    constexpr int restart=20;
    for (;;) {
        a.apply(x,work);
        for (int i=0;i<a.size();++i) residual[i]=source[i]-work[i];
        const double norm=a.norm(residual);
        report.residual=norm*scale;
        if (!std::isfinite(norm)) return result;
        if (norm<=target) {
            result.potential=std::move(x);
            for (double& v:result.potential) v*=scale;
            a.project(result.potential);
            // Scaling back to physical units is also part of acceptance.
            a.apply(result.potential,work);
            for (int i=0;i<a.size();++i) work[i]=rhs[i]-report.removed_rhs_mean-work[i];
            report.residual=a.norm(work);
            if (std::isfinite(report.residual) && report.residual<=report.target) report.status=SolveStatus::Converged;
            else result.potential.clear();
            return result;
        }
        if (report.cycles>=control.max_cycles) { report.status=SolveStatus::MaxCycles; return result; }
        a.project(residual);
        const double beta=a.norm(residual);
        if (!std::isfinite(beta) || beta==0.) return result;
        std::vector<std::vector<double>> basis(1,std::move(residual)), correction;
        for (double& v:basis[0]) v/=beta;
        double h[restart+1][restart]{}, cosine[restart]{}, sine[restart]{}, g[restart+1]{};
        g[0]=beta;
        int count=0;
        for (int j=0;j<restart && report.cycles<control.max_cycles;++j) {
            ++report.cycles;
            try { correction.push_back(precondition(basis[j])); }
            catch (const std::runtime_error&) { return result; }
            a.apply(correction[j],work); a.project(work);
            // Reorthogonalization avoids a false small residual at tight tolerances.
            for (int pass=0;pass<2;++pass) for (int i=0;i<=j;++i) {
                const double value=a.dot(work,basis[i]); h[i][j]+=value;
                for (int k=0;k<a.size();++k) work[k]-=value*basis[i][k];
            }
            h[j+1][j]=a.norm(work);
            const double next=h[j+1][j];
            if (!std::isfinite(next)) return result;
            for (int i=0;i<j;++i) {
                const double first=cosine[i]*h[i][j]+sine[i]*h[i+1][j];
                h[i+1][j]=-sine[i]*h[i][j]+cosine[i]*h[i+1][j]; h[i][j]=first;
            }
            const double length=std::hypot(h[j][j],h[j+1][j]);
            if (!std::isfinite(length) || length==0.) return result;
            cosine[j]=h[j][j]/length; sine[j]=h[j+1][j]/length;
            h[j][j]=length; h[j+1][j]=0.;
            g[j+1]=-sine[j]*g[j]; g[j]*=cosine[j]; count=j+1;
            if (std::abs(g[j+1])<=0.5*target || next==0.) break;
            basis.push_back(work);
            for (double& v:basis.back()) v/=next;
        }
        double y[restart]{};
        for (int i=count-1;i>=0;--i) {
            y[i]=g[i];
            for (int j=i+1;j<count;++j) y[i]-=h[i][j]*y[j];
            y[i]/=h[i][i];
            for (int k=0;k<a.size();++k) x[k]+=y[i]*correction[i][k];
        }
        a.project(x); residual.resize(a.size());
    }
}
} // namespace arch::multigrid
