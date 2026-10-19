#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "numerics/multigrid/CompositeMultigrid.h"
#if ARCH_CUDA_BUILD_ENABLED
#include "cuda/runtime/gravity/CudaGravityExecution.h"
#endif
#include "physics/constant/PhysicalConstants.h"
#include "physics/gravity/GravityBoundary.h"
#include "physics/gravity/GravitySourceBounds.h"
using namespace arch;
namespace {
constexpr double pi=constants::math::pi;
void require(bool condition,const char* what) { if (!condition) throw std::runtime_error(what); }
elliptic::CartesianMesh base_mesh(int dim,int n) {
    elliptic::CartesianMesh b; b.dimension=dim;
    for (int a=0;a<dim;++a) { b.cells[a]=n; b.spacing[a]=1./n; }
    return b;
}
std::vector<elliptic::CompositeCell> make_cells(const elliptic::CartesianMesh& base,bool refined) {
    std::vector<elliptic::CompositeCell> cells;
    for (int i=0;i<base.size();++i) {
        const auto p=base.position(i);
        bool inside=refined;
        for (int a=0;a<base.dimension;++a) inside&=p[a]>=base.cells[a]/4 && p[a]<3*base.cells[a]/4;
        if (!inside) cells.push_back({0,p});
        else for (int child=0;child<(1<<base.dimension);++child) {
            auto q=p;
            for (int a=0;a<base.dimension;++a) q[a]=2*p[a]+((child>>a)&1);
            cells.push_back({1,q});
        }
    }
    return cells;
}
/** Reproduce the production origin topology: one azimuthal half refined. */
std::vector<elliptic::CompositeCell> make_origin_seam_cells(
    const elliptic::CartesianMesh& base) {
    std::vector<elliptic::CompositeCell> cells;
    for (int i=0;i<base.size();++i) {
        const auto p=base.position(i);
        if (p[1]>=base.cells[1]/2) {cells.push_back({0,p});continue;}
        for (int child=0;child<4;++child) {
            auto q=p;
            for (int axis=0;axis<2;++axis)
                q[axis]=2*p[axis]+((child>>axis)&1);
            cells.push_back({1,q});
        }
    }
    return cells;
}

/** Internal anisotropy is hierarchy-owned; physical inputs retain ratio <=2.
 * The solve here is an algebraic residual check, not an independent science gate.
 */
void coarse_mesh_diagnostic() {
    static_assert(!std::is_constructible_v<elliptic::CompositePoisson,
        elliptic::CartesianMesh,std::vector<elliptic::CompositeCell>,
        elliptic::BoundaryKind,const elliptic::CompositePoisson*>);
    auto rejects=[](auto function,const char* message) {
        bool failed=false;
        try {function();} catch(const std::invalid_argument&) {failed=true;}
        require(failed,message);
    };
    auto invalid=base_mesh(3,4);invalid.spacing={4.,1.,1.};
    rejects([&]{elliptic::CompositePoisson op(invalid,make_cells(invalid,false));},
            "unsupported physical spacing ratio accepted");
    rejects([&]{elliptic::CompositePoisson op(invalid,make_cells(invalid,true));},
            "AMR leaf level bypassed physical spacing ratio");
    invalid.spacing[0]=std::numeric_limits<double>::infinity();
    rejects([&]{multigrid::CompositeMultigrid op(invalid,make_cells(invalid,false));},
            "nonfinite physical geometry accepted");
    for (const auto shape:{std::array<int,3>{32,8,4},std::array<int,3>{64,16,16}})
        for (const auto kind:{elliptic::BoundaryKind::Periodic,elliptic::BoundaryKind::Dirichlet})
            for (bool refined:{false,true}) {
                auto base=base_mesh(3,shape[0]);base.cells=shape;
                multigrid::CompositeMultigrid solver(base,make_cells(base,refined),kind);
                const auto& op=solver.op();
                require(solver.level_count()==static_cast<std::size_t>(
                    (shape[0]==32?4:5)+(refined?1:0)),
                    "coarse hierarchy stopped before bounded actual bottom");
                std::vector<double> exact(op.size()),rhs(op.size()),residual(op.size());
                for(int i=0;i<op.size();++i) {
                    const auto x=op.center(i);double value=1.;
                    for(int a=0;a<3;++a)
                        value*=std::cos(2*pi*(x[a]-base.origin[a])/
                            (base.cells[a]*base.spacing[a])+0.17*(a+1));
                    exact[i]=value;
                }
                op.project(exact);op.apply(exact,rhs);op.project(rhs);
                const auto solution=solver.solve(rhs,{1e-10,0.,200});
                require(solution.report.status==multigrid::SolveStatus::Converged,
                        "derived coarse hierarchy solve failed");
                op.apply(solution.potential,residual);
                for(int i=0;i<op.size();++i)residual[i]-=rhs[i];
                require(op.norm(residual)<=solution.report.target,
                        "derived hierarchy independent physical residual");
                solver.clear_initial_guess();
                const auto repeated=solver.solve(rhs,{1e-10,0.,200});
                require(repeated.report.status==multigrid::SolveStatus::Converged &&
                        repeated.report.residual<=repeated.report.target,
                        "derived hierarchy repeated solve failed");
                std::cout<<"derived-coarse shape="<<shape[0]<<','<<shape[1]<<','<<shape[2]
                         <<" refined="<<refined<<" boundary="<<static_cast<int>(kind)
                         <<" levels="<<solver.level_count()
                         <<" residual="<<op.norm(residual)
                         <<" target="<<solution.report.target<<'\n';
            }
}

double potential(const std::array<double,3>& x,int dim) {
    double result=1.;
    for (int a=0;a<dim;++a) result*=std::cos(2*pi*x[a]+0.17*(a+1));
    return result;
}
double net_force(const elliptic::CompositePoisson& op,std::span<const double> density,
                 std::span<const double> phi) {
    std::vector<std::array<double,3>> g(op.size());
    for (const auto& face:op.faces()) {
        const double value=-op.face_gradient(phi,face);
        for(int cell:{face.left,face.right})
            g[cell][face.axis]+=0.5*face.area*op.width(cell,face.axis)/op.volumes()[cell]*value;
    }
    double error=0.;
    for(int axis=0;axis<op.base().dimension;++axis) {
        long double sum=0.,absolute=0.;
        for(int cell=0;cell<op.size();++cell) {
            const long double force=static_cast<long double>(op.volumes()[cell])*density[cell]*g[cell][axis];
            sum+=force;absolute+=std::abs(force);
        }
        error=std::max(error,static_cast<double>(std::abs(sum)/absolute));
    }
    return error;
}
void convergence(int maximum_dimension) {
    std::cout<<"dimension,refined,n,cells,iterations,residual,target,phi_rms,face_rms,interface_rms,phi_order,face_order,interface_order,net_force\n";
    for (int dim=1;dim<=maximum_dimension;++dim) for (bool refined:{false,true}) {
        double previous[3]{};
        double previous_force=0.;
        for (int n:{16,32,64}) {
            const auto base=base_mesh(dim,n);
            multigrid::CompositeMultigrid solver(base,make_cells(base,refined));
            const auto& op=solver.op();
            std::vector<double> rhs(op.size()),exact(op.size()),error(op.size());
            for (int i=0;i<op.size();++i) {
                exact[i]=potential(op.center(i),dim);
                rhs[i]=dim*4*pi*pi*exact[i];
            }
            op.project(rhs); op.project(exact);
            const auto solution=solver.solve(rhs,{1e-10,0.,200});
            std::cout<<dim<<','<<refined<<','<<n<<','<<op.size()<<','<<solution.report.cycles<<','
                <<solution.report.residual<<','<<solution.report.target<<std::flush;
            require(solution.report.status==multigrid::SolveStatus::Converged,"composite solve did not converge");
            require(solution.report.residual<=solution.report.target,"composite residual acceptance");
            for (int i=0;i<op.size();++i) error[i]=solution.potential[i]-exact[i];
            const double ep=op.norm(error);
            long double all=0.,all_area=0.,interface=0.,interface_area=0.;
            for (const auto& f:op.faces()) {
                double expected=-2*pi*std::sin(2*pi*f.center[f.axis]+0.17*(f.axis+1));
                for (int a=0;a<dim;++a) if (a!=f.axis) expected*=std::cos(2*pi*f.center[a]+0.17*(a+1));
                const double e=op.face_gradient(solution.potential,f)-expected;
                all+=f.area*e*e; all_area+=f.area;
                if (op.cells()[f.left].level!=op.cells()[f.right].level) { interface+=f.area*e*e; interface_area+=f.area; }
            }
            const double errors[]{ep,std::sqrt(static_cast<double>(all/all_area)),
                interface_area>0. ? std::sqrt(static_cast<double>(interface/interface_area)) : 0.};
            double orders[3]{};
            for (int a=0;a<3;++a) {
                if (previous[a]!=0.) orders[a]=std::log2(previous[a]/errors[a]);
                previous[a]=errors[a];
                std::cout<<','<<errors[a];
            }
            for (double order:orders) std::cout<<','<<order;
            std::vector<double> density(op.size());
            for(int cell=0;cell<op.size();++cell) density[cell]=1.+0.1*rhs[cell]/(dim*4*pi*pi);
            const double force=net_force(op,density,solution.potential);
            std::cout<<','<<force<<'\n'<<std::flush;
            if(n==64) require(force <= (refined ? 2e-3 : 1e-11),"net self-force budget");
            if(n>16 && refined) require(force<previous_force || std::max(force,previous_force)<1e-11,"net self-force does not improve");
            previous_force=force;
            if (n>16) for (int a=0;a<(refined ? 3 : 2);++a) require(orders[a]>=1.8,"composite spatial order below 1.8");
        }
    }
}
void averaged_source_exactness() {
    for (int n:{16,32,64}) {
        const auto base=base_mesh(1,n);
        multigrid::CompositeMultigrid solver(base,make_cells(base,false));
        const auto& op=solver.op();
        std::vector<double> rhs(op.size());
        for (int i=0;i<op.size();++i)
            rhs[i]=4*pi*pi*potential(op.center(i),1)*std::sin(pi/n)/(pi/n);
        op.project(rhs);
        const auto result=solver.solve(rhs,{1e-10,0.,200});
        require(result.report.status==multigrid::SolveStatus::Converged,"averaged-source solve");
        for (const auto& f:op.faces())
            require(std::abs(op.face_gradient(result.potential,f)+2*pi*std::sin(2*pi*f.center[0]+0.17))<1e-8,
                    "1D Gauss-law exactness for cell-averaged source");
    }
}
void contract() {
    const auto base=base_mesh(2,16);
    auto cells=make_cells(base,true);
    multigrid::CompositeMultigrid solver(base,cells); const auto& op=solver.op();
    std::vector<double> input(op.size(),-7.),applied(op.size());
    op.apply(input,applied);
    for(double x:applied) require(x==0.,"constant nullspace");
    for(int i=0;i<op.size();++i) input[i]=std::sin(1.7*i)+0.2*std::cos(0.13*i);
    op.apply(input,applied);
    require(std::abs(op.mean(applied))<1e-12*op.norm(applied),"integrated coarse/fine flux mismatch");
    std::vector<double> rhs(op.size());
    auto zero=solver.solve(rhs,{1e-10,0.,100});
    require(zero.report.cycles==0 && zero.report.status==multigrid::SolveStatus::Converged,"zero source");
    auto rejects=[](auto function,const char* message){bool failed=false;try{function();}catch(const std::exception&){failed=true;}require(failed,message);};
    rhs[0]=1.;rejects([&]{solver.solve(rhs,{1e-10,0.,100});},"nonzero mean accepted");
    rhs[0]=std::numeric_limits<double>::quiet_NaN();
    rejects([&]{solver.solve(rhs,{1e-10,0.,100});},"nonfinite RHS accepted");
    rejects([&]{solver.solve(input,{0.,0.,100});},"empty tolerance accepted");
    auto broken=cells;broken.pop_back();
    rejects([&]{elliptic::CompositePoisson invalid(base,broken);},"hole accepted");
    broken=cells;broken[0]=broken[1];
    rejects([&]{elliptic::CompositePoisson invalid(base,broken);},"duplicate accepted");
    op.project(input);
    auto limited=solver.solve(input,{1e-14,0.,1});
    require(limited.report.status!=multigrid::SolveStatus::Converged && limited.potential.empty(),"unconverged iterate published");
    const auto reference=solver.solve(input,{1e-10,0.,200});
    require(reference.report.status==multigrid::SolveStatus::Converged,"reference solve");
    for(double scale:{1e-100,1e100}) {
        for(int i=0;i<op.size();++i) rhs[i]=scale*input[i];
        op.project(rhs);const auto result=solver.solve(rhs,{1e-10,0.,200});
        require(result.report.status==multigrid::SolveStatus::Converged,"physical scale solve");
        op.apply(result.potential,applied);
        for(int i=0;i<op.size();++i) applied[i]-=rhs[i];
        require(op.norm(applied)<=result.report.target,"independent physical residual");
        for(int i=0;i<op.size();++i) applied[i]=result.potential[i]/scale-reference.potential[i];
        require(op.norm(applied)<1e-8*op.norm(reference.potential),"gravity scale invariance");
    }
    auto nested=cells;
    for(std::size_t i=0;i<nested.size();) {
        const auto cell=nested[i];
        if(cell.level==1 && cell.index[0]>=12 && cell.index[0]<20 && cell.index[1]>=12 && cell.index[1]<20) {
            nested.erase(nested.begin()+i);
            for(int child=0;child<4;++child) nested.push_back({2,{2*cell.index[0]+(child&1),2*cell.index[1]+((child>>1)&1),0}});
        } else ++i;
    }
    multigrid::CompositeMultigrid hierarchy(base,nested);
    std::vector<double> nested_rhs(hierarchy.op().size());
    for(int i=0;i<hierarchy.op().size();++i) nested_rhs[i]=potential(hierarchy.op().center(i),2);
    hierarchy.op().project(nested_rhs);
    require(hierarchy.solve(nested_rhs,{1e-10,0.,200}).report.status==multigrid::SolveStatus::Converged,
            "nested refinement V-cycle failed");
    std::cout<<"Composite contracts passed\n";
}

void boundary_convergence(int largest=32) {
    for(bool refined:{false,true}) {
        double previous_phi=0.,previous_face=0.,previous_boundary=0.;
        for(int n:{8,16,32}) {
            if(n>largest)continue;
            auto base=base_mesh(3,n);
            multigrid::CompositeMultigrid solver(base,make_cells(base,refined),elliptic::BoundaryKind::Dirichlet);
            const auto& op=solver.op();
            const auto exact=[](const std::array<double,3>& p) {return std::exp(.3*p[0]+.2*p[1]+.1*p[2]);};
            std::vector<double> rhs(op.size()),bc(op.faces().size()),error(op.size());
            for(int i=0;i<op.size();++i) rhs[i]=-.14*exact(op.center(i));
            for(std::size_t i=0;i<bc.size();++i) if(op.faces()[i].boundary_side>=0) bc[i]=exact(op.faces()[i].center);
            rhs=op.effective_rhs(rhs,bc);
            const auto result=solver.solve(rhs,{1e-11,0.,300});
            require(result.report.status==multigrid::SolveStatus::Converged,"Dirichlet composite convergence failed");
            for(int i=0;i<op.size();++i) error[i]=result.potential[i]-exact(op.center(i));
            double face_error=0.,face_area=0.,boundary_error=0.,boundary_area=0.;
            for(std::size_t i=0;i<bc.size();++i) {
                const auto& f=op.faces()[i];
                const double coefficient[]={.3,.2,.1};
                const double e=op.face_gradient(result.potential,f,bc[i])-coefficient[f.axis]*exact(f.center);
                face_error+=f.area*e*e;face_area+=f.area;
                if(f.boundary_side>=0) {boundary_error+=f.area*e*e;boundary_area+=f.area;}
            }
            const double ep=op.norm(error),ef=std::sqrt(face_error/face_area),eb=std::sqrt(boundary_error/boundary_area);
            std::cout<<"Dirichlet refined="<<refined<<" n="<<n<<" phi="<<ep<<" face="<<ef<<" boundary="<<eb<<" iterations="<<result.report.cycles;
            if(previous_phi) {
                const double qp=std::log2(previous_phi/ep),qf=std::log2(previous_face/ef),qb=std::log2(previous_boundary/eb);
                std::cout<<" orders="<<qp<<','<<qf<<','<<qb;
                require(qp>=1.8 && qf>=1.8 && qb>=1.8,"Dirichlet second order budget");
            }
            std::cout<<'\n';previous_phi=ep;previous_face=ef;previous_boundary=eb;
        }
    }
}

void isolated_boundary() {
    for(bool elongated:{false,true}) for(bool refined:{false,true}) {
        auto base=base_mesh(3,16);
        if(elongated) {base.cells[1]=8;base.cells[2]=8;}
        elliptic::CompositePoisson op(base,make_cells(base,refined),elliptic::BoundaryKind::Dirichlet);
        Physical::Gravity::GravityBoundary boundary(op);
        std::vector<double> rho(op.size());
        for(int i=0;i<op.size();++i) {
            const auto x=op.center(i);
            const double r2=std::pow((x[0]-.37)/.09,2)+std::pow((x[1]-.43)/.07,2)+std::pow((x[2]-.56)/.11,2);
            rho[i]=std::exp(-.5*r2)+.01;
        }
        boundary.update(rho);
        const auto direct=boundary.values(op,1.,0.),standard=boundary.values(op,1.),
            tighter=boundary.values(op,1.,.125),monopole=boundary.values(op,1.,.25,0);
        double norm=0.,error=0.,tight_error=0.,low_order_error=0.;
        for(std::size_t i=0;i<direct.size();++i) {
            norm+=direct[i]*direct[i];error+=std::pow(standard[i]-direct[i],2);
            tight_error+=std::pow(tighter[i]-direct[i],2);low_order_error+=std::pow(monopole[i]-direct[i],2);
        }
        error=std::sqrt(error/norm);tight_error=std::sqrt(tight_error/norm);low_order_error=std::sqrt(low_order_error/norm);
        std::cout<<"Isolated boundary elongated="<<elongated<<" refined="<<refined<<" relative="<<error<<" theta/2="<<tight_error<<" monopole="<<low_order_error<<'\n';
        require(error<2e-3 && tight_error<error && error<low_order_error,"isolated boundary approximation budget");
        // An explicit independent source sum validates tree indexing and moments.
        const auto point=op.faces().front().center;
        double exact=0.;
        for(int i=0;i<op.size();++i) {
            const auto x=op.center(i);double distance=0.;for(int a=0;a<3;++a) distance+=std::pow(x[a]-point[a],2);
            exact-=rho[i]*op.volumes()[i]/std::sqrt(distance);
        }
        const double actual=Physical::Gravity::isolated_potential(boundary.nodes().data(),boundary.moments().data(),
            static_cast<int>(boundary.nodes().size()),point.data(),1.,0.);
        require(std::abs(actual/exact-1.)<1e-12,"direct boundary source sum mismatch");
    }
}


/** Independent continuum data for one physical side policy.
 *  Dirichlet samples Phi, Neumann and Robin sample a*Phi+b*dPhi/dn of the
 *  analytic solution with the outward normal (s=-1 lower, s=+1 upper side). */
std::vector<double> continuum_boundary_values(const elliptic::CompositePoisson& op,
    const elliptic::CompositeBoundary& boundary,
    const std::function<double(const std::array<double,3>&)>& exact,
    const std::function<std::array<double,3>(const std::array<double,3>&)>& gradient) {
    std::vector<double> values(op.faces().size(),0.);
    for(std::size_t index=0;index<op.faces().size();++index) {
        const auto& face=op.faces()[index];
        if(face.boundary_side<0) continue;
        const int side=face.boundary_side&1,axis=face.boundary_side/2;
        const double s=side?1.:-1.;
        const double derivative=gradient(face.center)[axis];
        const auto& condition=boundary.conditions[face.boundary_side];
        switch(condition.kind) {
        case elliptic::FaceBoundaryKind::Dirichlet: values[index]=exact(face.center); break;
        case elliptic::FaceBoundaryKind::Neumann: values[index]=s*derivative; break;
        case elliptic::FaceBoundaryKind::Robin:
            values[index]=condition.a*exact(face.center)+condition.b*s*derivative; break;
        default: values[index]=0.; break;
        }
    }
    return values;
}

/** Area-weighted continuum flux mismatch and discrete solution error. */
void continuum_face_error(const elliptic::CompositePoisson& op,std::span<const double> potential,
    std::span<const double> values,
    const std::function<std::array<double,3>(const std::array<double,3>&)>& gradient,
    double& flux) {
    double mismatch=0.,area=0.;
    for(std::size_t i=0;i<op.faces().size();++i) {
        const auto& face=op.faces()[i];
        const double value=op.face_gradient(potential,face,values[i]);
        const double expected=gradient(face.center)[face.axis];
        mismatch+=face.area*(value-expected)*(value-expected);
        area+=face.area;
    }
    flux=std::sqrt(mismatch/area);
}

/** Mixed Dirichlet/Neumann/Robin sides on a Cartesian box with AMR. */
void mixed_cartesian_boundary() {
    using K=elliptic::FaceBoundaryKind;
    std::cout<<"mixed Cartesian refined,n,cells,cycles,phi,flux,phi_order,flux_order\n";
    const auto exact=[](const std::array<double,3>& x) {
        return std::exp(.3*x[0]+.2*x[1]+.1*x[2]);
    };
    const auto gradient=[](const std::array<double,3>& x) {
        const double value=std::exp(.3*x[0]+.2*x[1]+.1*x[2]);
        return std::array<double,3>{.3*value,.2*value,.1*value};
    };
    elliptic::CompositeBoundary boundary;
    boundary.sides={K::Dirichlet,K::Neumann,K::Robin,K::Dirichlet,K::Neumann,K::Robin};
    boundary.conditions[0]={K::Dirichlet,1.,0.};
    boundary.conditions[1]={K::Neumann,0.,1.};
    boundary.conditions[2]={K::Robin,1.5,1.};
    boundary.conditions[3]={K::Dirichlet,1.,0.};
    boundary.conditions[4]={K::Neumann,0.,1.};
    boundary.conditions[5]={K::Robin,.5,1.};
    for(bool refined:{false,true}) {
        double previous_phi=0.,previous_flux=0.;
        for(int n:{8,16}) {
            const auto base=base_mesh(3,n);
            multigrid::CompositeMultigrid solver(base,make_cells(base,refined),boundary);
            const auto& op=solver.op();
            require(!op.has_constant_nullspace(),"mixed boundary kept a constant nullspace");
            // Every nonzero-area flux side publishes its own physical record.
            int dirichlet_faces=0,neumann_faces=0,robin_faces=0;
            for(const auto& face:op.faces()) {
                if(face.boundary_side<0) continue;
                require(face.area>0.,"mixed boundary published a zero-area record");
                switch(boundary.conditions[face.boundary_side].kind) {
                case K::Dirichlet: ++dirichlet_faces; break;
                case K::Neumann: ++neumann_faces; break;
                case K::Robin: ++robin_faces; break;
                default: break;
                }
            }
            require(dirichlet_faces>0 && neumann_faces>0 && robin_faces>0,
                "mixed boundary did not publish every side kind");
            std::vector<double> rhs(op.size()),error(op.size());
            // Independent continuum source: -Laplacian(Phi) = -.14*Phi.
            for(int i=0;i<op.size();++i) rhs[i]=-.14*exact(op.center(i));
            const auto values=continuum_boundary_values(op,boundary,exact,gradient);
            const auto result=solver.solve(op.effective_rhs(rhs,values),{1e-10,0.,300});
            require(result.report.status==multigrid::SolveStatus::Converged
                    && result.report.residual<=result.report.target,
                "mixed Cartesian solve failed");
            for(int i=0;i<op.size();++i) error[i]=result.potential[i]-exact(op.center(i));
            const double phi=op.norm(error);
            double flux=0.;
            continuum_face_error(op,result.potential,values,gradient,flux);
            std::cout<<"mixed Cartesian refined="<<refined<<" n="<<n<<" cells="<<op.size()
                <<" cycles="<<result.report.cycles<<" phi="<<phi<<" flux="<<flux;
            if(previous_phi) {
                const double phi_order=std::log2(previous_phi/phi);
                const double flux_order=std::log2(previous_flux/flux);
                std::cout<<" orders="<<phi_order<<','<<flux_order;
                require(phi_order>=1.8 && flux_order>=1.8,
                    "mixed Cartesian second order budget");
            }
            std::cout<<'\n';
            previous_phi=phi;previous_flux=flux;
        }
    }
}

/** Solve one mixed radial problem and report its independent error measures. */
void curved_mixed_case(elliptic::Geometry geometry,bool refined,int n,
    const std::function<double(const std::array<double,3>&)>& exact,
    const std::function<std::array<double,3>(const std::array<double,3>&)>& gradient,
    const std::function<double(const std::array<double,3>&)>& source,
    double& phi,double& flux,int& cycles,bool wedge=false) {
    using K=elliptic::FaceBoundaryKind;
    const bool spherical=geometry==elliptic::Geometry::Spherical;
    auto base=base_mesh(3,n);
    base.geometry=geometry;
    base.origin[0]=.5;base.spacing[0]=1./n;
    base.origin[1]=spherical?.3:-.5;base.spacing[1]=(spherical?pi-.6:1.)/n;
    base.origin[2]=0.;base.spacing[2]=2*pi/n;
    elliptic::CompositeBoundary boundary;
    boundary.sides={K::Robin,K::Dirichlet,K::Neumann,K::Robin,K::Periodic,K::Periodic};
    boundary.conditions[0]={K::Robin,1.,1.};
    boundary.conditions[1]={K::Dirichlet,1.,0.};
    boundary.conditions[2]={K::Neumann,0.,1.};
    boundary.conditions[3]={K::Robin,.5,1.};
    boundary.conditions[4]={K::Periodic,0.,1.};
    boundary.conditions[5]={K::Periodic,0.,1.};
    if(wedge) {
        base.origin[2]=.4; base.spacing[2]=1./n;
        boundary.sides[4]=K::Dirichlet; boundary.conditions[4]={K::Dirichlet,1.,0.};
        boundary.sides[5]=K::Neumann; boundary.conditions[5]={K::Neumann,0.,1.};
    }
    multigrid::CompositeMultigrid solver(base,make_cells(base,refined),boundary);
    const auto& op=solver.op();
    std::vector<double> rhs(op.size()),error(op.size());
    for(int i=0;i<op.size();++i) rhs[i]=source(op.center(i));
    const auto values=continuum_boundary_values(op,boundary,exact,gradient);
    const auto result=solver.solve(op.effective_rhs(rhs,values),{1e-10,0.,300});
    require(result.report.status==multigrid::SolveStatus::Converged
            && result.report.residual<=result.report.target,
        "mixed curved solve failed");
    for(int i=0;i<op.size();++i) error[i]=result.potential[i]-exact(op.center(i));
    phi=op.norm(error);cycles=result.report.cycles;
    continuum_face_error(op,result.potential,values,gradient,flux);
}

/** Mixed Dirichlet/Neumann/Robin radial sides on curved native geometry.
 *  A radial quadratic is reproduced exactly by the shared fit, while a cubic
 *  perturbation leaves an O(h^2) error with an exactly zero tangential flux. */
void curved_mixed_boundary() {
    const auto quadratic_exact=[](const std::array<double,3>& x) {return 1.+.5*x[0]*x[0];};
    const auto quadratic_gradient=[](const std::array<double,3>& x) {
        return std::array<double,3>{x[0],0.,0.};
    };
    const auto cubic_exact=[](const std::array<double,3>& x) {
        return 1.+.5*x[0]*x[0]+x[0]*x[0]*x[0]/6.;
    };
    const auto cubic_gradient=[](const std::array<double,3>& x) {
        return std::array<double,3>{x[0]+.5*x[0]*x[0],0.,0.};
    };
    std::cout<<"mixed curved geometry,refined,n,cells,cycles,phi,flux,phi_order,flux_order\n";
    for(auto geometry:{elliptic::Geometry::Cylindrical,elliptic::Geometry::Spherical}) {
        const bool spherical=geometry==elliptic::Geometry::Spherical;
        // -Laplacian(1+r^2/2) is constant; the cubic term adds a linear radial
        // source (2r spherical, 1.5r cylindrical).
        const auto quadratic_source=[&](const std::array<double,3>&) {
            return spherical?-3.:-2.;
        };
        const auto cubic_source=[&](const std::array<double,3>& x) {
            return spherical?-(3.+2.*x[0]):-(2.+1.5*x[0]);
        };
        for(bool refined:{false,true}) for(int n:{8,16}) {
            double phi=0.,flux=0.;int cycles=0;
            curved_mixed_case(geometry,refined,n,quadratic_exact,quadratic_gradient,
                quadratic_source,phi,flux,cycles);
            std::cout<<"mixed curved quadratic geometry="<<static_cast<int>(geometry)
                <<" refined="<<refined<<" n="<<n<<" cycles="<<cycles
                <<" phi="<<phi<<" flux="<<flux<<'\n';
            // The shared quadratic fit reproduces this solution to roundoff.
            require(phi<1e-8 && flux<1e-7,"curved mixed quadratic exactness budget");
        }
        for(bool refined:{false,true}) {
            double phi=0.,flux=0.; int cycles=0;
            curved_mixed_case(geometry,refined,8,quadratic_exact,quadratic_gradient,
                quadratic_source,phi,flux,cycles,true);
            require(phi<1e-8 && flux<1e-7,"curved wedge mixed quadratic exactness budget");
        }
        for(bool refined:{false,true}) {
            double previous_phi=0.,previous_flux=0.;
            for(int n:{8,16}) {
                double phi=0.,flux=0.;int cycles=0;
                curved_mixed_case(geometry,refined,n,cubic_exact,cubic_gradient,
                    cubic_source,phi,flux,cycles);
                std::cout<<"mixed curved cubic geometry="<<static_cast<int>(geometry)
                    <<" refined="<<refined<<" n="<<n<<" cycles="<<cycles
                    <<" phi="<<phi<<" flux="<<flux;
                if(previous_phi) {
                    const double phi_order=std::log2(previous_phi/phi);
                    const double flux_order=std::log2(previous_flux/flux);
                    std::cout<<" orders="<<phi_order<<','<<flux_order;
                    require(phi_order>=1.8 && flux_order>=1.8,
                        "mixed curved second order budget");
                }
                std::cout<<'\n';
                previous_phi=phi;previous_flux=flux;
            }
        }
    }
}

void pure_neumann_compatibility() {
    using K=elliptic::FaceBoundaryKind;
    const auto base=base_mesh(2,16);
    const auto cells=make_cells(base,true);
    elliptic::CompositeBoundary boundary;
    boundary.sides.fill(K::Neumann);
    elliptic::CompositePoisson op(base,cells,boundary);
    require(op.has_constant_nullspace() && !op.periodic_boundary(),
        "pure Neumann operator lost its constant nullspace");
    std::vector<double> constant(op.size(),2.5),applied(op.size());
    op.apply(constant,applied);
    for(double value:applied) require(std::abs(value)<1e-12,"pure Neumann constant mode is not null");
    // The volume-weighted zero gauge differs from the arithmetic mean on AMR.
    std::vector<double> refined_fraction(op.size(),0.);
    int refined_cells=0;
    for(int i=0;i<op.size();++i) if(op.cells()[i].level==1) {refined_fraction[i]=1.;++refined_cells;}
    const double volume_fraction=op.mean(refined_fraction);
    require(std::abs(volume_fraction-double(refined_cells)/op.size())>1e-3,
        "volume weighting is not distinct from the arithmetic mean");
    op.project(refined_fraction);
    require(std::abs(op.mean(refined_fraction))<=1e-14*op.norm(refined_fraction),
        "volume-weighted zero gauge was not applied");
    // A compatible source (zero volume-weighted mass) converges to a true solution.
    multigrid::CompositeMultigrid solver(base,cells,boundary);
    std::vector<double> rhs(op.size()),exact(op.size()),error(op.size());
    for(int i=0;i<op.size();++i) {
        const auto x=op.center(i);
        exact[i]=std::cos(2*pi*x[0])*std::cos(2*pi*x[1]);
        rhs[i]=8*pi*pi*exact[i];
    }
    op.project(rhs);
    op.validate_compatibility(rhs);
    const auto result=solver.solve(rhs,{1e-10,0.,300});
    require(result.report.status==multigrid::SolveStatus::Converged,
        "pure Neumann compatible solve failed");
    op.apply(result.potential,applied);
    for(int i=0;i<op.size();++i) applied[i]-=rhs[i];
    require(op.norm(applied)<=result.report.target,"pure Neumann independent residual");
    for(int i=0;i<op.size();++i) error[i]=result.potential[i]-exact[i];
    std::cout<<"pure Neumann phi="<<op.norm(error)<<" cycles="<<result.report.cycles
        <<" residual="<<result.report.residual<<'\n';
    // A constant shift is still a solution and never changes the flux.
    std::vector<double> shifted=result.potential;
    for(double& value:shifted) value+=7.;
    op.apply(shifted,applied);
    for(int i=0;i<op.size();++i) applied[i]-=rhs[i];
    require(op.norm(applied)<=result.report.target,"pure Neumann shift invariance");
    // Inhomogeneous Neumann data must still telescope to zero net oriented flux.
    std::vector<double> values(op.faces().size(),0.),flux_rhs(rhs.size(),0.);
    for(std::size_t i=0;i<op.faces().size();++i)
        if(op.faces()[i].boundary_side>=0) values[i]=1.;
    flux_rhs=op.effective_rhs(flux_rhs,values);
    require(std::abs(op.mean(flux_rhs))>1e-6,"balanced flux data is not represented");
    // Nonzero mass with homogeneous Neumann data is rejected, not projected.
    auto rejects=[&](auto function,const char* message) {
        bool failed=false;
        try {function();} catch(const std::exception&) {failed=true;}
        require(failed,message);
    };
    std::vector<double> mass(op.size(),1.);
    rejects([&]{solver.solve(mass,{1e-10,0.,100});},"nonzero Neumann mass accepted");
    rejects([&]{op.validate_compatibility(mass);},"validate_compatibility accepted nonzero mass");
    rejects([&]{std::vector<double> bad(op.size(),std::numeric_limits<double>::quiet_NaN());
        op.validate_compatibility(bad);},"validate_compatibility accepted a nonfinite source");
    // A pure periodic boundary keeps its legacy projection path.
    elliptic::CompositePoisson periodic(base,cells,elliptic::BoundaryKind::Periodic);
    require(periodic.periodic_boundary() && periodic.has_constant_nullspace(),
        "legacy periodic boundary lost its projection");
    periodic.validate_compatibility(mass);
    std::vector<double> periodic_rhs(op.size(),1.);
    periodic.project(periodic_rhs);
    require(std::abs(periodic.mean(periodic_rhs))<1e-15,"legacy periodic projection");
    // A one-cell root is rejected, so a coarse level always keeps a real
    // bounded LU instead of the degenerate single-cell all-Neumann case.
    elliptic::CartesianMesh single;
    single.dimension=1;single.cells={1,1,1};single.spacing={1.,1.,1.};
    rejects([&]{elliptic::CompositePoisson invalid(single,{{0,{0,0,0}}},boundary);},
        "single-cell all-Neumann root accepted");
}

/** Prescribed flux remains independent of a large, volume-zero-mean potential.
 *  The flat boundary patches give the independent values dPhi/dn=c/b for
 *  Neumann and dPhi/dn=(c-a*Phi_A)/b in the near-Neumann roundoff limit. */
void extreme_flux_boundary() {
    using K=elliptic::FaceBoundaryKind;
    const auto base=base_mesh(1,16);
    const auto cells=make_cells(base,false);
    for(const double a:{0.,std::ldexp(1.,-60)}) {
        const auto kind=a==0.?K::Neumann:K::Robin;
        elliptic::CompositeBoundary boundary;
        boundary.sides.fill(kind);
        boundary.conditions.fill({kind,a,1.});
        // One Dirichlet side fixes the near-Neumann global gauge. This check
        // targets the local flux row, not an almost singular coarse solve.
        if(a!=0.) {
            boundary.sides[1]=K::Dirichlet;
            boundary.conditions[1]={K::Dirichlet,1.,0.};
        }
        multigrid::CompositeMultigrid solver(base,cells,boundary);
        const auto& op=solver.op();
        std::vector<double> potential(op.size()),values(op.faces().size(),1.);
        for(int i=0;i<op.size();++i)
            potential[i]=std::ldexp(op.center(i)[0]<.5?1.:-1.,54);
        require(op.mean(potential)==0.,"extreme flux fixture has a nonzero gauge");
        auto& execution=solver.execution();
        const auto resident=execution.upload(potential),data=execution.upload(values);
        auto gradients=execution.array<double>(op.faces().size());
        solver.gradient(resident,gradients,data);
        const auto packed=execution.download(gradients);
        for(std::size_t i=0;i<op.faces().size();++i) {
            const auto& face=op.faces()[i];
            if(!op.has_flux_boundary(face))continue;
            const int anchor=face.left>=0?face.left:face.right;
            const double s=(face.boundary_side&1)?1.:-1.;
            const double expected=s*(1.-a*potential[anchor]);
            const double actual=op.face_gradient(potential,face,1.);
            require(std::abs(actual-expected)<=16.*std::numeric_limits<double>::epsilon(),
                "prescribed flux was cancelled against a large potential");
            require(actual==packed[i],"packed gradient differs from the shared flux row");
        }
    }
    // A pure Neumann row must not form an overflowing difference even when
    // both input potentials are finite and have opposite maximum magnitude.
    const double extreme[]={std::numeric_limits<double>::max(),-std::numeric_limits<double>::max()};
    const int sample=1;const double zero=0.;
    require(elliptic::composite_face_gradient(extreme,0,&sample,&zero,1,1.,1.,0.,true)==1.,
        "pure Neumann flux read an irrelevant overflowing field difference");
    std::cout<<"Extreme Neumann/near-Neumann scalar and packed gradients passed\n";
}

/** Reject unpaired periodic sides, inconsistent policies and bad coefficients. */
void boundary_policy_rejection() {
    using K=elliptic::FaceBoundaryKind;
    const auto base=base_mesh(3,8);
    const auto cells=make_cells(base,false);
    auto rejects=[&](auto function,const char* message) {
        bool failed=false;
        try {function();} catch(const std::exception&) {failed=true;}
        require(failed,message);
    };
    elliptic::CompositeBoundary unpaired;
    unpaired.sides={K::Periodic,K::Dirichlet,K::Dirichlet,K::Dirichlet,K::Dirichlet,K::Dirichlet};
    for(int index=0;index<6;++index)
        unpaired.conditions[index]=index==0?elliptic::FaceBoundaryCondition{K::Periodic,0.,1.}
                                           :elliptic::FaceBoundaryCondition{K::Dirichlet,1.,0.};
    rejects([&]{elliptic::CompositePoisson invalid(base,cells,unpaired);},
        "unpaired periodic side accepted");
    elliptic::CompositeBoundary mismatch;
    mismatch.sides.fill(K::Dirichlet); // conditions keep their Neumann default.
    rejects([&]{elliptic::CompositePoisson invalid(base,cells,mismatch);},
        "conditions disagreeing with sides accepted");
    const auto invalid=[&](K kind,double a,double b,const char* message) {
        elliptic::CompositeBoundary boundary;
        boundary.sides.fill(K::Dirichlet);
        for(int index=0;index<6;++index)
            boundary.conditions[index]={K::Dirichlet,1.,0.};
        boundary.sides[0]=kind;boundary.conditions[0]={kind,a,b};
        rejects([&]{elliptic::CompositePoisson invalid(base,cells,boundary);},message);
    };
    invalid(K::Dirichlet,1.,1.,"Dirichlet b!=0 accepted");
    invalid(K::Neumann,0.,2.,"Neumann b!=1 accepted");
    invalid(K::Neumann,1.,1.,"Neumann a!=0 accepted");
    invalid(K::Robin,1.,0.,"Robin b=0 accepted");
    invalid(K::Robin,-1.,1.,"Robin negative a accepted");
    invalid(K::Robin,1.,std::numeric_limits<double>::infinity(),"Robin nonfinite b accepted");
    {
        elliptic::CompositeBoundary boundary;
        boundary.sides.fill(K::Dirichlet);
        for(int index=0;index<6;++index)
            boundary.conditions[index]={K::Dirichlet,1.,0.};
        boundary.constant_nullspace=true;
        rejects([&]{elliptic::CompositePoisson invalid(base,cells,boundary);},
            "positive boundary weight kept the constant nullspace");
    }
    // A zero-weight Robin flux side is accepted and keeps a positive result.
    {
        elliptic::CompositeBoundary boundary;
        boundary.sides.fill(K::Dirichlet);
        for(int index=0;index<6;++index)
            boundary.conditions[index]={K::Dirichlet,1.,0.};
        boundary.sides[0]=K::Robin;boundary.conditions[0]={K::Robin,0.,.5};
        elliptic::CompositePoisson accepted(base,cells,boundary);
        require(!accepted.has_constant_nullspace(),"Robin/Dirichlet lost its nonsingular operator");
    }
    rejects([&]{elliptic::CompositePoisson invalid(base,cells,elliptic::BoundaryKind::RadialIsolated);},
        "Cartesian radial boundary accepted");
    rejects([&]{elliptic::CompositePoisson invalid(base,cells,elliptic::BoundaryKind::User);},
        "bare user boundary kind accepted");
}

/** Check radial geometry, regular origin, mixed AMR and independent Gauss law. */
void radial_convergence() {
    for(auto geometry:{elliptic::Geometry::Spherical,elliptic::Geometry::Cylindrical})
        for(bool refined:{false,true}) {
            const int d=geometry==elliptic::Geometry::Spherical?3:2;
            double previous_phi=0.,previous_face=0.;
            for(int n:{16,32,64}) {
                auto base=base_mesh(1,n);base.geometry=geometry;
                multigrid::CompositeMultigrid solver(base,make_cells(base,refined),
                    elliptic::BoundaryKind::RadialIsolated);
                const auto& op=solver.op();
                constexpr double q=.2, G=1.;
                std::vector<double> rho(op.size()),rhs(op.size()),exact(op.size()),error(op.size());
                double mass=0.;
                for(int i=0;i<op.size();++i) {
                    const double r=op.center(i)[0],h=op.width(i,0),left=r-.5*h,right=r+.5*h;
                    const double average_r2=(double(d)/(d+2))*
                        (std::pow(right,d+2)-std::pow(left,d+2))/
                        (std::pow(right,d)-std::pow(left,d));
                    rho[i]=1.+q*average_r2;
                    rhs[i]=-4*pi*G*rho[i];
                    mass+=rho[i]*op.volumes()[i];
                    const double radial=4*pi*G*(r*r/(2*d)+q*std::pow(r,4)/(4*(d+2)));
                    const double at_outer=geometry==elliptic::Geometry::Spherical
                        ?-4*pi*G*(1./d+q/(d+2)):0.;
                    const double radial_outer=4*pi*G*(1./(2*d)+q/(4*(d+2)));
                    exact[i]=at_outer+radial-radial_outer;
                }
                // Outer spherical value is -G*M/R; cylindrical value fixes
                // the additive logarithmic-potential gauge to Phi(R)=0.
                const double boundary=geometry==elliptic::Geometry::Spherical?-4*pi*G*mass:0.;
                std::vector<double> bc(op.faces().size());
                for(std::size_t f=0;f<bc.size();++f)
                    if(op.faces()[f].boundary_side==1)bc[f]=boundary;
                // At the finest mixed cylindrical level, A has O(h^-2)
                // coefficients. Multiplying one FP64 ulp in Phi by that
                // scale gives an O(1e-12) residual floor. Keep the physical
                // residual check while setting its relative request above
                // that floor; potential order and independent Gauss checks
                // below remain unchanged.
                const auto result=solver.solve(op.effective_rhs(rhs,bc),{3e-13,0.,300});
                if(result.report.status!=multigrid::SolveStatus::Converged)
                    std::cout<<"radial solve failed geometry="<<d<<" refined="<<refined
                        <<" n="<<n<<" cycles="<<result.report.cycles
                        <<" residual="<<result.report.residual
                        <<" target="<<result.report.target<<'\n';
                require(result.report.status==multigrid::SolveStatus::Converged,
                    "radial composite solve failed");
                for(int i=0;i<op.size();++i)error[i]=result.potential[i]-exact[i];
                const double phi_error=op.norm(error);
                double gradient_error=0.,area=0.;
                for(const auto& f:op.faces()) {
                    const double r=f.center[0];
                    const double expected=4*pi*G*(r/d+q*r*r*r/(d+2));
                    const double actual=op.face_gradient(result.potential,f,
                        f.boundary_side==1?boundary:0.);
                    gradient_error+=f.area*std::pow(actual-expected,2);
                    area+=f.area;
                }
                gradient_error=std::sqrt(gradient_error/area);
                std::cout<<"radial geometry="<<d<<" refined="<<refined<<" n="<<n
                    <<" phi="<<phi_error<<" face="<<gradient_error
                    <<" cycles="<<result.report.cycles<<'\n';
                if(previous_phi) {
                    require(std::log2(previous_phi/phi_error)>=1.8,
                        "radial potential order below 1.8");
                    // With exact volume-average rho, summing A*Phi=b from
                    // the regular origin gives A_f*grad(Phi)_f =
                    // 4*pi*G*sum_inside(rho_i*V_i). The analytic polynomial
                    // has this identical enclosed mass at every face, so
                    // face error is controlled by solve residual, not h^2.
                    // Require a stronger absolute Gauss-law bound instead
                    // of an undefined quotient of residual-floor errors.
                    require(gradient_error<1e-9,
                        "radial face violates the exact discrete Gauss-law budget");
                }
                previous_phi=phi_error;previous_face=gradient_error;
                require(std::abs(mass-(1./d+q/(d+2)))<1e-13,
                    "radial physical mass integral mismatch");
            }
        }
}

/** Check independent enclosed-mass force with a hollow spherical shell. */
void curved_gauss_law() {
    constexpr double G=1.,q=.2,rmin=.5;
    constexpr int dim=3;
    for(bool refined:{false,true}) {
        double previous=0.;
        for(int n:{8,16}) {
            auto base=base_mesh(dim,n);
            base.geometry=elliptic::Geometry::Spherical;
            base.origin[0]=rmin;base.spacing[0]=1./n;
            base.origin[1]=0.;base.spacing[1]=pi/n;
            base.origin[2]=0.;base.spacing[2]=2*pi/n;
            multigrid::CompositeMultigrid solver(base,make_cells(base,refined),
                elliptic::BoundaryKind::CurvilinearIsolated);
            const auto& op=solver.op();
            std::vector<double> rho(op.size()),rhs(op.size()),bc(op.faces().size());
            for(int i=0;i<op.size();++i) {
                const double r=op.center(i)[0],h=op.width(i,0);
                const double lo=r-h/2,hi=r+h/2;
                const double average_r2=3.*(std::pow(hi,5)-std::pow(lo,5))
                        /(5.*(std::pow(hi,3)-std::pow(lo,3)));
                rho[i]=1.+q*average_r2;
                rhs[i]=-4*pi*G*rho[i];
            }
            Physical::Gravity::GravityBoundary tree(op);tree.update(rho);
            bc=tree.values(op,G);
            const auto solved=solver.solve(op.effective_rhs(rhs,bc),{1e-10,0.,300});
            require(solved.report.status==multigrid::SolveStatus::Converged,
                "curved enclosed-mass solve failed");
            double numerator=0.,denominator=0.;
            for(std::size_t index=0;index<op.faces().size();++index) {
                const auto& face=op.faces()[index];
                const double r=face.center[0];
                double expected=0.;
                if(face.axis==0) {
                    const double mass=4*pi*((std::pow(r,3)-std::pow(rmin,3))/3.
                            +q*(std::pow(r,5)-std::pow(rmin,5))/5.);
                    expected=G*mass/(r*r);
                }
                const double actual=op.face_gradient(solved.potential,face,
                    face.boundary_side>=0?bc[index]:0.);
                numerator+=face.area*(actual-expected)*(actual-expected);
                denominator+=face.area;
            }
            const double error=std::sqrt(numerator/denominator);
            std::cout<<"curved Gauss dimension="<<dim<<" refined="<<refined
                <<" n="<<n<<" force="<<error<<" cycles="<<solved.report.cycles;
            if(previous)std::cout<<" order="<<std::log2(previous/error);
            std::cout<<'\n';
            if(previous)require(std::log2(previous/error)>=1.8,
                "curved enclosed-mass force order below 1.8");
            previous=error;
        }
    }
}

/** Compare physical isolated boundary kernels against direct cell quadrature. */
void curved_boundary_integral() {
    constexpr double nodes[]{-0.7745966692414834,0.,0.7745966692414834};
    constexpr double weights[]{5./9.,8./9.,5./9.};
    for(auto geometry:{elliptic::Geometry::Cylindrical,elliptic::Geometry::Spherical})
        for(int dim:{2,3}) {
            if(geometry==elliptic::Geometry::Cylindrical && dim==2) continue;
            auto base=base_mesh(dim,8);base.geometry=geometry;
            base.origin[0]=.5;base.spacing[0]=1./8;
            if(dim==2){base.origin[1]=0.;base.spacing[1]=2*pi/8;}
            else if(geometry==elliptic::Geometry::Cylindrical) {
                base.origin[1]=-.5;base.spacing[1]=1./8;
                base.origin[2]=0.;base.spacing[2]=2*pi/8;
            } else {
                base.origin[1]=.3;base.spacing[1]=(pi-.6)/8;
                base.origin[2]=0.;base.spacing[2]=2*pi/8;
            }
            const auto position=[=](const std::array<double,3>& native) {
                const double r=native[0],phi=native[dim-1];
                if(dim==2)return std::array<double,3>{r*std::cos(phi),r*std::sin(phi),0.};
                if(geometry==elliptic::Geometry::Cylindrical)
                    return std::array<double,3>{r*std::cos(phi),r*std::sin(phi),native[1]};
                return std::array<double,3>{r*std::sin(native[1])*std::cos(phi),
                    r*std::sin(native[1])*std::sin(phi),r*std::cos(native[1])};
            };
            elliptic::CompositePoisson op(base,make_cells(base,true),
                elliptic::BoundaryKind::CurvilinearIsolated);
            std::vector<double> density(op.size());
            for(int i=0;i<op.size();++i) {
                const auto x=position(op.center(i));
                const double a=(x[0]-.7)/.18,b=(x[1]-.4)/.18,c=x[2]/.18;
                density[i]=std::exp(-.5*(a*a+b*b+(dim==3?c*c:0.)));
            }
            Physical::Gravity::GravityBoundary tree(op);tree.update(density);
            const auto actual=tree.values(op,1.),tighter=tree.values(op,1.,.125);
            double error=0.,tight_error=0.,scale=0.;int checked=0;
            for(std::size_t f=0;f<op.faces().size() && checked<12;++f) {
                if(op.faces()[f].boundary_side<0)continue;
                const auto point=position(op.faces()[f].center);
                double reference=0.;
                for(int i=0;i<op.size();++i) {
                    const auto center=op.center(i);
                    double total=0.,potential=0.;
                    for(int u=0;u<3;++u)for(int v=0;v<3;++v)
                        for(int w=0;w<(dim==3?3:1);++w) {
                            auto x=center;
                            x[0]+=.5*op.width(i,0)*nodes[u];
                            x[1]+=.5*op.width(i,1)*nodes[v];
                            if(dim==3)x[2]+=.5*op.width(i,2)*nodes[w];
                            const auto cart=position(x);
                            const double jacobian=dim==3 && geometry==elliptic::Geometry::Spherical
                                ?x[0]*x[0]*std::sin(x[1]):x[0];
                            const double weight=weights[u]*weights[v]*(dim==3?weights[w]:1.)*jacobian;
                            double distance=0.;
                            for(int a=0;a<3;++a)distance+=std::pow(point[a]-cart[a],2);
                            const double kernel=dim==2?2.*std::log(std::sqrt(distance)/1.5)
                                :-1./std::sqrt(distance);
                            total+=weight;potential+=weight*kernel;
                        }
                    reference+=density[i]*op.volumes()[i]*potential/total;
                }
                error+=std::pow(actual[f]-reference,2);
                tight_error+=std::pow(tighter[f]-reference,2);
                scale+=reference*reference;
                ++checked;
            }
            const double relative=std::sqrt(error/scale),tight=std::sqrt(tight_error/scale);
            std::cout<<"curved boundary geometry="<<static_cast<int>(geometry)
                <<" dim="<<dim<<" faces="<<checked<<" relative="<<relative
                <<" tighter="<<tight<<'\n';
            require(checked>0 && relative<.03 && tight<relative,
                "curved isolated multipole differs from direct physical quadrature");
        }
}

/** Keep the same compact mass fixed while moving the outer radial boundary. */
void curved_domain_extension() {
    for(auto geometry:{elliptic::Geometry::Cylindrical,elliptic::Geometry::Spherical})
        for(int dim:{2,3}) {
            if(geometry==elliptic::Geometry::Cylindrical && dim==2) continue;
            std::vector<double> reference;
            double reference_norm=0.,difference=0.;int compared=0;
            for(int radial_cells:{8,16}) {
                auto base=base_mesh(dim,8);
                base.geometry=geometry;base.cells[0]=radial_cells;
                base.origin[0]=.5;base.spacing[0]=.125;
                if(dim==2){base.origin[1]=0.;base.spacing[1]=2*pi/8;}
                else if(geometry==elliptic::Geometry::Cylindrical) {
                    base.origin[1]=-.5;base.spacing[1]=.125;
                    base.origin[2]=0.;base.spacing[2]=2*pi/8;
                } else {
                    base.origin[1]=.3;base.spacing[1]=(pi-.6)/8;
                    base.origin[2]=0.;base.spacing[2]=2*pi/8;
                }
                multigrid::CompositeMultigrid solver(base,make_cells(base,false),
                    elliptic::BoundaryKind::CurvilinearIsolated);
                const auto& op=solver.op();
                std::vector<double> density(op.size()),rhs(op.size());
                for(int i=0;i<op.size();++i) {
                    const auto x=op.center(i);
                    const double radial=(x[0]-.85)/.25;
                    const double envelope=std::abs(radial)<1.
                        ?std::pow(1.-radial*radial,4):0.;
                    density[i]=envelope*(1.+.1*std::cos(2*x[dim-1]));
                    rhs[i]=-4*pi*density[i];
                }
                Physical::Gravity::GravityBoundary tree(op);tree.update(density);
                const auto boundary=tree.values(op,1.);
                const auto solved=solver.solve(op.effective_rhs(rhs,boundary),
                    {1e-10,0.,300});
                require(solved.report.status==multigrid::SolveStatus::Converged &&
                        solved.report.residual<=solved.report.target,
                        "curved expanded-domain solve failed");
                // Compare only the same inner-domain physical faces. The 2D
                // logarithmic gauge changes with Rref, but its gradient does not.
                if(radial_cells==8) {
                    for(std::size_t f=0;f<op.faces().size();++f) {
                        const auto& face=op.faces()[f];
                        if(face.center[0]>1.25)continue;
                        reference.push_back(op.face_gradient(solved.potential,face,
                            face.boundary_side>=0?boundary[f]:0.));
                    }
                } else {
                    auto inner=base;inner.cells[0]=8;
                    elliptic::CompositePoisson inner_op(inner,make_cells(inner,false),
                        elliptic::BoundaryKind::CurvilinearIsolated);
                    std::size_t reference_index=0;
                    for(const auto& face:inner_op.faces()) {
                        if(face.center[0]>1.25)continue;
                        bool found=false;
                        for(std::size_t f=0;f<op.faces().size();++f) {
                            const auto& candidate=op.faces()[f];
                            if(candidate.axis!=face.axis)continue;
                            bool same=true;
                            for(int a=0;a<dim;++a)
                                same &= std::abs(candidate.center[a]-face.center[a])<1e-12;
                            if(!same)continue;
                            const double gradient=op.face_gradient(solved.potential,candidate,
                                candidate.boundary_side>=0?boundary[f]:0.);
                            const double expected=reference.at(reference_index);
                            difference+=(gradient-expected)*(gradient-expected);
                            reference_norm+=expected*expected;
                            ++compared;found=true;break;
                        }
                        require(found,"curved expanded domain lost an inner physical face");
                        ++reference_index;
                    }
                    require(reference_index==reference.size(),
                        "curved expanded-domain face mapping mismatch");
                }
            }
            const double relative=std::sqrt(difference/reference_norm);
            std::cout<<"curved domain geometry="<<static_cast<int>(geometry)
                <<" dim="<<dim<<" faces="<<compared<<" force_change="<<relative<<'\n';
            require(compared>0 && relative<.02,
                "curved isolated force changed with empty outer-domain extension");
        }
}

void periodic_gravity_source_probe() {
    using namespace Physical::Gravity;
    const auto dump=[](const auto& x) {
        std::cout<<'[';bool first=true;
        for(auto value:x){if(!first)std::cout<<',';first=false;std::cout<<value;}std::cout<<']';
    };
    std::cout<<std::setprecision(17)<<"{\"cases\":[";bool first=true;
    for(bool mixed:{false,true})for(int lane=0;lane<6;++lane) {
        auto base=base_mesh(2,4);
        multigrid::CompositeMultigrid solver(base,make_cells(base,mixed),elliptic::BoundaryKind::Periodic);
        const auto& op=solver.op();auto& execution=solver.execution();
        std::vector<double> density(op.size());
        for(int i=0;i<op.size();++i) {
            if(lane==0)density[i]=1.e7;
            if(lane==1)density[i]=1.e7+(i%3-1)*.5;
            if(lane==2)density[i]=1.e12+(i%3-1)*.000244140625;
            if(lane==3)density[i]=(1.+(i%3)*.1)*1.e100;
            if(lane==4)density[i]=(2.+i%3)*1.e-310;
            if(lane==5)density[i]=(1.+i%3)*std::numeric_limits<double>::denorm_min();
        }
        const auto rho=execution.upload(density);
        auto rhs=execution.array<double>(op.size());
        const double factor=-4.*constants::math::pi*constants::gravity::cgs::gravitational_constant;
        const double mean=solver.mean(rho);
        execution.difference_scale(rhs,rho,mean,factor);
        solver.project(rhs);auto host_source=execution.download(rhs);
        // Retain the pre-fix provider formula as a named diagnostic, not a
        // second production path or an accepted physical source.
        execution.linear(rhs,factor,rho,0.,{},-factor*mean);
        solver.project(rhs);auto legacy=execution.download(rhs);
        auto legacy_bounds=bound_periodic_gravity_source(op,density,legacy);
        require(legacy_bounds.status==GravitySourceBoundStatus::Bounded,"legacy diagnostic bound missing");
        auto bounds=bound_periodic_gravity_source(op,density,host_source);
        require(bounds.status==GravitySourceBoundStatus::Bounded,"periodic provider source bound missing");
        // Existing UniformGravity ordering is a separately named diagnostic.
        auto subtract_first=density;const double scalar_mean=op.mean(density);
        for(double& x:subtract_first)x=factor*(x-scalar_mean);
        op.project(subtract_first);
        auto alternate=bound_periodic_gravity_source(op,density,subtract_first);
        require(alternate.status==GravitySourceBoundStatus::Bounded,"subtract-first source bound missing");
        require(host_source==subtract_first,"corrected host source differs from subtract-first reference ordering");
        for(int path=0;path<3;++path) {
            const auto& actual=path==0?host_source:(path==1?subtract_first:legacy);
            const auto& bound=path==0?bounds:(path==1?alternate:legacy_bounds);
            if(!first)std::cout<<',';first=false;
            std::cout<<"{\"mixed\":"<<mixed<<",\"lane\":"<<lane<<",\"path\":"
                <<path<<",\"density\":";dump(density);std::cout<<",\"source\":";dump(actual);
            std::cout<<",\"weights\":";dump(op.norm_weights());
            std::cout<<",\"lower\":";dump(bound.lower);std::cout<<",\"upper\":";dump(bound.upper);
            std::cout<<",\"cellBounds\":";dump(bound.cell_bounds);
            std::cout<<",\"normUpper\":"<<bound.norm_upper<<'}';
        }
        if(lane==0)require(bounds.norm_upper==0.&&alternate.norm_upper==0.,
            "constant positive density generated artificial source error floor");
        if(lane==5) {
            require(bounds.norm_upper>0.&&alternate.norm_upper>0.,
                "unrepresentable periodic contrast got exact-zero certificate");
            for(double x:host_source)require(x==0.,"tiny source fixture no longer rounds to zero");
        }
        auto bad_output=host_source;bad_output[0]=std::numeric_limits<double>::quiet_NaN();
        require(bound_periodic_gravity_source(op,density,bad_output).status==GravitySourceBoundStatus::InvalidInput,
            "NaN periodic output accepted");
        auto invalid=density;invalid[0]=-1.;
        require(bound_periodic_gravity_source(op,invalid,host_source).status==GravitySourceBoundStatus::InvalidInput,
            "negative periodic density accepted");
        require(bound_periodic_gravity_source(op,density,{}).status==GravitySourceBoundStatus::InvalidInput,
            "missing periodic source accepted");
        require(bound_periodic_gravity_source(op,{},host_source).status==GravitySourceBoundStatus::InvalidInput,
            "missing periodic density accepted");
    }
    auto base=base_mesh(2,4);
    elliptic::CompositePoisson isolated(base,make_cells(base,false),elliptic::BoundaryKind::Dirichlet);
    require(bound_periodic_gravity_source(isolated,{},{}).status==GravitySourceBoundStatus::UnsupportedNonperiodic,
        "nonperiodic source treated as contrast");
    std::cout<<"],\"negativePass\":true}\n";
}

void constant_mode_projection_probe() {
    using namespace elliptic;
    const auto dump=[](const auto& x) {
        std::cout<<'[';bool first=true;
        for(auto value:x){if(!first)std::cout<<',';first=false;std::cout<<value;}std::cout<<']';
    };
    std::cout<<std::setprecision(17)<<"{\"cases\":[";bool first=true;
    for(bool mixed:{false,true})for(bool periodic:{false,true})for(int lane=0;lane<5;++lane) {
        auto base=base_mesh(2,4);
        CompositePoisson op(base,make_cells(base,mixed),
            periodic?BoundaryKind::Periodic:BoundaryKind::Dirichlet);
        std::vector<double> input(op.size());
        for(int i=0;i<op.size();++i) {
            if(lane==1)input[i]=1.e7;
            if(lane==2)input[i]=1.e12+(i%3-1)*.000244140625;
            if(lane==3)input[i]=(i%2?1.:-1.)*(1.e100+i*1.e85);
            if(lane==4)input[i]=(i%3-1)*std::numeric_limits<double>::denorm_min();
        }
        auto actual=input;op.project(actual);
        auto bound=op.bound_constant_mode_projection_roundoff(input,actual);
        require(bound.status==BoundaryErrorStatus::Bounded,"projection ledger unavailable");
        if(lane==0)require(bound.norm_upper==0.,"projection introduced zero budget floor");
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"mixed\":"<<mixed<<",\"periodic\":"<<periodic<<",\"lane\":"<<lane
            <<",\"input\":";dump(input);std::cout<<",\"computed\":";dump(actual);
        std::cout<<",\"weights\":";dump(op.norm_weights());
        std::cout<<",\"cellBounds\":";dump(bound.cell_bounds);
        std::cout<<",\"normUpper\":"<<bound.norm_upper<<'}';
        if(lane==0) {
            auto wrong=actual;wrong[0]=1.;
            require(op.bound_constant_mode_projection_roundoff(input,wrong).cell_bounds[0]>=1.,
                "wrong projected array obtained zero error");
            auto invalid=input;invalid[0]=std::numeric_limits<double>::quiet_NaN();
            require(op.bound_constant_mode_projection_roundoff(invalid,actual).status==BoundaryErrorStatus::InvalidInput,
                "NaN projection input accepted");
            require(op.bound_constant_mode_projection_roundoff({},actual).status==BoundaryErrorStatus::InvalidInput,
                "missing projection input accepted");
        }
    }
    std::cout<<"],\"negativePass\":true}\n";
}

/** Test-private companion for the nonperiodic physical source, preserving the
 * original interval formula and stored-weight error assertions. */
using Physical::Gravity::GravitySourceBounds;
using Physical::Gravity::GravitySourceBoundStatus;

inline GravitySourceBounds bound_uncentered_gravity_source(
    const arch::elliptic::CompositePoisson& op,std::span<const double> density,
    std::span<const double> computed_source) {
    GravitySourceBounds result{};
    if(op.periodic_boundary()) {
        result.status=GravitySourceBoundStatus::UnsupportedPeriodic;return result;
    }
    if(density.size()!=static_cast<std::size_t>(op.size())
        ||computed_source.size()!=density.size())return result;
    const auto down=[](double v){return v<=0.?0.:std::nextafter(v,0.);};
    const auto product_upper=[](double a,double b){
        return a==0.||b==0.?0.:std::nextafter(a*b,std::numeric_limits<double>::infinity());
    };
    const double pi=arch::constants::math::pi;
    const double G=arch::constants::gravity::cgs::gravitational_constant;
    const double factor_lower=down(down(4.*down(pi))*G);
    const double factor_upper=product_upper(product_upper(4.,
        std::nextafter(pi,std::numeric_limits<double>::infinity())),G);
    result.lower.resize(density.size());result.upper.resize(density.size());
    result.cell_bounds.resize(density.size());
    for(std::size_t i=0;i<density.size();++i) {
        const double rho=density[i],actual=computed_source[i];
        if(!std::isfinite(rho)||rho<0.||!std::isfinite(actual))return result;
        if(rho>0.&&actual==0.) {
            result.status=GravitySourceBoundStatus::CollapsedToZero;return result;
        }
        const double lo=rho==0.?0.:-product_upper(factor_upper,rho);
        const double hi=rho==0.?0.:-down(factor_lower*rho);
        result.lower[i]=lo;result.upper[i]=hi;
        const double distance=std::max(std::abs(actual-lo),std::abs(actual-hi));
        result.cell_bounds[i]=distance==0.?0.:
            std::nextafter(distance,std::numeric_limits<double>::infinity());
        if(!std::isfinite(lo)||!std::isfinite(hi)||!std::isfinite(result.cell_bounds[i])) {
            result.status=GravitySourceBoundStatus::Overflow;return result;
        }
    }
    const auto norm=op.norm_interval(result.cell_bounds);
    if(norm.status!=arch::elliptic::BoundaryErrorStatus::Bounded) {
        result.status=norm.status==arch::elliptic::BoundaryErrorStatus::Overflow?
            GravitySourceBoundStatus::Overflow:GravitySourceBoundStatus::InvalidInput;
        return result;
    }
    result.norm_upper=norm.upper;result.status=GravitySourceBoundStatus::Bounded;return result;
}

void isolated_gravity_source_probe() {
    using namespace Physical::Gravity;
    auto base=base_mesh(2,4);
    elliptic::CompositePoisson op(base,make_cells(base,true),elliptic::BoundaryKind::Dirichlet);
    const double samples[]{0.,1.e-310,1.e-300,1.e-10,1.,1.e7,1.e100,1.e300,
        std::numeric_limits<double>::max()};
    std::vector<double> density(op.size()),source(op.size());
    const double factor=-4.*constants::math::pi*constants::gravity::cgs::gravitational_constant;
    for(int i=0;i<op.size();++i){density[i]=samples[i%9];source[i]=factor*density[i];}
    const auto bounds=bound_uncentered_gravity_source(op,density,source);
    require(bounds.status==GravitySourceBoundStatus::Bounded,
        "isolated source construction bounds missing");
    const auto dump=[](const auto& x) {
        std::cout<<'[';bool first=true;
        for(auto value:x){if(!first)std::cout<<',';first=false;std::cout<<value;}std::cout<<']';
    };
    std::cout<<std::setprecision(17)<<"{\"density\":";dump(density);
    std::cout<<",\"source\":";dump(source);std::cout<<",\"lower\":";dump(bounds.lower);
    std::cout<<",\"upper\":";dump(bounds.upper);std::cout<<",\"cellBounds\":";dump(bounds.cell_bounds);
    std::cout<<",\"weights\":";dump(op.norm_weights());std::cout<<",\"normUpper\":"<<bounds.norm_upper;
    auto zero=density;std::fill(zero.begin(),zero.end(),0.);
    require(bound_uncentered_gravity_source(op,{},source).status==GravitySourceBoundStatus::InvalidInput
        &&bound_uncentered_gravity_source(op,density,{}).status==GravitySourceBoundStatus::InvalidInput,
        "mismatched nonperiodic source spans accepted");
    require(bound_uncentered_gravity_source(op,zero,zero).norm_upper==0.,
        "exact zero source acquired hidden floor");
    auto tiny=density;std::fill(tiny.begin(),tiny.end(),std::numeric_limits<double>::denorm_min());
    require(bound_uncentered_gravity_source(op,tiny,zero).status==GravitySourceBoundStatus::CollapsedToZero,
        "positive unrepresentable source accepted as zero");
    tiny[0]=-1.;
    require(bound_uncentered_gravity_source(op,tiny,source).status==GravitySourceBoundStatus::InvalidInput,
        "negative physical density accepted");
    auto invalid=source;invalid[0]=std::numeric_limits<double>::quiet_NaN();
    require(bound_uncentered_gravity_source(op,density,invalid).status==GravitySourceBoundStatus::InvalidInput,
        "NaN source acquired certificate");
    invalid=source;invalid[8]=std::numeric_limits<double>::max();
    require(bound_uncentered_gravity_source(op,density,invalid).status==GravitySourceBoundStatus::Overflow,
        "unrepresentable error distance acquired finite certificate");
    auto cart=base_mesh(2,4);
    elliptic::CompositePoisson periodic(cart,make_cells(cart,false),elliptic::BoundaryKind::Periodic);
    require(bound_uncentered_gravity_source(periodic,{},{}).status==GravitySourceBoundStatus::UnsupportedPeriodic,
        "total density certified as periodic contrast source");
    std::cout<<",\"negativePass\":true}\n";
}

void curved_manufactured(bool singular=false, bool seam_refined=false) {
    for(auto geometry:{elliptic::Geometry::Cylindrical,elliptic::Geometry::Spherical})
        for(int dim:{2,3}) for(bool refined:{false,true}) {
            if(geometry==elliptic::Geometry::Cylindrical && dim==2) continue;
            if(seam_refined && (!singular || !refined || dim!=2)) continue;
            double previous=0.,previous_force=0.,previous_interface=0.;
            for(int n:{8,16}) {
                auto base=base_mesh(dim,n);
                base.geometry=geometry;base.origin[0]=singular?0.:.5;base.spacing[0]=1./n;
                if(dim==2) {base.origin[1]=0.;base.spacing[1]=2*pi/n;}
                else if(geometry==elliptic::Geometry::Cylindrical) {
                    base.origin[1]=-.5;base.spacing[1]=1./n;
                    base.origin[2]=0.;base.spacing[2]=2*pi/n;
                } else {
                    base.origin[1]=singular?0.:.3;
                    base.spacing[1]=(singular?pi:pi-.6)/n;
                    base.origin[2]=0.;base.spacing[2]=2*pi/n;
                }
                const auto exact=[=](const std::array<double,3>& x) {
                    constexpr double e=.1;
                    if(dim==2)return x[0]*x[0]*(1.+e*std::cos(2*x[1]));
                    if(geometry==elliptic::Geometry::Cylindrical)
                        return x[0]*x[0]*(1.+e*std::cos(2*x[2]))+x[1]*x[1];
                    return x[0]*x[0]*(1.+e*std::sin(x[1])*std::sin(x[1])*std::cos(2*x[2]));
                };
                multigrid::CompositeMultigrid solver(base,
                    seam_refined ? make_origin_seam_cells(base) : make_cells(base,refined),
                    elliptic::BoundaryKind::CurvilinearIsolated);
                const auto& op=solver.op();
                std::vector<double> rhs(op.size(),dim==2?-4.:-6.),bc(op.faces().size()),error(op.size());
                for(std::size_t f=0;f<bc.size();++f)
                    if(op.faces()[f].boundary_side>=0)bc[f]=exact(op.faces()[f].center);
                const auto result=solver.solve(op.effective_rhs(rhs,bc),{1e-11,0.,300});
                std::cout<<"curved manufactured singular="<<singular<<" geometry="<<static_cast<int>(geometry)
                    <<" dim="<<dim<<" refined="<<refined<<" seam="<<seam_refined<<" n="<<n
                    <<" cycles="<<result.report.cycles<<" residual="<<result.report.residual
                    <<" target="<<result.report.target<<std::flush;
                require(result.report.status==multigrid::SolveStatus::Converged,
                    "curved manufactured solve did not converge");
                for(int i=0;i<op.size();++i)error[i]=result.potential[i]-exact(op.center(i));
                const double norm=op.norm(error);
                double force_squared=0.,face_area=0.,interface_squared=0.,interface_area=0.;
                for(const auto& face:op.faces()) {
                    const auto& x=face.center;
                    require(face.area>0., "singular composite face has nonpositive area");
                    if(singular) {
                        require(!(face.axis==0 && std::abs(x[0])<1e-14),
                            "zero-area origin emitted a face flux");
                        if(dim==3 && geometry==elliptic::Geometry::Spherical)
                            require(!(face.axis==1 &&
                                (std::abs(x[1])<1e-14 || std::abs(x[1]-pi)<1e-14)),
                                "zero-area spherical pole emitted a face flux");
                    }
                    constexpr double e=.1;
                    const double phi=dim==2?x[1]:x[2];
                    double expected=0.;
                    if(face.axis==0)expected=2*x[0]*(1.+e*(dim==3 && geometry==elliptic::Geometry::Spherical
                        ?std::sin(x[1])*std::sin(x[1]):1.)*std::cos(2*phi));
                    else if(dim==3 && face.axis==1 && geometry==elliptic::Geometry::Cylindrical)
                        expected=2*x[1];
                    else if(dim==3 && face.axis==1)
                        expected=2*e*x[0]*std::sin(x[1])*std::cos(x[1])*std::cos(2*phi);
                    else expected=-2*e*x[0]*(dim==3 && geometry==elliptic::Geometry::Spherical
                        ?std::sin(x[1]):1.)*std::sin(2*phi);
                    const double value=op.face_gradient(result.potential,face,
                        face.boundary_side>=0?bc[&face-&op.faces()[0]]:0.);
                    const double mismatch=value-expected;
                    force_squared+=face.area*mismatch*mismatch;
                    face_area+=face.area;
                    if(face.left>=0 && face.right>=0 &&
                        op.cells()[face.left].level!=op.cells()[face.right].level) {
                        interface_squared+=face.area*mismatch*mismatch;
                        interface_area+=face.area;
                    }
                }
                const double force=std::sqrt(force_squared/face_area);
                const double interface_force=interface_area>0.
                    ? std::sqrt(interface_squared/interface_area) : 0.;
                std::cout<<" error="<<norm<<" force="<<force;
                if(interface_area>0.)std::cout<<" interface="<<interface_force;
                if(previous)std::cout<<" orders="<<std::log2(previous/norm)<<','
                    <<std::log2(previous_force/force);
                std::cout<<'\n';
                if(previous) {
                    require(std::log2(previous/norm)>=1.8,
                        "curved manufactured potential order below 1.8");
                    require(std::log2(previous_force/force)>=1.8,
                        "curved manufactured face-force order below 1.8");
                    if(singular && previous_interface>0. && interface_force>0.)
                        require(std::log2(previous_interface/interface_force)>=1.8,
                            "singular coarse/fine face-force order below 1.8");
                }
                previous=norm;previous_force=force;
                previous_interface=interface_force;
            }
        }
}

/** Axisymmetric -Laplacian on the existing V/A composite operator.
 * Phi=a(r^2+z^2) has source -6a. An even quartic perturbation provides
 * an independent truncation/convergence check on the axis and mixed AMR.
 * z spans eight length units deliberately: it is never an azimuthal turn.
 */
void axisymmetric_prescribed_poisson(bool device=false) {
    using K=elliptic::FaceBoundaryKind;
    std::shared_ptr<multigrid::CompositeExecution> execution;
    if(device) {
#if ARCH_CUDA_BUILD_ENABLED
        // Reuse the production executor; the same independent field/flux and
        // Gauss assertions below apply to both backends at their original limits.
        execution=cuda::make_cuda_gravity_execution(nullptr,0,{})->numeric();
#else
        throw std::runtime_error("Device Poisson requires a CUDA build");
#endif
    }
    for(double inner:{0.,.5})for(bool refined:{false,true})for(bool quartic:{false,true}) {
        double previous_phi=0.,previous_flux=0.;
        for(int n:{8,16,32}) {
            auto base=base_mesh(2,n);base.geometry=elliptic::Geometry::Cylindrical;
            base.origin={inner,-4.,0.};base.spacing={1./n,8./n,1.};
            elliptic::CompositeBoundary boundary;
            boundary.sides={K::Neumann,K::Dirichlet,K::Dirichlet,K::Dirichlet,K::Neumann,K::Neumann};
            if(inner>0.)boundary.sides[0]=K::Dirichlet;
            for(int side=0;side<6;++side)boundary.conditions[side]={boundary.sides[side],
                boundary.sides[side]==K::Dirichlet?1.:0.,boundary.sides[side]==K::Dirichlet?0.:1.};
            const double perturbation=quartic?.1:0.;
            const auto exact=[=](const std::array<double,3>& x) {
                const double r2=x[0]*x[0],z2=x[1]*x[1];
                return r2+z2+perturbation*(r2*r2+z2*z2);
            };
            const auto gradient=[=](const std::array<double,3>& x) {
                return std::array<double,3>{2.*x[0]+4.*perturbation*x[0]*x[0]*x[0],
                    2.*x[1]+4.*perturbation*x[1]*x[1]*x[1],0.};
            };
            multigrid::CompositeMultigrid solver(base,make_cells(base,refined),boundary,execution);
            const auto& op=solver.op();std::vector<double> rhs(op.size()),error(op.size());
            for(int c=0;c<op.size();++c) {const auto x=op.center(c);
                rhs[c]=-6.-perturbation*(16.*x[0]*x[0]+12.*x[1]*x[1]);}
            const auto values=continuum_boundary_values(op,boundary,exact,gradient);
            const auto result=solver.solve(op.effective_rhs(rhs,values),{1e-12,0.,300});
            require(result.report.status==multigrid::SolveStatus::Converged&&result.report.residual<=result.report.target,
                "axisymmetric prescribed Poisson solve failed");
            for(int c=0;c<op.size();++c)error[c]=result.potential[c]-exact(op.center(c));
            const double phi=op.norm(error);double flux=0.;
            continuum_face_error(op,result.potential,values,gradient,flux);
            long double source=0.,surface=0.;
            for(int c=0;c<op.size();++c)source-=static_cast<long double>(rhs[c])*op.volumes()[c];
            for(std::size_t f=0;f<op.faces().size();++f)if(op.faces()[f].boundary_side>=0) {
                const auto& face=op.faces()[f];
                surface+=(face.boundary_side%2?1.L:-1.L)*face.area*op.face_gradient(result.potential,face,values[f]);
            }
            const double gauss=static_cast<double>(std::abs(surface-source)/std::abs(source));
            require(gauss<5e-10,"axisymmetric prescribed Gauss balance failed");
            if(!quartic)require(phi<1e-8&&flux<1e-7,"axisymmetric quadratic exactness budget");
            double phi_order=0.,flux_order=0.;
            if(quartic&&previous_phi>0.) {
                phi_order=std::log2(previous_phi/phi);flux_order=std::log2(previous_flux/flux);
                require(phi_order>=1.8&&flux_order>=1.8,"axisymmetric prescribed second order budget");
            }
            std::cout<<"AXISYMMETRIC_POISSON device="<<device<<" inner="<<inner<<" mixed="<<refined<<" quartic="<<quartic
                <<" n="<<n<<" cells="<<op.size()<<" cycles="<<result.report.cycles
                <<" phi_error="<<phi<<" flux_error="<<flux<<" phi_order="<<phi_order<<" flux_order="<<flux_order
                <<" gauss_error="<<gauss<<'\n';
            previous_phi=phi;previous_flux=flux;
        }
    }
    std::cout<<"AXISYMMETRIC_PRESCRIBED_POISSON_PASS\n";
}

/** Binary64 inputs and independently rounded 500-digit mpmath elliptic-K values. */
void test_complete_elliptic_oracles() {
    constexpr double oracle[][2]{
        {0x1.0000000000000p+0,0x1.921fb54442d18p+0},
        {0x1.fffffffffffffp-1,0x1.921fb54442d18p+0},
        {0x1.ccccccccccccdp-1,0x1.9cc8f4cb78b28p+0},
        {0x1.0000000000000p-1,0x1.daa4a35759e4bp+0},
        {0x1.fffffffffffffp-2,0x1.daa4a35759e4bp+0},
        {0x1.0000000000001p-1,0x1.daa4a35759e4bp+0},
        {0x1.999999999999ap-4,0x1.49feec2073f58p+1},
        {0x1.47ae147ae147bp-7,0x1.d90aa525f5669p+1},
        {0x1.a36e2eb1c432dp-14,0x1.7f763323228b6p+2},
        {0x1.5798ee2308c3ap-27,0x1.5317a1c62b262p+3},
        {0x1.cd2b297d889bcp-54,0x1.3ce95eba4f8b5p+4},
        {0x1.dee7a4ad4b81fp-167,0x1.d79b7cd4326c1p+5},
        {0x1.bff2ee48e0530p-333,0x1.d20fec1473dd9p+6},
        {0x1.87e92154ef7acp-665,0x1.cf4a23b494965p+7},
        {0x1.56e1fc2f8f359p-997,0x1.5ac628af779efp+8},
        {0x0.0000000000001p-1022,0x1.759b3876c6bfdp+8},
    };
    double maximum=0.;
    for(const auto& row:oracle) {
        const double actual=Physical::Gravity::complete_elliptic_first_kind(row[0]);
        require(std::isfinite(actual),"elliptic K oracle produced nonfinite");
        maximum=std::max(maximum,std::abs(actual/row[1]-1.));
    }
    require(maximum<2e-15,"elliptic K high precision oracle mismatch");
    for(double invalid:{0.,-1.,2.,std::numeric_limits<double>::quiet_NaN()})
        require(std::isnan(Physical::Gravity::complete_elliptic_first_kind(invalid)),"invalid K silently repaired");
    require(std::isnan(Physical::Gravity::ring_average_inverse_distance(1.,1.,0.)),"singular ring silently softened");
    for(double separation:{1e-200,1e-300,std::numeric_limits<double>::denorm_min()}) {
        const double expected=(std::log(8.)-std::log(separation))/pi;
        const double actual=Physical::Gravity::ring_average_inverse_distance(1.,1.,separation);
        require(std::isfinite(actual)&&std::abs(actual/expected-1.)<2e-15,"noncoincident near ring was rejected");
    }
    std::cout<<"ELLIPTIC_K_ORACLE_PASS samples="<<std::size(oracle)<<" max_relative="<<maximum<<'\n';
}

/** Independent full-phi quadrature for the three-dimensional Newton kernel.
 * It uses neither the elliptic series nor tree moments. The five-point
 * meridional rule also checks the production three-point leaf quadrature.
 */
double axisymmetric_direct_potential(const elliptic::CompositePoisson& op,
    std::span<const double> density,const std::array<double,3>& point,int azimuths,bool fifth) {
    constexpr double x3[]{-.7745966692414834,0.,.7745966692414834};
    constexpr double w3[]{5./9.,8./9.,5./9.};
    constexpr double x5[]{-.9061798459386640,-.5384693101056831,0.,.5384693101056831,.9061798459386640};
    constexpr double w5[]{.2369268850561891,.4786286704993665,.5688888888888889,.4786286704993665,.2369268850561891};
    const int count=fifth?5:3;const double* x=fifth?x5:x3;const double* w=fifth?w5:w3;
    std::vector<double> cosine(azimuths),sine(azimuths);
    for(int p=0;p<azimuths;++p) {const double angle=2.*pi*(p+.5)/azimuths;cosine[p]=std::cos(angle);sine[p]=std::sin(angle);}
    long double potential=0.;
    for(int c=0;c<op.size();++c) {
        const auto center=op.center(c);long double integral=0.,normal=0.;
        for(int i=0;i<count;++i)for(int j=0;j<count;++j) {
            const double r=center[0]+.5*op.width(c,0)*x[i],z=center[1]+.5*op.width(c,1)*x[j];
            // Preserve the binary64 inputs, but form the independent reference
            // product in long double before accumulating the quadrature.
            const long double weight=static_cast<long double>(w[i])*w[j]*r;long double angle_sum=0.;
            for(int p=0;p<azimuths;++p) {
                const double dx=point[0]-r*cosine[p],dy=point[1]-r*sine[p],dz=point[2]-z;
                angle_sum+=1./std::sqrt(dx*dx+dy*dy+dz*dz);
            }
            integral+=weight*angle_sum/azimuths;normal+=weight;
        }
        potential-=2.L*pi*density[c]*op.volumes()[c]*integral/normal;
    }
    return static_cast<double>(potential);
}

void axisymmetric_isolated_boundary() {
    using namespace Physical::Gravity;
    test_complete_elliptic_oracles();
    for(double inner:{0.,.5})for(bool refined:{false,true}) {
        auto base=base_mesh(2,8);base.geometry=elliptic::Geometry::Cylindrical;
        base.origin={inner,-.5,0.};base.spacing={.125,.125,1.};
        elliptic::CompositePoisson op(base,make_cells(base,refined),elliptic::BoundaryKind::CurvilinearIsolated);
        require(op.boundary().sides[2]==elliptic::FaceBoundaryKind::Dirichlet&&
            op.boundary().sides[3]==elliptic::FaceBoundaryKind::Dirichlet,"axisymmetric isolated z became periodic");
        std::vector<double> rho(op.size());long double full_mass=0.;
        for(int c=0;c<op.size();++c) {
            const auto center=op.center(c);rho[c]=1.+.2*center[0]+.1*center[1];
            full_mass+=2.L*pi*rho[c]*op.volumes()[c];
        }
        GravityBoundary tree(op);tree.update(rho);
        require(std::abs(tree.moments().front().value[0]/full_mass-1.L)<1e-14L,"axisymmetric tree lost full ring mass");
        const auto values=tree.values(op,1.),tight=tree.values(op,1.,.125),direct=tree.values(op,1.,0.);
        long double ordinary_error=0.,tight_error=0.,quadrature_error=0.,kernel_error=0.,scale=0.;int count=0;
        for(std::size_t f=0;f<op.faces().size();++f)if(op.faces()[f].boundary_side>=0) {
            // Sample one face per side strip; the full production surface is
            // still evaluated above, including every mixed-level fragment.
            if((count++%4)!=0)continue;
            const auto native=op.faces()[f].center;const std::array<double,3> point{native[0],0.,native[1]};
            const double reference3=axisymmetric_direct_potential(op,rho,point,4096,false);
            const double reference5=axisymmetric_direct_potential(op,rho,point,4096,true);
            ordinary_error+=(values[f]-reference5)*(values[f]-reference5);
            tight_error+=(tight[f]-reference5)*(tight[f]-reference5);
            quadrature_error+=(direct[f]-reference5)*(direct[f]-reference5);
            kernel_error+=(direct[f]-reference3)*(direct[f]-reference3);
            scale+=reference5*reference5;
        }
        const double ordinary=std::sqrt(ordinary_error/scale),tighter=std::sqrt(tight_error/scale),
            spatial=std::sqrt(quadrature_error/scale),kernel=std::sqrt(kernel_error/scale);
        require(ordinary<.03&&tighter<.03,"axisymmetric isolated boundary physical quadrature budget");
        require(kernel<2e-10,"axisymmetric ring reduction differs from full-phi Newton oracle");
        const std::array<double,3> far{0.,0.,32.};
        const double far_value=isolated_potential(tree.nodes().data(),tree.moments().data(),tree.nodes().size(),far.data(),1.,.25,2,GridMetrics::Geometry::Cylindrical,2);
        const double far_reference=axisymmetric_direct_potential(op,rho,far,256,true);
        require(std::abs(far_value/far_reference-1.)<1e-5,"axisymmetric far Newton multipole mismatch");
        std::cout<<"AXISYMMETRIC_BOUNDARY inner="<<inner<<" mixed="<<refined<<" cells="<<op.size()
            <<" faces="<<count<<" full_mass="<<static_cast<double>(full_mass)<<" relative="<<ordinary
            <<" tighter="<<tighter<<" spatial_rule_difference="<<spatial<<" ring_kernel_error="<<kernel
            <<" far_error="<<std::abs(far_value/far_reference-1.)<<'\n';
    }
    std::cout<<"AXISYMMETRIC_ISOLATED_BOUNDARY_PASS\n";
}

/** Smooth spherical Gaussian represented on a meridional cylindrical mesh.
 * Its independent infinite-space Newton potential is -M*erf(d/sigma)/d,
 * M=pi^(3/2)*sigma^3. The box truncates beyond four Gaussian scales; its
 * exponentially small omitted mass is reported as a reference limitation.
 */
void axisymmetric_isolated_gaussian() {
    constexpr double sigma=.25;const double mass=std::pow(pi,1.5)*sigma*sigma*sigma;
    for(bool refined:{false,true}) {
        double previous_phi=0.,previous_force=0.;
        for(int n:{16,32,64}) {
            auto base=base_mesh(2,n);base.geometry=elliptic::Geometry::Cylindrical;
            base.origin={0.,-1.,0.};base.spacing={1./n,2./n,1.};
            const auto begin=std::chrono::steady_clock::now();
            multigrid::CompositeMultigrid solver(base,make_cells(base,refined),elliptic::BoundaryKind::CurvilinearIsolated);
            const auto& op=solver.op();std::vector<double> rho(op.size()),rhs(op.size()),error(op.size()),exact(op.size());
            const auto potential=[=](const std::array<double,3>& x) {
                const double d=std::hypot(x[0],x[1]);return d==0.?-2.*pi*sigma*sigma:-mass*std::erf(d/sigma)/d;
            };
            const auto gradient=[=](const std::array<double,3>& x) {
                const double d=std::hypot(x[0],x[1]),q=d/sigma;
                const double factor=d==0.?0.:mass*(std::erf(q)-2.*q*std::exp(-q*q)/std::sqrt(pi))/(d*d*d);
                return std::array<double,3>{factor*x[0],factor*x[1],0.};
            };
            long double physical_mass=0.;
            for(int c=0;c<op.size();++c) {const auto x=op.center(c);rho[c]=std::exp(-(x[0]*x[0]+x[1]*x[1])/(sigma*sigma));
                rhs[c]=-4.*pi*rho[c];exact[c]=potential(x);physical_mass+=2.L*pi*rho[c]*op.volumes()[c];}
            Physical::Gravity::GravityBoundary tree(op);tree.update(rho);const auto values=tree.values(op,1.);
            const auto boundary_done=std::chrono::steady_clock::now();
            const auto result=solver.solve(op.effective_rhs(rhs,values),{1e-12,0.,300});
            require(result.report.status==multigrid::SolveStatus::Converged&&result.report.residual<=result.report.target,"axisymmetric Gaussian solve failed");
            for(int c=0;c<op.size();++c)error[c]=result.potential[c]-exact[c];
            const double phi=op.norm(error)/op.norm(exact);double force=0.;continuum_face_error(op,result.potential,values,gradient,force);
            long double surface=0.;
            for(std::size_t f=0;f<op.faces().size();++f)if(op.faces()[f].boundary_side>=0) {
                const auto& face=op.faces()[f];surface+=(face.boundary_side%2?1.L:-1.L)*face.area*op.face_gradient(result.potential,face,values[f]);}
            const double gauss=static_cast<double>(std::abs(2.L*pi*surface-4.L*pi*physical_mass)/(4.L*pi*physical_mass));
            require(gauss<5e-10,"axisymmetric Gaussian Gauss balance failed");
            double phi_order=0.,force_order=0.;
            if(previous_phi>0.) {phi_order=std::log2(previous_phi/phi);force_order=std::log2(previous_force/force);
                require(phi_order>=1.8&&force_order>=1.8,"axisymmetric isolated Gaussian second order budget");}
            const auto end=std::chrono::steady_clock::now();
            std::cout<<"AXISYMMETRIC_GAUSSIAN mixed="<<refined<<" n="<<n<<" cells="<<op.size()<<" cycles="<<result.report.cycles
                <<" phi_relative="<<phi<<" force_error="<<force<<" phi_order="<<phi_order<<" force_order="<<force_order
                <<" mass_relative="<<static_cast<double>(physical_mass/mass-1.L)<<" gauss_error="<<gauss
                <<" setup_boundary_seconds="<<std::chrono::duration<double>(boundary_done-begin).count()
                <<" poisson_seconds="<<std::chrono::duration<double>(end-boundary_done).count()<<'\n'<<std::flush;
            previous_phi=phi;previous_force=force;
        }
    }
    std::cout<<"AXISYMMETRIC_ISOLATED_GAUSSIAN_PASS\n";
}

/** A tiny annular physical surface must not disappear under an area threshold. */
void tiny_physical_boundary() {
    auto base=base_mesh(1,16);
    base.geometry=elliptic::Geometry::Cylindrical;
    base.origin[0]=1e-18;
    elliptic::CompositePoisson op(base,make_cells(base,false),elliptic::BoundaryKind::RadialIsolated);
    const auto face=std::find_if(op.faces().begin(),op.faces().end(),[](const auto& f){return f.boundary_side==0;});
    require(face!=op.faces().end() && face->area==base.origin[0],
        "tiny positive annular face was mistaken for the origin join");
}

}
void boundary_error_ledger_contract() {
    using namespace elliptic;
    for(bool refined:{false,true}) {
        auto base=base_mesh(2,4);
        CompositePoisson op(base,make_cells(base,refined),BoundaryKind::Dirichlet);
        std::vector<BoundaryPotentialError> errors(op.faces().size(),
            {0.,BoundaryErrorQuality::CertifiedAbsolute});
        for(std::size_t f=0;f<errors.size();++f)
            errors[f].absolute_error=std::ldexp(1.,-22-static_cast<int>(f%5));
        const auto bound=op.propagate_boundary_error(errors);
        require(bound.status==BoundaryErrorStatus::Bounded,"certified face propagation rejected");
        double maximum_ratio=0.;
        std::vector<double> source(op.size(),0.),values(errors.size());
        for(int pattern=0;pattern<16;++pattern) {
            for(std::size_t f=0;f<values.size();++f)
                values[f]=((f+pattern)%3==0?-1.:1.)*errors[f].absolute_error;
            const auto perturbation=op.effective_rhs(source,values);
            for(int cell=0;cell<op.size();++cell) {
                require(std::abs(perturbation[cell])<=bound.cell_bounds[cell],
                    "face error mapped outside the positive native B bound");
                if(bound.cell_bounds[cell]>0.)
                    maximum_ratio=std::max(maximum_ratio,std::abs(perturbation[cell])/bound.cell_bounds[cell]);
            }
            require(op.norm(perturbation)<=bound.norm_upper,"weighted RHS norm exceeded upper bound");
        }
        auto invalid=errors;
        int used=-1;
        for(std::size_t f=0;f<op.faces().size();++f)
            if(op.faces()[f].area>0. && op.faces()[f].boundary_coefficient!=0.) {used=f;break;}
        require(used>=0,"ledger test has no physical boundary");
        invalid[used].quality=BoundaryErrorQuality::Estimate;
        require(op.propagate_boundary_error(invalid).status==BoundaryErrorStatus::UncertifiedInput,
            "quadrature estimate accepted as a certified boundary bound");
        invalid[used].quality=BoundaryErrorQuality::CertifiedAbsolute;
        invalid[used].absolute_error=-1.;
        require(op.propagate_boundary_error(invalid).status==BoundaryErrorStatus::InvalidInput,
            "negative potential bound accepted");
        invalid[used].absolute_error=std::numeric_limits<double>::max();
        require(op.propagate_boundary_error(invalid).status==BoundaryErrorStatus::Overflow,
            "overflow boundary budget accepted");
        for(auto& e:errors)e.absolute_error=0.;
        const auto zero=op.propagate_boundary_error(errors);
        require(zero.status==BoundaryErrorStatus::Bounded && zero.norm_upper==0.,
            "zero boundary budget gained an implicit floor");
        std::cout<<"BOUNDARY_RHS_LEDGER refined="<<refined
            <<" cells="<<op.size()<<" max_bound_ratio="<<maximum_ratio<<'\n';
    }
    std::cout<<"BOUNDARY_RHS_LEDGER_PASS\n";
}



int main(int argc,char** argv) {
    try {
        if(argc>1 && std::string(argv[1])=="periodic-source-bounds-probe") {periodic_gravity_source_probe();return 0;}
        if(argc>1 && std::string(argv[1])=="projection-ledger-probe") {constant_mode_projection_probe();return 0;}
        if(argc>1 && std::string(argv[1])=="gravity-source-bounds-probe") {isolated_gravity_source_probe();return 0;}
        if(argc>1 && std::string(argv[1])=="boundary-ledger") {boundary_error_ledger_contract();return 0;}
        std::cout<<std::setprecision(17);
        if(argc>1 && std::string(argv[1])=="coarse-diagnostic") {coarse_mesh_diagnostic();return 0;}
        if (argc>1 && std::string(argv[1])=="contract") { contract(); coarse_mesh_diagnostic(); radial_convergence(); axisymmetric_prescribed_poisson();axisymmetric_isolated_boundary();return 0; }
        if(argc>1 && std::string(argv[1])=="radial") {radial_convergence();return 0;}
        if(argc>1 && std::string(argv[1])=="axisymmetric-boundary") {axisymmetric_isolated_boundary();return 0;}
        if(argc>1 && std::string(argv[1])=="axisymmetric-isolated") {axisymmetric_isolated_gaussian();return 0;}
        if(argc>1 && std::string(argv[1])=="axisymmetric-device") {axisymmetric_prescribed_poisson(true);return 0;}
        if(argc>1 && std::string(argv[1])=="axisymmetric") {axisymmetric_prescribed_poisson();return 0;}
        if(argc>1 && std::string(argv[1])=="curved") {curved_manufactured();curved_boundary_integral();return 0;}
        if(argc>1 && std::string(argv[1])=="singular") {curved_manufactured(true);curved_manufactured(true,true);return 0;}
        if(argc>1 && std::string(argv[1])=="gauss") {curved_gauss_law();return 0;}
        if(argc>1 && std::string(argv[1])=="domain") {curved_domain_extension();return 0;}
        if(argc>1 && std::string(argv[1])=="boundary") {boundary_convergence();isolated_boundary();return 0;}
        if(argc>1 && std::string(argv[1])=="policy") {
            tiny_physical_boundary();
            mixed_cartesian_boundary();curved_mixed_boundary();
            pure_neumann_compatibility();extreme_flux_boundary();boundary_policy_rejection();return 0;
        }
        if(argc>1 && std::string(argv[1])=="ci") {
            tiny_physical_boundary();
            // Reuse the existing analytic owner for native point-stencil,
            // certificate and physical-volume-source convergence checks.
            boundary_convergence(16);isolated_boundary();averaged_source_exactness();
            convergence(2);radial_convergence();axisymmetric_prescribed_poisson();axisymmetric_isolated_boundary();axisymmetric_isolated_gaussian();curved_manufactured();
            curved_boundary_integral();curved_domain_extension();curved_gauss_law();curved_manufactured(true);curved_manufactured(true,true);
            mixed_cartesian_boundary();curved_mixed_boundary();
            pure_neumann_compatibility();extreme_flux_boundary();boundary_policy_rejection();return 0;
        }
        averaged_source_exactness();
        convergence(argc>1 ? std::stoi(argv[1]) : 3);
        std::cout<<"Composite Poisson analytic validation passed\n";
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
