#include "numerics/multigrid/HostCompositeMG.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace arch::multigrid {
using elliptic::CompositeCell;
using elliptic::CompositeCellHash;
using elliptic::CompositePoisson;
HostCompositeMG::HostCompositeMG(elliptic::CartesianMesh base,std::vector<CompositeCell> cells,
    elliptic::BoundaryKind kind,std::shared_ptr<CompositeExecution> execution)
    :execution_(execution?std::move(execution):make_host_composite_execution()) {
    levels_.emplace_back(CompositePoisson(base,std::move(cells),kind));
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
        levels_.emplace_back(CompositePoisson(coarse_base,std::move(coarse),kind));
        const auto& child=levels_[levels_.size()-2]; const auto& parent=levels_.back();
        std::vector<double> volume(parent.op.size());
        for(int cell=0;cell<child.op.size();++cell) volume[child.parent[cell]]+=child.op.volumes()[cell];
        for(int cell=0;cell<parent.op.size();++cell)
            if (std::abs(volume[cell]-parent.op.volumes()[cell])>32.*std::numeric_limits<double>::epsilon()*parent.op.volumes()[cell])
                throw std::logic_error("Composite restriction does not preserve cell volume");
    }
    setup();
}
void HostCompositeMG::setup() {
    auto& e=*execution_;
    for(auto& l:levels_) {
        const auto& a=l.op;const int n=a.size();
        l.u=e.array<double>(n);l.rhs=e.array<double>(n);l.residual=e.array<double>(n);l.scratch=e.array<double>(n);
        l.face_values=e.array<double>(a.faces().size());l.diagonal=e.upload(a.diagonal());
        auto weights=a.volumes(); math::CompensatedSum volume;for(double v:weights)volume.add(v);
        for(double& v:weights)v/=volume.value();l.weights=e.upload(weights);
        SparseStorage faces,rows;std::vector<int> anchors;std::vector<double> boundary;
        std::vector<std::vector<int>> adjacency(n);std::vector<std::vector<double>> signs(n);
        for(int f=0;f<static_cast<int>(a.faces().size());++f) {
            const auto& face=a.faces()[f];faces.row(face.samples,face.coefficients);
            anchors.push_back(face.left>=0?face.left:face.right);boundary.push_back(face.boundary_coefficient);
            if(face.left>=0){adjacency[face.left].push_back(f);signs[face.left].push_back(-face.area/a.volumes()[face.left]);}
            if(face.right>=0){adjacency[face.right].push_back(f);signs[face.right].push_back(face.area/a.volumes()[face.right]);}
        }
        for(int c=0;c<n;++c)rows.row(adjacency[c],signs[c]);
        l.faces=SparseArray(e,faces);l.rows=SparseArray(e,rows);l.anchors=e.upload(anchors);l.boundary=e.upload(boundary);
    }
    for(std::size_t level=0;level+1<levels_.size();++level) {
        auto& f=levels_[level];const auto& c=levels_[level+1];
        SparseStorage restrict,prolong;std::vector<std::vector<int>> children(c.op.size());
        std::vector<std::vector<double>> weights(c.op.size());
        for(int i=0;i<f.op.size();++i){const int p=f.parent[i];children[p].push_back(i);
            weights[p].push_back(f.op.volumes()[i]/c.op.volumes()[p]);
            const double one=1.;prolong.row({&p,1},{&one,1});}
        for(int i=0;i<c.op.size();++i)restrict.row(children[i],weights[i]);
        f.restriction=SparseArray(e,restrict);f.prolongation=SparseArray(e,prolong);
    }
    // Reuse the validated P2 solve to build a bounded coarse inverse ONCE per
    // topology. Both backends apply exactly these coefficients; no host trips
    // or vendor sparse factorization occur inside a device V-cycle.
    const auto& a=levels_.back().op;const int n=a.size();
    HostMultigrid bottom(a.base(),a.boundary_kind());
    elliptic::BoundaryData boundary;boundary.kind=a.boundary_kind();
    if(boundary.kind==elliptic::BoundaryKind::Dirichlet)
        for(int axis=0;axis<a.base().dimension;++axis)for(int s=0;s<2;++s)
            boundary.values[2*axis+s].assign(n/a.base().cells[axis],0.);
    std::vector<std::vector<double>> inverse(n,std::vector<double>(n));
    for(int col=0;col<n;++col) {
        std::vector<double> rhs(n,0.);rhs[a.base().index(a.cells()[col].index)]=1.;
        if(boundary.kind==elliptic::BoundaryKind::Periodic)elliptic::project_mean(rhs);
        const auto solution=bottom.solve(rhs,boundary,{1e-13,1e-15,100});
        if(solution.report.status!=SolveStatus::Converged)throw std::runtime_error("Composite coarse inverse failed");
        for(int row=0;row<n;++row)inverse[row][col]=solution.potential[a.base().index(a.cells()[row].index)];
    }
    SparseStorage matrix;std::vector<int> indices(n);for(int i=0;i<n;++i)indices[i]=i;
    for(const auto& row:inverse)matrix.row(indices,row);bottom_=SparseArray(e,matrix);
    const int size=op().size();source_=e.array<double>(size);x_=e.array<double>(size);
    residual_=e.array<double>(size);work_=e.array<double>(size);
    for(int i=0;i<21;++i)basis_.push_back(e.array<double>(size));
    for(int i=0;i<20;++i)correction_.push_back(e.array<double>(size));
    e.fence();
}
double HostCompositeMG::mean(const Vector& x,int level) {
    auto& e=*execution_;const double scale=e.maximum(x);if(scale==0.)return 0.;
    return scale*e.reduce({x.data,nullptr,levels_[level].weights.data,x.size,ReductionKind::Product,scale});
}
double HostCompositeMG::dot(const Vector& x,const Vector& y,int level) {
    return execution_->reduce({x.data,y.data,levels_[level].weights.data,x.size,ReductionKind::Product});
}
double HostCompositeMG::norm(const Vector& x,int level) {
    auto& e=*execution_;const double scale=e.maximum(x);if(scale==0.)return 0.;
    return scale*std::sqrt(e.reduce({x.data,x.data,levels_[level].weights.data,x.size,ReductionKind::Product,scale,scale}));
}
void HostCompositeMG::project(Vector& x,int level) {
    if(levels_[level].op.boundary_kind()==elliptic::BoundaryKind::Periodic)
        execution_->project(x,levels_[level].weights);
}
void HostCompositeMG::gradient(const Vector& x,Vector& out,const Vector& boundary) {
    const auto& l=levels_.front();execution_->run(GradientWork{out.size,
        {l.faces.view(),l.anchors.data,l.boundary.data},x.data,boundary.data,out.data});
}
void HostCompositeMG::boundary_rhs(Vector& rhs,const Vector& values) {
    auto& l=levels_.front();execution_->fill(l.scratch);
    gradient(l.scratch,l.face_values,values);
    execution_->run(RowsWork{rhs.size,l.rows.view(),l.face_values.data,rhs.data,-1.,1.});
}
void HostCompositeMG::apply(int level,const Vector& x,Vector& out) {
    auto& l=levels_[level];auto& e=*execution_;
    e.run(GradientWork{l.face_values.size,{l.faces.view(),l.anchors.data,l.boundary.data},x.data,nullptr,l.face_values.data});
    e.run(RowsWork{out.size,l.rows.view(),l.face_values.data,out.data});
}
void HostCompositeMG::smooth(int level) {
    auto& l=levels_[level];
    for(int s=0;s<3;++s){apply(level,l.u,l.scratch);
        execution_->run(JacobiWork{l.u.size,l.u.data,l.rhs.data,l.scratch.data,l.diagonal.data});project(l.u,level);}
}
void HostCompositeMG::cycle(int level) {
    auto& f=levels_[level];auto& e=*execution_;
    if(level+1==static_cast<int>(levels_.size())) {
        e.run(RowsWork{f.u.size,bottom_.view(),f.rhs.data,f.u.data});project(f.u,level);return;
    }
    smooth(level);apply(level,f.u,f.residual);e.linear(f.residual,1.,f.rhs,-1.,f.residual);project(f.residual,level);
    auto& c=levels_[level+1];
    e.run(RowsWork{c.rhs.size,f.restriction.view(),f.residual.data,c.rhs.data});project(c.rhs,level+1);
    e.fill(c.u);cycle(level+1);
    e.run(RowsWork{f.u.size,f.prolongation.view(),c.u.data,f.u.data,1.,1.});project(f.u,level);smooth(level);
}
void HostCompositeMG::precondition(const Vector& rhs,Vector& out) {
    auto& f=levels_.front();auto& e=*execution_;
    e.linear(f.rhs,1.,rhs);project(f.rhs);e.fill(f.u);cycle(0);e.linear(out,1.,f.u);
}
SolveResult HostCompositeMG::solve(std::span<const double> rhs,SolveControl control) {
    elliptic::validate_values(rhs,op().size());auto input=execution_->upload(rhs);
    SolveResult result;result.report=solve(input,control);
    if(result.report.status==SolveStatus::Converged)result.potential=execution_->download(x_);
    return result;
}
SolveReport HostCompositeMG::solve(const Vector& rhs,SolveControl control) {
    if(rhs.size!=op().size())throw std::invalid_argument("Composite source extent mismatch");
    if(!std::isfinite(control.relative_tolerance)||control.relative_tolerance<0.||control.relative_tolerance>=1.||
       !std::isfinite(control.absolute_tolerance)||control.absolute_tolerance<0.||control.max_cycles<1||
       (control.relative_tolerance==0.&&control.absolute_tolerance==0.))throw std::invalid_argument("Invalid composite convergence controls");
    auto& e=*execution_;SolveReport report;
    report.rhs_rms=norm(rhs);report.removed_rhs_mean=op().boundary_kind()==elliptic::BoundaryKind::Periodic?mean(rhs):0.;
    if(!std::isfinite(report.rhs_rms))return report;
    if(report.rhs_rms!=0.&&std::abs(report.removed_rhs_mean/report.rhs_rms)>64.*std::numeric_limits<double>::epsilon())
        throw std::invalid_argument("Incompatible periodic composite RHS");
    e.linear(source_,1.,rhs);project(source_);const double scale=norm(source_);
    report.rhs_rms=scale;report.target=std::max(control.absolute_tolerance,control.relative_tolerance*scale);
    report.initial_residual=report.residual=scale;e.fill(x_);
    if(scale<=report.target){report.status=SolveStatus::Converged;return report;}
    e.linear(source_,1./scale,source_);const double target=report.target/scale;
    constexpr int restart=20;
    for(;;) {
        apply(0,x_,work_);e.linear(residual_,1.,source_,-1.,work_);
        const double residual_norm=norm(residual_);report.residual=residual_norm*scale;
        if(!std::isfinite(residual_norm))return report;
        if(residual_norm<=target) {
            e.linear(x_,scale,x_);project(x_);apply(0,x_,work_);
            e.linear(work_,1.,rhs,-1.,work_,-report.removed_rhs_mean);report.residual=norm(work_);
            if(std::isfinite(report.residual)&&report.residual<=report.target)report.status=SolveStatus::Converged;
            return report;
        }
        if(report.cycles>=control.max_cycles){report.status=SolveStatus::MaxCycles;return report;}
        project(residual_);const double beta=norm(residual_);
        if(!std::isfinite(beta)||beta==0.)return report;
        e.linear(basis_[0],1./beta,residual_);
        double h[restart+1][restart]{},cosine[restart]{},sine[restart]{},g[restart+1]{};g[0]=beta;int count=0;
        for(int j=0;j<restart&&report.cycles<control.max_cycles;++j) {
            ++report.cycles;precondition(basis_[j],correction_[j]);apply(0,correction_[j],work_);project(work_);
            for(int pass=0;pass<2;++pass)for(int i=0;i<=j;++i){const double value=dot(work_,basis_[i]);h[i][j]+=value;e.linear(work_,1.,work_,-value,basis_[i]);}
            h[j+1][j]=norm(work_);const double next=h[j+1][j];if(!std::isfinite(next))return report;
            for(int i=0;i<j;++i){const double first=cosine[i]*h[i][j]+sine[i]*h[i+1][j];
                h[i+1][j]=-sine[i]*h[i][j]+cosine[i]*h[i+1][j];h[i][j]=first;}
            const double length=std::hypot(h[j][j],h[j+1][j]);if(!std::isfinite(length)||length==0.)return report;
            cosine[j]=h[j][j]/length;sine[j]=h[j+1][j]/length;h[j][j]=length;h[j+1][j]=0.;
            g[j+1]=-sine[j]*g[j];g[j]*=cosine[j];count=j+1;
            if(std::abs(g[j+1])<=0.5*target||next==0.)break;
            e.linear(basis_[j+1],1./next,work_);
        }
        double y[restart]{};
        for(int i=count-1;i>=0;--i){y[i]=g[i];for(int j=i+1;j<count;++j)y[i]-=h[i][j]*y[j];
            y[i]/=h[i][i];e.linear(x_,1.,x_,y[i],correction_[i]);}
        project(x_);
    }
}
}
