#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
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

/** Pure Neumann nullspace, volume-weighted gauge and source compatibility. */
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

/** Check independent enclosed-mass force with a hollow polar ring/spherical shell. */
void curved_gauss_law() {
    constexpr double G=1.,q=.2,rmin=.5;
    for(int dim:{2,3})for(bool refined:{false,true}) {
        double previous=0.;
        for(int n:{8,16}) {
            auto base=base_mesh(dim,n);
            base.geometry=dim==2?elliptic::Geometry::Cylindrical:elliptic::Geometry::Spherical;
            base.origin[0]=rmin;base.spacing[0]=1./n;
            if(dim==2){base.origin[1]=0.;base.spacing[1]=2*pi/n;}
            else {base.origin[1]=0.;base.spacing[1]=pi/n;
                base.origin[2]=0.;base.spacing[2]=2*pi/n;}
            multigrid::CompositeMultigrid solver(base,make_cells(base,refined),
                elliptic::BoundaryKind::CurvilinearIsolated);
            const auto& op=solver.op();
            std::vector<double> rho(op.size()),rhs(op.size()),bc(op.faces().size());
            for(int i=0;i<op.size();++i) {
                const double r=op.center(i)[0],h=op.width(i,0);
                const double lo=r-h/2,hi=r+h/2;
                const double average_r2=dim==2?(hi*hi+lo*lo)/2
                    :3.*(std::pow(hi,5)-std::pow(lo,5))
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
                    const double mass=dim==2
                        ?2*pi*(.5*(r*r-rmin*rmin)+q*.25*(std::pow(r,4)-std::pow(rmin,4)))
                        :4*pi*((std::pow(r,3)-std::pow(rmin,3))/3.
                            +q*(std::pow(r,5)-std::pow(rmin,5))/5.);
                    expected=dim==2?2*G*mass/r:G*mass/(r*r);
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

/** Exercise nonaxisymmetric manufactured potentials on native curved meshes. */
/** Prevent explicit RZ identity from silently reaching the legacy 2D log kernel. */

/** Independent full-azimuth product quadrature; a reference, not a certified near bound. */
long double independent_ring_potential(double lo,double hi,double zlo,double zhi,
    const std::array<double,3>& point,int order) {
    std::vector<long double> nodes(order),weights(order);
    constexpr long double pi_l=3.141592653589793238462643383279502884L;
    for(int i=0;i<order;++i) {
        long double x=std::cos(pi_l*(i+.75L)/(order+.5L));
        for(int it=0;it<30;++it) {
            long double prev=1.,p=x;
            for(int n=2;n<=order;++n) {const long double next=((2*n-1)*x*p-(n-1)*prev)/n;prev=p;p=next;}
            const long double derivative=order*(x*p-prev)/(x*x-1.);
            const long double next=x-p/derivative;
            if(std::abs(next-x)<4*std::numeric_limits<long double>::epsilon()) {x=next;break;}
            x=next;
        }
        long double prev=1.,p=x;
        for(int n=2;n<=order;++n) {const long double next=((2*n-1)*x*p-(n-1)*prev)/n;prev=p;p=next;}
        const long double derivative=order*(x*p-prev)/(x*x-1.);
        nodes[i]=x;weights[i]=2/((1-x*x)*derivative*derivative);
    }
    constexpr int azimuths=256;
    long double value=0.;
    for(int i=0;i<order;++i)for(int j=0;j<order;++j) {
        const long double radius=.5L*(lo+hi)+.5L*(hi-lo)*nodes[i];
        const long double z=.5L*(zlo+zhi)+.5L*(zhi-zlo)*nodes[j];
        long double angular=0.;
        for(int k=0;k<azimuths;++k) {
            const long double angle=2*pi_l*(k+.5L)/azimuths;
            const long double x=point[0]-radius*std::cos(angle);
            const long double y=point[1]-radius*std::sin(angle);
            const long double dz=point[2]-z;
            angular+=1/std::sqrt(x*x+y*y+dz*dz);
        }
        value-=radius*weights[i]*weights[j]*.25L*(hi-lo)*(zhi-zlo)
            *2*pi_l/azimuths*angular;
    }
    return value;
}

void finite_ring_moment_contract() {
    using namespace Physical::Gravity;
    constexpr long double pi_l=3.141592653589793238462643383279502884L;
    const auto close=[](double actual,long double reference,const char* message) {
        require(std::isfinite(actual)
            && std::abs(static_cast<long double>(actual)-reference)
                <=2.e-12L*std::max(1.L,std::abs(reference)),message);
    };
    for(double lo:{0.,.5,3.})for(double dz:{.125,2.}) {
        const double hi=lo+.75;
        const long double mass=pi_l*(static_cast<long double>(hi)*hi-lo*lo)*dz;
        const auto m=finite_ring_unit_moments(lo,hi,dz);
        close(m.value[0],mass,"ring unit mass");
        close(m.value[second_moment_index(0,0)],mass*(hi*hi+lo*lo)/4,"ring Ixx");
        close(m.value[second_moment_index(1,1)],mass*(hi*hi+lo*lo)/4,"ring Iyy");
        close(m.value[second_moment_index(2,2)],mass*dz*dz/12,"ring Izz");
        for(int q:{1,2,3,5,6,8})require(m.value[q]==0.,"ring nonzero odd/cross moment");
        close(finite_ring_support_squared(hi,.5*dz),hi*hi+.25L*dz*dz,"ring full physical support");
        require(finite_ring_support_squared(hi,.5*dz)>.25*(hi-lo)*(hi-lo)+.25*dz*dz,
            "ring support silently used meridional half diagonal");
    }
    auto base=base_mesh(2,4);
    base.geometry=elliptic::Geometry::Cylindrical;
    base.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
    base.origin={.5,-.5,0.};
    for(bool mixed:{false,true}) {
        elliptic::CompositePoisson op(base,make_cells(base,mixed),elliptic::BoundaryKind::CurvilinearIsolated);
        GravityBoundary tree(op);
        std::vector<double> density(op.size());
        long double total=0.,dipole=0.,xx=0.,zz=0.;
        const auto root_z=tree.nodes().front().center[2];
        for(int c=0;c<op.size();++c) {
            const auto native=op.center(c);
            density[c]=1.+.3*native[1];
            const long double lo=native[0]-.5*op.width(c,0),hi=native[0]+.5*op.width(c,0);
            const long double dz=op.width(c,1),offset=native[1]-root_z;
            const long double mass=density[c]*pi_l*(hi*hi-lo*lo)*dz;
            total+=mass;dipole+=mass*offset;xx+=mass*(hi*hi+lo*lo)/4;
            zz+=mass*(dz*dz/12+offset*offset);
        }
        tree.update(density);
        const auto& root=tree.moments().front();
        close(root.value[0],total,"ring parent mass translation");
        close(root.value[3],dipole,"ring parent dipole translation");
        close(root.value[second_moment_index(0,0)],xx,"ring parent radial second moment");
        close(root.value[second_moment_index(2,2)],zz,"ring parent axial second moment");
        require(root.value[3]!=0.,"ring asymmetric parent accidentally declared symmetric");
        for(const auto& node:tree.nodes()) {
            require(node.center[0]==0. && node.center[1]==0.,"ring center off symmetry axis");
            if(node.cell<0)continue;
            const auto x=op.center(node.cell);
            const double expected_radius=x[0]+.5*op.width(node.cell,0);
            close(node.radius_squared,expected_radius*expected_radius
                +.25L*op.width(node.cell,1)*op.width(node.cell,1),"actual ring leaf support");
            close(node.center[2],x[1],"actual ring axial midpoint");
        }
        auto changed=density;for(auto& value:changed)value*=2.;
        tree.update(changed);close(tree.moments().front().value[0],2*total,"ring density generation reuse");
        std::cout<<"RZ_RING_TREE mixed="<<mixed<<" leaves="<<op.size()
            <<" mass="<<root.value[0]<<" dipole_z="<<root.value[3]<<'\n';
    }
    const auto moments=finite_ring_unit_moments(.5,1.,.75);
    const double support=std::sqrt(finite_ring_support_squared(1.,.375));
    for(double q:{.01,.2,.7,.95}) {
        const double distance=support/q;
        const std::array<double,3> point{.6*distance,0.,.8*distance};
        const long double a=independent_ring_potential(.5,1.,-.375,.375,point,24);
        const long double b=independent_ring_potential(.5,1.,-.375,.375,point,32);
        const double r[3]{point[0],point[1],point[2]};
        const double actual=newtonian_multipole_potential(moments,r,distance*distance,1.,2);
        const auto symmetric=multipole_truncation_bound(1.,moments.value[0],support,distance,true);
        const auto general=multipole_truncation_bound(1.,moments.value[0],support,distance,false);
        require(symmetric.status==MultipoleBoundStatus::Bounded
            && general.status==MultipoleBoundStatus::Bounded,"separated ring bound absent");
        require(std::abs(a-b)<1.e-12L*std::abs(b),"independent far ring reference not converged");
        require(std::abs(actual-b)<symmetric.value && symmetric.value<=general.value,
            "ring quadrupole violates frozen leaf truncation envelope");
        // Signed manufactured sources use absolute mass, even with net M=0.
        const auto signed_source=multipole_truncation_bound(1.,2*moments.value[0],support,distance,false);
        close(signed_source.value,2*general.value,"signed absolute-mass remainder");
        std::cout<<"RZ_RING_FAR q="<<q<<" actual_error="<<static_cast<double>(std::abs(actual-b))
            <<" symmetric_bound="<<symmetric.value<<" general_bound="<<general.value
            <<" reference_difference="<<static_cast<double>(std::abs(a-b))<<'\n';
    }
    // Signed manufactured pair: net monopole vanishes, but the remainder does not.
    // It must use integral |rho| dV, not abs(M_net).
    BoundaryTreeNode signed_nodes[3]{};
    signed_nodes[0].children[0]=1;signed_nodes[0].children[1]=2;
    signed_nodes[1].center={0.,0.,.25};signed_nodes[2].center={0.,0.,-.25};
    BoundaryMoments signed_moments[3]{};
    signed_moments[1]=finite_ring_unit_moments(.5,1.,.25);
    signed_moments[2]=signed_moments[1];
    for(auto& value:signed_moments[2].value)value=-value;
    signed_moments[0]=combine_boundary_moments(signed_nodes,signed_moments,0);
    require(signed_moments[0].value[0]==0. && signed_moments[0].value[3]!=0.,
        "signed parent translation lost source cancellation");
    const double signed_point[3]{1.8,0.,2.4};
    const double signed_actual=newtonian_multipole_potential(
        signed_moments[0],signed_point,9.,1.,2);
    const std::array<double,3> reference_point{1.8,0.,2.4};
    const long double signed_reference=independent_ring_potential(.5,1.,.125,.375,reference_point,32)
        -independent_ring_potential(.5,1.,-.375,-.125,reference_point,32);
    const double absolute_mass=2*signed_moments[1].value[0];
    const auto signed_bound=multipole_truncation_bound(1.,absolute_mass,support,3.,false);
    const double signed_error=static_cast<double>(std::abs(signed_actual-signed_reference));
    require(signed_bound.status==MultipoleBoundStatus::Bounded
        && signed_error>1.e-12 && signed_error<signed_bound.value,
        "signed zero-net-mass parent lost its absolute-mass error envelope");
    std::cout<<"RZ_RING_SIGNED net_mass="<<signed_moments[0].value[0]
        <<" absolute_mass="<<absolute_mass<<" actual_error="<<signed_error
        <<" general_bound="<<signed_bound.value<<'\n';
    require(multipole_truncation_bound(std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(),.5,1.,false).status==MultipoleBoundStatus::Overflow,
        "overflowed multipole bound became accepted");
    require(multipole_truncation_bound(1.,1.,1.,1.,false).status==MultipoleBoundStatus::NotSeparated,
        "ring contact accepted far bound");
    require(multipole_truncation_bound(1.,-1.,1.,2.,false).status==MultipoleBoundStatus::InvalidInput,
        "negative absolute mass accepted");
    require(multipole_truncation_bound(1.,0.,1.,2.,false).value==0.,
        "zero source remainder changed");
    std::cout<<"RZ_RING_MOMENT_REMAINDER_PASS production_values=gated near_bound=pending\n";
}


/** Actual tree->native face->RHS error consumer, kept behind production gate. */
/** Validate exact quotient-key arithmetic and the actual native tree memo.
 * Metadata examples have independent exact dyadic expected values; production
 * TwoSum is not duplicated as a test oracle. Actual cache reuse is compared
 * with original uncached finite-leaf enclosures at EVERY exterior observer.
 * This tests scalar-potential integral equivalence, not continuous force or
 * physical Runtime qualification. Existing field/source/work gates remain.
 */
void finite_ring_axial_memo_key_contract() {
    using namespace Physical::Gravity;
    using ring_memo_detail::exact_axial_relative_endpoints;
    const int original_rounding=std::fegetround();
    require(original_rounding!=-1&&std::fesetround(FE_TONEAREST)==0,
        "memo-key fixture cannot establish strict nearest rounding");
    struct RoundingRestore {
        int mode;
        /** Restore caller arithmetic even when a test assertion throws. */
        ~RoundingRestore(){std::fesetround(mode);}
    } restore{original_rounding};
    const auto original=exact_axial_relative_endpoints(1.,2.,0.);
    const auto translated=exact_axial_relative_endpoints(3.,4.,2.);
    const auto reflected=exact_axial_relative_endpoints(-4.,-3.,-2.);
    require(original&&translated&&reflected&&*original==std::array<double,4>{1.,0.,2.,0.}
        &&*translated==*original&&*reflected==*original,
        "exact translated/reflected potential supports did not share one metadata value");
    const double displacement=0x1p-55;
    const auto different=exact_axial_relative_endpoints(1.,2.,displacement);
    require(1.-displacement==1.&&2.-displacement==2.&&different
        &&*different==std::array<double,4>{1.,-displacement,2.,-displacement}
        &&*different!=*original,
        "rounded endpoint collision omitted the represented exact subtraction residual");
    const auto negative_zero=exact_axial_relative_endpoints(-1.,1.,-0.);
    const auto positive_zero=exact_axial_relative_endpoints(-1.,1.,0.);
    require(negative_zero&&positive_zero&&*negative_zero==*positive_zero,
        "axial signed-zero observer changed the exact quotient key");
    for(double value:*negative_zero)if(value==0.)
        require(std::bit_cast<std::uint64_t>(value)==0,
            "axial key retained a negative zero high/low word");
    for(const auto unsupported:{std::array<double,3>{0.,0x1p401,0.},
                               std::array<double,3>{0x1p-401,1.,0.},
                               std::array<double,3>{0.,1.,0x1p-401}})
        require(!exact_axial_relative_endpoints(unsupported[0],unsupported[1],unsupported[2]),
            "unsupported axial exponents acquired an unproved normalized key");
    require(!exact_axial_relative_endpoints(2.,1.,0.)
        &&!exact_axial_relative_endpoints(0.,1.,std::numeric_limits<double>::quiet_NaN())
        &&!exact_axial_relative_endpoints(0.,std::numeric_limits<double>::infinity(),0.),
        "invalid axial support acquired an exact quotient key");
    for(int mode:{FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}) {
        require(std::fesetround(mode)==0,"memo-key fixture cannot set rounding mode");
        require(!exact_axial_relative_endpoints(1.,2.,0.),
            "non-nearest arithmetic acquired a TwoSum quotient certificate");
    }
    require(std::fesetround(FE_TONEAREST)==0,"memo-key fixture lost nearest rounding");

    // The unchanged active-grid validator accepts powers of two >=2, but
    // actual Native boundary point interpolation needs transverse support.
    // Reuse the existing canonical 4x4 owner: four distinct radial rho columns
    // prevent uniform quartets; identical axial columns retain exact reuse.
    auto base=base_mesh(2,4);base.spacing={.5,1.,1.};
    base.origin={.5,-2.,0.};base.geometry=elliptic::Geometry::Cylindrical;
    base.native_canonical_domain=true;base.root_upper={2.5,2.,0.};
    base.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
    const elliptic::CompositePoisson op(base,make_cells(base,false),
        elliptic::BoundaryKind::CurvilinearIsolated);
    GravityBoundary tree(op,{43});
    GravitySolveIdentity source;source.topology={43};
    source.gravitational_constant=constants::gravity::cgs::gravitational_constant;
    source.operator_revision=source.boundary_revision=source.accuracy_revision=1;
    source.inputs.push_back({{{1},{43}},state::StateSlot::Current,{1},1});
    std::vector<double> density(op.size());
    for(int c=0;c<op.size();++c)density[c]=1.+op.cells()[c].index[0];
    tree.update(density,source);
    RingBoundaryControl control;control.face_absolute_target=1.e-5;
    control.maximum_boxes_per_leaf=8;
    const auto cold=tree.ring_boundary(op,source,control);
    require(cold.status==RingBoundaryStatus::Bounded&&cold.memo_hits>0
        &&cold.memo_misses>0&&cold.memo_admissions>0&&cold.kernel_enclosures>0
        &&cold.coalesced_parent_attempts==0,
        "actual translated/reflected native supports never reused an original bounded integral");
    tree.require_current_ring(op,cold);
    RingEnclosureControl leaf;
    leaf.absolute_target=finite_ring_detail::positive_down(control.face_absolute_target/op.size());
    leaf.maximum_boxes=control.maximum_boxes_per_leaf;
    std::uint64_t uncached_kernels=0;std::size_t exterior=0;
    for(std::size_t f=0;f<op.faces().size();++f) {
        const auto& face=op.faces()[f];if(face.boundary_side<0)continue;
        ++exterior;finite_ring_detail::SignedInterval uncached{};
        for(int c=0;c<op.size();++c) {
            const auto value=finite_ring_potential_enclosure(op.lower(c,0),op.upper(c,0),
                op.lower(c,1),op.upper(c,1),density[c],face.center[0],face.center[1],
                source.gravitational_constant,leaf);
            require(value.bound_valid&&value.status==RingIntervalStatus::Bounded,
                "original uncached leaf failed the same explicit precision/box request");
            uncached=finite_ring_detail::interval_sum(uncached,{value.lower,value.upper});
            uncached_kernels+=value.kernel_enclosures;
        }
        require(finite_ring_detail::interval_finite(uncached)
            &&cold.lower[f]<=uncached.upper&&uncached.lower<=cold.upper[f]
            &&cold.errors[f].quality==elliptic::BoundaryErrorQuality::CertifiedAbsolute
            &&cold.errors[f].absolute_error<=control.face_absolute_target,
            "actual quotient-cache face departed from the original uncached source enclosure/budget");
    }
    require(exterior>0&&cold.kernel_enclosures<uncached_kernels
        &&cold.represented_leaf_evaluations==exterior*op.size(),
        "normalized history omitted source coverage or saved no actual kernel work");
    const auto warm=tree.ring_boundary(op,source,control);
    require(warm.status==RingBoundaryStatus::Bounded&&warm.memo_hits>0
        &&warm.kernel_enclosures==0&&warm.range_evaluations==0&&warm.agm_iterations==0,
        "same actual source did not reuse already admitted certified intervals");
    tree.require_current_ring(op,warm);
    // Clearing only the memo preserves real source identity/generation. New
    // cold traversal still benefits from WITHIN-call translation/reflection.
    tree.clear_ring_memo();require(tree.ring_memo_size()==0,
        "normalized integral history survived its original explicit clear");
    const auto recold=tree.ring_boundary(op,source,control);
    require(recold.status==RingBoundaryStatus::Bounded&&recold.memo_misses>0
        &&recold.memo_hits>0&&recold.source_generation==cold.source_generation,
        "memo clear altered source generation or suppressed new exact admissions");
    const auto occupancy=tree.ring_memo_size();
    require(std::fesetround(FE_DOWNWARD)==0,"cannot establish non-nearest cache fallback");
    const auto directed=tree.ring_boundary(op,source,control);
    require(directed.memo_hits==0&&directed.memo_admissions==0
        &&tree.ring_memo_size()==occupancy,
        "non-nearest traversal read or admitted mathematical history");
    require(std::fesetround(FE_TONEAREST)==0,"cannot restore nearest after cache fallback");

    // A real unsafe-exponent source is allowed to use the ORIGINAL raw key;
    // normalization must decline it without altering any kernel coordinate.
    auto raw_base=base;raw_base.origin[1]=0x1p-500;
    raw_base.root_upper={2.5,2.,0.};raw_base.spacing={.5,.5,1.};
    const elliptic::CompositePoisson raw_op(raw_base,make_cells(raw_base,false),
        elliptic::BoundaryKind::CurvilinearIsolated);
    require(!exact_axial_relative_endpoints(raw_op.lower(0,1),raw_op.upper(0,1),.5),
        "actual unsafe-exponent source unexpectedly entered quotient mode");
    GravityBoundary raw_tree(raw_op,{43});
    std::vector<double> raw_density(raw_op.size());
    for(int c=0;c<raw_op.size();++c)raw_density[c]=1.+raw_op.cells()[c].index[0];
    raw_tree.update(raw_density,source);
    const auto raw_cold=raw_tree.ring_boundary(raw_op,source,control);
    const auto raw_warm=raw_tree.ring_boundary(raw_op,source,control);
    require(raw_cold.status==RingBoundaryStatus::Bounded&&raw_cold.memo_admissions>0
        &&raw_warm.status==RingBoundaryStatus::Bounded&&raw_warm.memo_hits>0,
        "unsupported quotient exponent changed the exact original raw-key fallback");
    std::cout<<"RZ_RING_AXIAL_MEMO_KEY_PASS cold_hits="<<cold.memo_hits
        <<" cold_misses="<<cold.memo_misses<<" kernels="<<cold.kernel_enclosures
        <<" uncached_kernels="<<uncached_kernels<<" raw_mode_hits="<<raw_warm.memo_hits
        <<" integral_only=1 production_values=gated\n";
}

void finite_ring_tree_boundary_contract() {
    finite_ring_axial_memo_key_contract();
    using namespace Physical::Gravity;
    auto rejects=[](auto function,const char* message) {
        bool rejected=false;try{function();}catch(const std::exception&){rejected=true;}
        require(rejected,message);
    };
    for(bool mixed:{false,true})for(double radial_origin:{0.,.5}) {
        auto base=base_mesh(2,4);
        base.geometry=elliptic::Geometry::Cylindrical;
        base.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
        base.origin={radial_origin,-.5,0.};
        elliptic::CompositePoisson op(base,make_cells(base,mixed),elliptic::BoundaryKind::CurvilinearIsolated);
        GravityBoundary tree(op,{7});
        GravitySolveIdentity source;source.topology={7};
        source.gravitational_constant=constants::gravity::cgs::gravitational_constant;
        source.operator_revision=source.boundary_revision=source.accuracy_revision=1;
        source.inputs.push_back({{{1},{7}},state::StateSlot::Current,{1},1});
        std::vector<double> density(op.size());
        for(int cell=0;cell<op.size();++cell)density[cell]=1.+.3*op.center(cell)[1];
        RingBoundaryControl control{};control.face_absolute_target=1.e-5;control.maximum_boxes_per_leaf=8;
        rejects([&]{tree.ring_boundary(op,source,control);},"unprepared ring source consumed");
        tree.update(density,source);
        const auto result=tree.ring_boundary(op,source,control);
        require(result.status==RingBoundaryStatus::Bounded,"bounded native-face fixture failed");
        tree.require_current_ring(op,result);
        require(result.coalesced_parent_attempts==0,"nonuniform native source was averaged for coalescence");
        const auto ledger=op.propagate_boundary_error(result.errors);
        require(ledger.status==elliptic::BoundaryErrorStatus::Bounded&&ledger.norm_upper>0.,
            "native-face interval did not enter canonical RHS ledger");
        std::size_t exterior=0,newton_references=0;
        long double reference8=0.,reference12=0.;
        for(std::size_t face=0;face<op.faces().size();++face) {
            const auto& f=op.faces()[face];
            if(f.boundary_side<0) {
                require(result.values[face]==0.&&result.errors[face].absolute_error==0.,
                    "interior face acquired a scientific boundary value");continue;
            }
            ++exterior;
            require(result.errors[face].quality==elliptic::BoundaryErrorQuality::CertifiedAbsolute
                &&result.errors[face].absolute_error<=control.face_absolute_target,
                "native face failed complete source budget");
            if(newton_references)continue;
            // Independent full-azimuth 3D Newton integral, two orders.
            // This contact diagnostic is not a certified quadrature error.
            const std::array<double,3> point{f.center[0],0.,f.center[1]};
            for(int cell=0;cell<op.size();++cell) {
                const auto x=op.center(cell);
                const double rl=x[0]-.5*op.width(cell,0),rh=x[0]+.5*op.width(cell,0);
                const double lo=x[1]-.5*op.width(cell,1),hi=x[1]+.5*op.width(cell,1);
                const long double factor=static_cast<long double>(source.gravitational_constant)*density[cell];
                reference8+=factor*independent_ring_potential(rl,rh,lo,hi,point,8);
                reference12+=factor*independent_ring_potential(rl,rh,lo,hi,point,12);
            }
            require(result.lower[face]<=reference8&&reference8<=result.upper[face]
                &&result.lower[face]<=reference12&&reference12<=result.upper[face],
                "actual native face source diagnostics escaped interval");
            ++newton_references;
        }
        require(result.represented_leaf_evaluations==exterior*op.size(),
            "tree traversal omitted or double counted a finite source");
        const double far_tail=*std::max_element(result.far_truncation_upper.begin(),result.far_truncation_upper.end());
        const double far_arithmetic=*std::max_element(result.far_evaluation_width_upper.begin(),result.far_evaluation_width_upper.end());
        require(result.parent_acceptances>0&&std::isfinite(far_tail)&&far_tail>0.
            &&std::isfinite(far_arithmetic)&&far_arithmetic>0.,
            "actual parent acceptance or separate tail/evaluation diagnostics missing");
        auto changed=source;changed.input_time=.25;
        rejects([&]{tree.ring_boundary(op,changed,control);},"changed source time silently reused");
        changed=source;changed.inputs[0].version={2};
        rejects([&]{tree.ring_boundary(op,changed,control);},"changed density version silently reused");
        changed=source;changed.inputs[0].storage_generation=2;
        rejects([&]{tree.ring_boundary(op,changed,control);},"changed density allocation silently reused");
        auto moved=base;moved.origin[1]+=.25;
        elliptic::CompositePoisson other(moved,make_cells(moved,mixed),elliptic::BoundaryKind::CurvilinearIsolated);
        rejects([&]{tree.ring_boundary(other,source,control);},"same-count shifted mesh accepted");
        auto reordered=op.cells();std::reverse(reordered.begin(),reordered.end());
        elliptic::CompositePoisson order_changed(base,reordered,elliptic::BoundaryKind::CurvilinearIsolated);
        rejects([&]{tree.ring_boundary(order_changed,source,control);},"changed cell order accepted");
        rejects([&]{tree.values(op,source.gravitational_constant);},"production RZ gate removed");
        auto limited=control;limited.maximum_leaf_evaluations=1;
        const auto early=tree.ring_boundary(op,source,limited);
        require(early.status==RingBoundaryStatus::WorkLimit&&early.leaf_evaluations+early.parent_evaluations==1,
            "global source work budget ignored");
        rejects([&]{tree.require_current_ring(op,early);},"partial work result published");
        limited=control;limited.face_absolute_target=0.;limited.maximum_boxes_per_leaf=1;
        const auto failed=tree.ring_boundary(op,source,limited);
        require(failed.status!=RingBoundaryStatus::Bounded,
            "nonzero source met hidden zero-target floor");
        rejects([&]{tree.require_current_ring(op,failed);},"failed ring source result published");
        require(op.propagate_boundary_error(failed.errors).status
            ==elliptic::BoundaryErrorStatus::UncertifiedInput,
            "failed partial source field entered certified RHS ledger");
        auto doubled=density;for(auto& x:doubled)x*=2.;
        const auto moment_storage=tree.moments().data();
        tree.update(doubled,source);
        require(tree.moments().data()==moment_storage,"moment update invalidated stable upload views");
        rejects([&]{tree.require_current_ring(op,result);},"old source generation consumed");
        const auto next=tree.ring_boundary(op,source,control);
        require(next.source_generation==result.source_generation+1
            &&next.status==RingBoundaryStatus::Bounded,"density update did not retire old generation");
        auto bad=doubled;bad.back()=-1.;
        const auto mass=tree.moments().front().value[0];
        rejects([&]{tree.update(bad,source);},"negative source update accepted");
        require(tree.moments().front().value[0]==mass,"rejected source partly published moments");
        rejects([&]{tree.require_current_ring(op,next);},"failed update retained current source stamp");
        changed=source;changed.topology={8};changed.inputs[0].block.epoch={8};
        rejects([&]{tree.update(density,changed);},"new AMR epoch reused old tree");
        tree.update(std::vector<double>(op.size(),0.),source);
        const auto zero=tree.ring_boundary(op,source,limited);
        require(zero.status==RingBoundaryStatus::Bounded,
            "exact zero source acquired arithmetic or quadrature uncertainty");
        for(double value:zero.values)require(value==0.,"zero source became nonzero field");
        std::cout<<"RZ_RING_NATIVE_FACE mixed="<<mixed<<" radial_origin="<<radial_origin
            <<" leaves="<<op.size()<<" exterior_faces="<<exterior
            <<" source_evaluations="<<result.leaf_evaluations
            <<" parent_evaluations="<<result.parent_evaluations
            <<" coalesced_attempts="<<result.coalesced_parent_attempts
            <<" coalesced_acceptances="<<result.coalesced_parent_acceptances
            <<" coalesced_native_leaves="<<result.coalesced_native_leaves
            <<" parent_acceptances="<<result.parent_acceptances
            <<" represented_leaves="<<result.represented_leaf_evaluations
            <<" far_tail_max="<<far_tail<<" far_evaluation_width_max="<<far_arithmetic
            <<" newton_references="<<newton_references
            <<" newton8="<<static_cast<double>(reference8)<<" newton12="<<static_cast<double>(reference12)
            <<" quadrature_difference="<<static_cast<double>(std::abs(reference8-reference12))
            <<" rhs_error="<<ledger.norm_upper<<'\n';
    }
    std::cout<<"RZ_RING_NATIVE_FACE_PASS production_values=gated far_parent=interval_budget_checked\n";
}

/** Exact density and native/root geometry qualification; no average fallback. */
void finite_ring_uniform_quartet_contract() {
    using namespace Physical::Gravity;
    for(double origin:{0.,.3})for(bool uniform:{false,true}) {
        auto base=base_mesh(2,4);base.geometry=elliptic::Geometry::Cylindrical;
        base.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;base.origin={origin,-.5,0.};
        elliptic::CompositePoisson op(base,make_cells(base,false),elliptic::BoundaryKind::CurvilinearIsolated);
        GravityBoundary tree(op,{7});GravitySolveIdentity source;source.topology={7};
        source.gravitational_constant=constants::gravity::cgs::gravitational_constant;
        source.operator_revision=source.boundary_revision=source.accuracy_revision=1;
        source.inputs.push_back({{{1},{7}},state::StateSlot::Current,{1},1});
        std::vector<double> density(op.size());
        for(int i=0;i<op.size();++i)density[i]=uniform?1.:1.+.01*i;
        tree.update(density,source);
        RingBoundaryControl controls{};controls.face_absolute_target=1.e-9;
        controls.maximum_boxes_per_leaf=65536;
        const auto result=tree.ring_boundary(op,source,controls);
        require(result.status==RingBoundaryStatus::Bounded,"uniform quartet qualification boundary failed");
        if(origin==0.&&uniform)
            require(result.coalesced_parent_acceptances>0,"qualified exact quartet was never integrated");
        else require(result.coalesced_parent_attempts==0,"nonuniform or unproved geometry coalesced");
        std::size_t exterior=0;
        for(const auto& face:op.faces())exterior+=face.boundary_side>=0;
        require(result.represented_leaf_evaluations==exterior*op.size(),"quartet omitted or doubled original source");
        require(result.coalesced_native_leaves==4*result.coalesced_parent_acceptances,
            "quartet represented count does not match original four leaves");
        require(result.leaf_evaluations+result.parent_evaluations<=controls.maximum_leaf_evaluations,
            "quartet bypassed original work cap");
        tree.require_current_ring(op,result);
        std::cout<<"RZ_UNIFORM_QUARTET_QUALIFICATION_PASS origin="<<origin<<" uniform="<<uniform
            <<" attempts="<<result.coalesced_parent_attempts<<" accepted="<<result.coalesced_parent_acceptances
            <<" represented="<<result.represented_leaf_evaluations<<'\n';
    }
}

/** Actual source tree companion, independently audited by Decimal integrals. */
void finite_ring_parent_probe() {
    using namespace Physical::Gravity;
    std::cout<<std::setprecision(17)<<"{\"cases\":[";bool first_case=true;
    for(bool mixed:{false,true})for(double origin:{0.,.5}) {
        auto base=base_mesh(2,4);
        base.geometry=elliptic::Geometry::Cylindrical;
        base.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
        base.origin={origin,-.23,0.};
        elliptic::CompositePoisson op(base,make_cells(base,mixed),elliptic::BoundaryKind::CurvilinearIsolated);
        GravityBoundary tree(op,{7});
        GravitySolveIdentity source;source.topology={7};source.operator_revision=source.boundary_revision=source.accuracy_revision=1;
        source.gravitational_constant=constants::gravity::cgs::gravitational_constant;
        source.inputs.push_back({{{1},{7}},state::StateSlot::Current,{1},1});
        std::vector<double> density(op.size());
        for(int cell=0;cell<op.size();++cell)density[cell]=1.+.3*op.center(cell)[1]+.7*op.center(cell)[0];
        tree.update(density,source);
        const auto bounds=tree.ring_moment_enclosures(op,source);
        const auto& nodes=tree.nodes();
        if(!first_case)std::cout<<',';first_case=false;
        std::cout<<"{\"mixed\":"<<mixed<<",\"origin\":"<<origin<<",\"nodes\":[";
        for(std::size_t i=0;i<nodes.size();++i) {
            require(bounds[i].valid,"actual source moment companion invalid");
            if(i)std::cout<<',';
            std::cout<<"{\"center\":["<<nodes[i].center[0]<<','<<nodes[i].center[1]<<','<<nodes[i].center[2]
                <<"],\"end\":"<<nodes[i].end<<",\"cell\":"<<nodes[i].cell
                <<",\"support_upper\":"<<bounds[i].support_upper<<",\"leaves\":"<<bounds[i].leaves<<",\"moments\":[";
            for(int q=0;q<10;++q){if(q)std::cout<<',';std::cout<<'['<<bounds[i].value[q].lower<<','<<bounds[i].value[q].upper<<']';}
            std::cout<<']';
            if(nodes[i].cell>=0) {
                const auto center=op.center(nodes[i].cell);
                const double wr=op.width(nodes[i].cell,0),wz=op.width(nodes[i].cell,1);
                std::cout<<",\"ring\":["<<center[0]-.5*wr<<','<<center[0]+.5*wr<<','
                    <<center[1]-.5*wz<<','<<center[1]+.5*wz<<','<<density[nodes[i].cell]<<']';
            }
            std::cout<<'}';
        }
        require(bounds.front().value[3].lower>0.,"asymmetric density lost actual parent dipole");
        std::cout<<"],\"far\":[";
        int point=0;
        for(double distance:{100.,1000.})for(auto direction:{std::array<double,2>{1.,0.},
            std::array<double,2>{.6,.8},std::array<double,2>{.6,-.8}}) {
            if(point++)std::cout<<',';
            const double ro=distance*direction[0],zo=distance*direction[1];
            double tail;finite_ring_detail::SignedInterval evaluation;
            const auto potential=ring_node_far_enclosure(nodes.front(),bounds.front(),ro,zo,
                source.gravitational_constant,&tail,&evaluation);
            require(finite_ring_detail::interval_finite(potential)&&tail>0.,"general far interval unavailable");
            long double ref8=0.,ref12=0.;
            for(const auto& node:nodes)if(node.cell>=0) {
                const auto c=op.center(node.cell);const double wr=op.width(node.cell,0),wz=op.width(node.cell,1);
                const long double scale=static_cast<long double>(source.gravitational_constant)*density[node.cell];
                ref8+=scale*independent_ring_potential(c[0]-.5*wr,c[0]+.5*wr,c[1]-.5*wz,c[1]+.5*wz,{ro,0.,zo},8);
                ref12+=scale*independent_ring_potential(c[0]-.5*wr,c[0]+.5*wr,c[1]-.5*wz,c[1]+.5*wz,{ro,0.,zo},12);
            }
            require(potential.lower<=ref8&&ref8<=potential.upper&&potential.lower<=ref12&&ref12<=potential.upper,
                "independent compound Newton reference escaped general q^3 interval");
            std::cout<<"{\"point\":["<<ro<<','<<zo<<"],\"lower\":"<<potential.lower<<",\"upper\":"<<potential.upper
                <<",\"tail_upper\":"<<tail<<",\"evaluation_lower\":"<<evaluation.lower
                <<",\"evaluation_upper\":"<<evaluation.upper<<",\"newton8\":"<<static_cast<double>(ref8)
                <<",\"newton12\":"<<static_cast<double>(ref12)<<'}';
        }
        require(!finite_ring_detail::interval_finite(ring_node_far_enclosure(nodes.front(),bounds.front(),
            .1,nodes.front().center[2],source.gravitational_constant)),"inside full support accepted far expansion");
        auto wrong_center=nodes.front();wrong_center.center[0]=1.;
        require(!finite_ring_detail::interval_finite(ring_node_far_enclosure(wrong_center,bounds.front(),
            100.,0.,source.gravitational_constant)),"non-axis expansion center accepted RZ companion");
        auto invalid=bounds.front();invalid.valid=false;
        require(!finite_ring_detail::interval_finite(ring_node_far_enclosure(nodes.front(),invalid,100.,0.,
            source.gravitational_constant)),"invalid moments accepted far expansion");
        std::cout<<"]}";
    }
    std::cout<<"]}\n";
}

/** Final fitted/recovered stencils and root identity for exact independent audit. */
void native_rz_stencil_probe() {
    const auto dump=[](const auto& v){std::cout<<'[';bool first=true;for(auto x:v){if(!first)std::cout<<',';first=false;std::cout<<x;}std::cout<<']';};
    auto cart=base_mesh(2,4);elliptic::CompositePoisson cart_op(cart,make_cells(cart,false));
    require(cart_op.native_rz_stencil_enclosure(0).status==elliptic::BoundaryErrorStatus::InvalidInput,
        "Cartesian stencil acquired RZ certificate");
    require(cart_op.native_rz_face_enclosure(0).status==elliptic::BoundaryErrorStatus::InvalidInput,
        "Cartesian face acquired RZ geometry certificate");
    std::cout<<std::setprecision(17)<<"{\"cases\":[";bool first=true;
    const auto emit=[&](const elliptic::CompositePoisson& op,bool mixed,int hierarchy_level) {
        const auto& base=op.base();
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"mixed\":"<<mixed<<",\"hierarchy_level\":"<<hierarchy_level<<",\"origin\":";dump(base.origin);
        std::cout<<",\"spacing\":";dump(base.spacing);std::cout<<",\"cells\":[";
        bool fc=true;for(const auto& cell:op.cells()) {
            if(!fc)std::cout<<',';fc=false;
            std::cout<<"{\"level\":"<<cell.level<<",\"index\":";dump(cell.index);std::cout<<'}';
        }
        require(op.native_rz_stencil_enclosure(op.faces().size()).status==elliptic::BoundaryErrorStatus::InvalidInput,
            "out-of-range face acquired coefficient certificate");
        require(op.native_rz_face_enclosure(op.faces().size()).status==elliptic::BoundaryErrorStatus::InvalidInput,
            "missing face acquired geometry certificate");
        std::cout<<"],\"stored_volumes\":";dump(op.volumes());
        std::vector<double> face_values(op.faces().size(),0.),source(op.size(),0.);
        for(std::size_t i=0;i<face_values.size();++i)
            if(op.faces()[i].boundary_side>=0)face_values[i]=.5*(int(i%9)-4);
        const auto rhs=op.effective_rhs(source,face_values);
        const auto construction=op.native_rz_boundary_construction_error(face_values);
        const auto arithmetic=op.bound_rhs_assembly_roundoff(source,face_values,rhs);
        require(construction.status==elliptic::BoundaryErrorStatus::Bounded
            &&arithmetic.status==elliptic::BoundaryErrorStatus::Bounded,"actual B construction/assembly ledger failed");
        require(op.native_rz_boundary_construction_error({}).status==elliptic::BoundaryErrorStatus::InvalidInput,
            "missing boundary values acquired construction certificate");
        auto bad_values=face_values;bad_values[0]=std::numeric_limits<double>::quiet_NaN();
        require(op.native_rz_boundary_construction_error(bad_values).status==elliptic::BoundaryErrorStatus::InvalidInput,
            "nonfinite boundary values acquired construction certificate");
        auto zeros=face_values;std::fill(zeros.begin(),zeros.end(),0.);
        const auto zero_error=op.native_rz_boundary_construction_error(zeros);
        require(zero_error.status==elliptic::BoundaryErrorStatus::Bounded&&zero_error.native_norm_upper==0.,
            "zero B construction acquired floor");
        std::vector<double> combined(op.size());
        for(int i=0;i<op.size();++i) {
            const double sum=construction.cell_bounds[i]+arithmetic.cell_bounds[i];
            combined[i]=sum==0.?0.:std::nextafter(sum,std::numeric_limits<double>::infinity());
        }
        std::vector<elliptic::NativeRzFacePotentialError> errors(op.faces().size());
        std::vector<double> potential_errors(op.faces().size(),0.);
        std::size_t first_boundary=op.faces().size();
        for(std::size_t i=0;i<errors.size();++i)if(op.faces()[i].boundary_side>=0) {
            if(first_boundary==op.faces().size())first_boundary=i;
            potential_errors[i]=std::ldexp(1.,-20)*(1+i%3);
            errors[i].error={potential_errors[i],elliptic::BoundaryErrorQuality::CertifiedAbsolute};
            errors[i].scope=elliptic::NativeRzPotentialScope::RootDyadicSourceAndObserver;
        }
        require(first_boundary<op.faces().size(),"missing RZ physical boundary fixture");
        const auto propagated=op.native_rz_propagate_potential_error(errors);
        require(propagated.status==elliptic::BoundaryErrorStatus::Bounded,"ideal B potential error failed");
        require(op.native_rz_propagate_potential_error({}).status==elliptic::BoundaryErrorStatus::InvalidInput,
            "missing ideal error array accepted");
        auto bad=errors;bad[first_boundary].scope=elliptic::NativeRzPotentialScope::Unknown;
        require(op.native_rz_propagate_potential_error(bad).status==elliptic::BoundaryErrorStatus::UncertifiedInput,
            "stored/unknown coordinate error silently lifted");
        bad=errors;bad[first_boundary].error.quality=elliptic::BoundaryErrorQuality::Estimate;
        require(op.native_rz_propagate_potential_error(bad).status==elliptic::BoundaryErrorStatus::UncertifiedInput,
            "potential estimate certified");
        bad=errors;bad[first_boundary].error.absolute_error=-1.;
        require(op.native_rz_propagate_potential_error(bad).status==elliptic::BoundaryErrorStatus::InvalidInput,
            "negative potential error accepted");
        bad=errors;bad[first_boundary].error.absolute_error=std::numeric_limits<double>::quiet_NaN();
        require(op.native_rz_propagate_potential_error(bad).status==elliptic::BoundaryErrorStatus::InvalidInput,
            "NaN potential error accepted");
        bad=errors;bad[first_boundary].error.absolute_error=std::numeric_limits<double>::max();
        require(op.native_rz_propagate_potential_error(bad).status==elliptic::BoundaryErrorStatus::Overflow,
            "overflow potential propagation accepted");
        bad=errors;for(auto& e:bad)e.error.absolute_error=0.;
        const auto zero_potential=op.native_rz_propagate_potential_error(bad);
        require(zero_potential.status==elliptic::BoundaryErrorStatus::Bounded&&zero_potential.native_norm_upper==0.,
            "zero ideal potential error acquired floor");
        std::vector<double> total_boundary(op.size());
        for(int i=0;i<op.size();++i) {
            const double sum=combined[i]+propagated.cell_bounds[i];
            total_boundary[i]=sum==0.?0.:std::nextafter(sum,std::numeric_limits<double>::infinity());
        }
        const auto total_norm=op.native_rz_norm_interval(total_boundary);
        require(total_norm.status==elliptic::BoundaryErrorStatus::Bounded,"total native boundary norm failed");
        std::vector<double> potential(op.size()),applied(op.size()),residual(op.size());
        for(int i=0;i<op.size();++i) {
            const auto center=op.center(i);potential[i]=.25+center[0]*center[0]+.5*center[1];
        }
        op.apply(potential,applied);
        for(int i=0;i<op.size();++i)residual[i]=applied[i]-rhs[i];
        const auto evaluation=op.native_rz_residual_evaluation_error(potential,rhs,residual);
        require(evaluation.status==elliptic::BoundaryErrorStatus::Bounded,"native A/residual evaluation failed");
        require(op.native_rz_operator_construction_error({}).status==elliptic::BoundaryErrorStatus::InvalidInput,
            "missing phi construction accepted");
        auto bad_phi=potential;bad_phi[0]=std::numeric_limits<double>::quiet_NaN();
        require(op.native_rz_operator_construction_error(bad_phi).status==elliptic::BoundaryErrorStatus::InvalidInput,
            "NaN phi construction accepted");
        require(op.native_rz_residual_evaluation_error(potential,rhs,{}).status==elliptic::BoundaryErrorStatus::InvalidInput,
            "missing residual accepted");
        std::vector<double> fake(op.size(),0.);
        const auto fake_evaluation=op.native_rz_residual_evaluation_error(potential,rhs,fake);
        require(fake_evaluation.status==elliptic::BoundaryErrorStatus::Bounded&&fake_evaluation.native_norm_upper>0.,
            "fake zero residual assumed exact");
        const auto zero_evaluation=op.native_rz_residual_evaluation_error(fake,fake,fake);
        require(zero_evaluation.status==elliptic::BoundaryErrorStatus::Bounded&&zero_evaluation.native_norm_upper==0.,
            "zero A/residual evaluation acquired floor");
        std::vector<double> full_residual_error(op.size());
        for(int i=0;i<op.size();++i) {
            const double sum=total_boundary[i]+evaluation.cell_bounds[i];
            full_residual_error[i]=sum==0.?0.:std::nextafter(sum,std::numeric_limits<double>::infinity());
        }
        const auto full_error_norm=op.native_rz_norm_interval(full_residual_error);
        require(full_error_norm.status==elliptic::BoundaryErrorStatus::Bounded,"full manufactured residual error norm failed");
        const auto combined_norm=op.native_rz_norm_interval(combined);
        require(combined_norm.status==elliptic::BoundaryErrorStatus::Bounded,"combined native construction norm failed");
        std::cout<<",\"face_values\":";dump(face_values);std::cout<<",\"rhs\":";dump(rhs);
        std::cout<<",\"construction_cells\":";dump(construction.cell_bounds);
        std::cout<<",\"construction_native_norm_upper\":"<<construction.native_norm_upper
            <<",\"combined_cells\":";dump(combined);
        std::cout<<",\"combined_native_norm_upper\":"<<combined_norm.upper;
        std::cout<<",\"potential_errors\":";dump(potential_errors);
        std::cout<<",\"propagated_cells\":";dump(propagated.cell_bounds);
        std::cout<<",\"propagated_native_norm_upper\":"<<propagated.native_norm_upper
            <<",\"total_boundary_cells\":";dump(total_boundary);
        std::cout<<",\"total_boundary_native_norm_upper\":"<<total_norm.upper;
        std::cout<<",\"potential\":";dump(potential);std::cout<<",\"computed_applied\":";dump(applied);
        std::cout<<",\"computed_residual\":";dump(residual);
        std::cout<<",\"operator_construction_cells\":";dump(evaluation.construction.cell_bounds);
        std::cout<<",\"operator_construction_native_norm_upper\":"<<evaluation.construction.native_norm_upper
            <<",\"residual_evaluation_cells\":";dump(evaluation.cell_bounds);
        std::cout<<",\"residual_arithmetic_cells\":";dump(evaluation.arithmetic.cell_bounds);
        std::cout<<",\"residual_evaluation_native_norm_upper\":"<<evaluation.native_norm_upper
            <<",\"full_residual_error_cells\":";dump(full_residual_error);
        std::cout<<",\"full_residual_error_native_norm_upper\":"<<full_error_norm.upper;
        std::cout<<",\"faces\":[";fc=true;std::size_t face_index=0;
        for(const auto& face:op.faces()) {
            if(!fc)std::cout<<',';fc=false;
            const bool expected_fit=face.boundary_side>=0||
                (face.left>=0&&face.right>=0&&op.cells()[face.left].level!=op.cells()[face.right].level);
            require(expected_fit?face.construction!=elliptic::FaceStencilConstruction::TwoPoint
                :face.construction==elliptic::FaceStencilConstruction::TwoPoint,"stencil path not recorded");
            std::cout<<"{\"left\":"<<face.left<<",\"right\":"<<face.right<<",\"axis\":"<<face.axis
                <<",\"boundary_side\":"<<face.boundary_side<<",\"construction\":"<<int(face.construction)
                <<",\"center\":";dump(face.center);std::cout<<",\"fragment_width\":";dump(face.fragment_width);
            std::cout<<",\"area\":"<<face.area<<",\"samples\":";dump(face.samples);
            std::cout<<",\"coefficients\":";dump(face.coefficients);
            const auto proof=op.native_rz_stencil_enclosure(face_index++);
            require(proof.status==elliptic::BoundaryErrorStatus::Bounded,"RZ ideal stencil certificate missing");
            std::cout<<",\"boundary_coefficient\":"<<face.boundary_coefficient
                <<",\"coefficient_lower\":";dump(proof.coefficient_lower);
            std::cout<<",\"coefficient_upper\":";dump(proof.coefficient_upper);
            std::cout<<",\"coefficient_error_upper\":";dump(proof.coefficient_error_upper);
            std::cout<<",\"boundary_lower\":"<<proof.boundary_lower<<",\"boundary_upper\":"<<proof.boundary_upper
                <<",\"boundary_error_upper\":"<<proof.boundary_error_upper
                <<",\"inverse_residual_upper\":"<<proof.inverse_residual_upper
                <<",\"inverse_norm_upper\":"<<proof.inverse_norm_upper
                <<",\"lambda_error_upper\":"<<proof.lambda_error_upper;
            const auto geometry=op.native_rz_face_enclosure(face_index-1);
            require(geometry.status==elliptic::BoundaryErrorStatus::Bounded,"RZ face geometry certificate missing");
            std::cout<<",\"center_lower\":";dump(geometry.center_lower);
            std::cout<<",\"center_upper\":";dump(geometry.center_upper);
            std::cout<<",\"center_error_upper\":";dump(geometry.center_error_upper);
            std::cout<<",\"area_lower\":"<<geometry.area_lower<<",\"area_upper\":"<<geometry.area_upper
                <<",\"area_error_upper\":"<<geometry.area_error_upper<<",\"area_over_volume_lower\":";
            dump(geometry.area_over_volume_lower);std::cout<<",\"area_over_volume_upper\":";
            dump(geometry.area_over_volume_upper);std::cout<<",\"area_over_volume_error_upper\":";
            dump(geometry.area_over_volume_error_upper);std::cout<<",\"boundary_map_lower\":";
            dump(geometry.boundary_map_lower);std::cout<<",\"boundary_map_upper\":";
            dump(geometry.boundary_map_upper);std::cout<<",\"boundary_map_error_upper\":";
            dump(geometry.boundary_map_error_upper);std::cout<<'}';
        }
        std::cout<<"]}";
    };
    for(bool mixed:{false,true})for(double origin:{0.,.5,.3})for(int profile:{0,1,2,3}) {
        auto base=base_mesh(2,4);base.geometry=elliptic::Geometry::Cylindrical;
        base.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;base.origin={origin,-.3,0.};
        if(profile==1)base.spacing={.1,.15,1.};
        if(profile==2)base.spacing={.125,.25,1.};
        if(profile==3)base.spacing={.25,.125,1.};
        auto cells=mixed&&profile>=2?make_origin_seam_cells(base):make_cells(base,mixed);
        elliptic::CompositePoisson op(base,std::move(cells),elliptic::BoundaryKind::CurvilinearIsolated);
        emit(op,mixed,-1);
    }
    for(bool mixed:{false,true})for(double origin:{0.,.5}) {
        auto base=base_mesh(2,64);base.cells={64,4,1};
        base.geometry=elliptic::Geometry::Cylindrical;base.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
        base.origin={origin,-.3,0.};
        multigrid::CompositeMultigrid solver(base,make_cells(base,mixed),elliptic::BoundaryKind::CurvilinearIsolated);
        bool rejected=false;try{solver.level_operator(solver.level_count());}catch(const std::out_of_range&){rejected=true;}
        require(rejected,"hierarchy query accepted missing level");
        for(std::size_t level=0;level<solver.level_count();++level)emit(solver.level_operator(level),mixed,int(level));
    }
    std::cout<<"]}\n";
}

/** Dump actual native metric construction for independent root-coordinate proofs. */
void native_rz_measure_probe() {
    const auto dump=[](const auto& v){std::cout<<'[';bool first=true;for(auto x:v){if(!first)std::cout<<',';first=false;std::cout<<x;}std::cout<<']';};
    auto cart=base_mesh(2,4);
    elliptic::CompositePoisson cart_op(cart,make_cells(cart,false));
    require(cart_op.native_rz_measure_enclosure().status==elliptic::BoundaryErrorStatus::InvalidInput,
        "Cartesian operator acquired RZ certificate");
    std::cout<<std::setprecision(17)<<"{\"cases\":[";bool first=true;
    for(bool mixed:{false,true})for(double origin:{0.,.5,.3})for(bool decimal:{false,true}) {
        auto base=base_mesh(2,4);base.geometry=elliptic::Geometry::Cylindrical;
        base.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;base.origin={origin,-.3,0.};
        if(decimal)base.spacing={.1,.15,1.};
        elliptic::CompositePoisson op(base,make_cells(base,mixed),elliptic::BoundaryKind::CurvilinearIsolated);
        const auto m=op.native_rz_measure_enclosure();
        require(m.status==elliptic::BoundaryErrorStatus::Bounded,"native measure enclosure failed");
        std::vector<double> values(op.size());
        for(int i=0;i<op.size();++i)values[i]=(i%2?-1.:1.)*(1.+.1*i);
        const auto norm=op.native_rz_norm_interval(values);
        require(norm.status==elliptic::BoundaryErrorStatus::Bounded,"physical RMS failed");
        require(op.native_rz_norm_interval({}).status==elliptic::BoundaryErrorStatus::InvalidInput,
            "RMS accepted missing input");
        auto invalid=values;invalid[0]=std::numeric_limits<double>::quiet_NaN();
        require(op.native_rz_norm_interval(invalid).status==elliptic::BoundaryErrorStatus::InvalidInput,
            "RMS accepted nonfinite input");
        std::vector<double> zero(op.size(),0.);
        const auto zn=op.native_rz_norm_interval(zero);
        require(zn.status==elliptic::BoundaryErrorStatus::Bounded&&zn.lower==0.&&zn.upper==0.,
            "physical RMS added zero floor");
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"mixed\":"<<mixed<<",\"origin\":"<<origin<<",\"spacing\":";
        dump(base.spacing);std::cout<<",\"cells\":[";
        bool fc=true;for(const auto& cell:op.cells()) {
            if(!fc)std::cout<<',';fc=false;
            std::cout<<"{\"level\":"<<cell.level<<",\"index\":";dump(cell.index);std::cout<<'}';
        }
        std::cout<<"],\"stored_volumes\":";dump(op.volumes());
        std::cout<<",\"stored_weights\":";dump(op.norm_weights());
        std::cout<<",\"volume_lower\":";dump(m.volume_lower);
        std::cout<<",\"volume_upper\":";dump(m.volume_upper);
        std::cout<<",\"volume_error\":";dump(m.volume_error_upper);
        std::cout<<",\"weight_lower\":";dump(m.weight_lower);
        std::cout<<",\"weight_upper\":";dump(m.weight_upper);
        std::cout<<",\"weight_error\":";dump(m.weight_error_upper);
        std::cout<<",\"total_lower\":"<<m.total_volume_lower<<",\"total_upper\":"<<m.total_volume_upper
            <<",\"values\":";dump(values);
        std::cout<<",\"norm_lower\":"<<norm.lower<<",\"norm_upper\":"<<norm.upper<<'}';
    }
    std::cout<<"]}\n";
}

/** Actual identity-bound composition; raw vectors remain local for Fraction checks. */
void finite_ring_rhs_probe() {
    using namespace Physical::Gravity;
    const auto dump=[](const auto& v){std::cout<<'[';bool first=true;for(auto x:v){if(!first)std::cout<<',';first=false;std::cout<<x;}std::cout<<']';};
    std::cout<<std::setprecision(17)<<"{\"cases\":[";bool first=true;
    for(bool mixed:{false,true})for(double origin:{0.,.5,.3})for(bool zero:{false,true}) {
        auto base=base_mesh(2,4);base.geometry=elliptic::Geometry::Cylindrical;
        base.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;base.origin={origin,-.5,0.};
        multigrid::CompositeMultigrid solver(base,make_cells(base,mixed),elliptic::BoundaryKind::CurvilinearIsolated);
        const auto& op=solver.op();auto& execution=solver.execution();
        GravityBoundary tree(op,{7});GravitySolveIdentity id;id.topology={7};
        id.gravitational_constant=constants::gravity::cgs::gravitational_constant;
        id.operator_revision=id.boundary_revision=id.accuracy_revision=1;
        id.inputs.push_back({{{1},{7}},state::StateSlot::Current,{1},1});
        std::vector<double> density(op.size(),0.);
        if(!zero)for(int i=0;i<op.size();++i)density[i]=1.+.3*op.center(i)[1];
        tree.update(density,id);RingBoundaryControl control;control.face_absolute_target=1.e-5;
        control.maximum_boxes_per_leaf=8;
        const auto ring=tree.ring_boundary(op,id,control);
        require(ring.status==RingBoundaryStatus::Bounded,"composition ring input not bounded");
        const auto root_errors=tree.root_scoped_ring_errors(op,ring);
        const bool root_exact=origin!=.3;
        const auto ideal_potential=op.native_rz_propagate_potential_error(root_errors);
        require(ideal_potential.status==(root_exact?elliptic::BoundaryErrorStatus::Bounded
            :elliptic::BoundaryErrorStatus::UncertifiedInput),"producer root scope proof incorrect");
        auto scope_stale=ring;scope_stale.source_generation--;
        bool scope_rejected=false;
        try{tree.root_scoped_ring_errors(op,scope_stale);}catch(const std::exception&){scope_rejected=true;}
        require(scope_rejected,"root producer accepted stale source generation");
        auto missing_scope=ring;missing_scope.errors.clear();scope_rejected=false;
        try{tree.root_scoped_ring_errors(op,missing_scope);}catch(const std::invalid_argument&){scope_rejected=true;}
        require(scope_rejected,"root producer accepted missing face errors");
        auto estimated_scope=ring;
        for(auto& error:estimated_scope.errors)error.quality=elliptic::BoundaryErrorQuality::Estimate;
        const auto estimates=tree.root_scoped_ring_errors(op,estimated_scope);
        for(const auto& input:estimates)
            require(input.scope==elliptic::NativeRzPotentialScope::Unknown,"estimated producer acquired root certificate");
        auto other_base=base;other_base.origin[1]+=.125;
        elliptic::CompositePoisson other(other_base,make_cells(other_base,mixed),elliptic::BoundaryKind::CurvilinearIsolated);
        scope_rejected=false;
        try{tree.root_scoped_ring_errors(other,ring);}catch(const std::logic_error&){scope_rejected=true;}
        require(scope_rejected,"root producer accepted different operator geometry");

        const auto rho=execution.upload(density);auto src=execution.array<double>(op.size());
        const double factor=-4.*constants::math::pi*constants::gravity::cgs::gravitational_constant;
        execution.linear(src,factor,rho,0.,{},0.);
        const auto source=execution.download(src),rhs=op.effective_rhs(source,ring.values);
        std::vector<double> phi(op.size(),0.),residual(op.size());
        op.apply(phi,residual);for(int i=0;i<op.size();++i)residual[i]-=rhs[i];
        const auto result=tree.assess_ring_rhs(op,ring,source,rhs,phi,residual,1.e-10,0.);
        const auto native=tree.assess_native_ring_rhs(op,ring,source,rhs,phi,residual,1.e-10,0.);
        require(native.scope==RingRhsAssessmentScope::RootDyadicNativeOperator
            &&native.physical_status==elliptic::BoundaryResidualStatus::UncertifiedInput,
            "native discrete certificate promoted full RZ capability");
        require(native.conditional.status==(root_exact?(zero?elliptic::BoundaryResidualStatus::Accepted
            :elliptic::BoundaryResidualStatus::ResidualTooLarge):elliptic::BoundaryResidualStatus::UncertifiedInput),
            "native actual original request changed");
        if(root_exact) {
            if(zero)require(native.conditional.total_residual_upper==0.&&native.conditional.tolerance_safe==0.,
                "native zero acquired floor");
            auto fake_residual=residual;std::fill(fake_residual.begin(),fake_residual.end(),0.);
            const auto fake=tree.assess_native_ring_rhs(op,ring,source,rhs,phi,fake_residual,1.e-10,0.);
            if(!zero)require(fake.conditional.status!=elliptic::BoundaryResidualStatus::Accepted,
                "native fake zero residual hid actual unsolved source");
            require(tree.assess_native_ring_rhs(op,ring,{},rhs,phi,residual,1.e-10,0.).conditional.status
                ==elliptic::BoundaryResidualStatus::InvalidInput,"native missing source accepted");
        }

        require(result.scope==RingRhsAssessmentScope::StoredNativeOperator
            &&result.physical_status==elliptic::BoundaryResidualStatus::UncertifiedInput,
            "missing geometry construction became physical acceptance");
        require(result.source_error.status==GravitySourceBoundStatus::Bounded
            &&result.combined_rhs_error.status==elliptic::BoundaryErrorStatus::Bounded,
            "actual source/face/assembly composition failed");
        require(result.conditional.status==(zero?elliptic::BoundaryResidualStatus::Accepted
            :elliptic::BoundaryResidualStatus::ResidualTooLarge),"original tolerance comparison changed");
        if(zero)require(result.conditional.total_residual_upper==0.&&result.conditional.tolerance_safe==0.,
            "zero source/RHS acquired floor");
        auto wrong_residual=residual;
        if(!zero) {
            std::fill(wrong_residual.begin(),wrong_residual.end(),0.);
            const auto wrong=tree.assess_ring_rhs(op,ring,source,rhs,phi,wrong_residual,1.e-10,0.);
            require(wrong.conditional.status!=elliptic::BoundaryResidualStatus::Accepted
                &&wrong.residual_error.norm_upper>0.,"fake zero residual hid actual evaluation error");
        }
        auto stale=ring;stale.source_generation--;
        bool rejected=false;try{tree.assess_ring_rhs(op,stale,source,rhs,phi,residual,1.e-10,0.);}
        catch(const std::exception&){rejected=true;}
        require(rejected,"composition accepted stale ring source generation");
        require(tree.assess_ring_rhs(op,ring,{},rhs,phi,residual,1.e-10,0.).conditional.status
            ==elliptic::BoundaryResidualStatus::InvalidInput,"missing physical source accepted");
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"mixed\":"<<mixed<<",\"origin\":"<<origin<<",\"zero\":"<<zero<<",\"density\":";dump(density);
        std::cout<<",\"root_exact\":"<<root_exact<<",\"root_origin\":";dump(base.origin);
        std::cout<<",\"root_spacing\":";dump(base.spacing);
        std::cout<<",\"source_geometry\":[";bool geometry_first=true;
        for(int i=0;i<op.size();++i) {
            if(!geometry_first)std::cout<<',';geometry_first=false;
            std::cout<<"{\"level\":"<<op.cells()[i].level<<",\"index\":";dump(op.cells()[i].index);
            std::array<double,4> edges{};for(int a=0;a<2;++a) {
                edges[2*a]=op.center(i)[a]-.5*op.width(i,a);
                edges[2*a+1]=op.center(i)[a]+.5*op.width(i,a);
            }
            std::cout<<",\"edges\":";dump(edges);std::cout<<'}';
        }
        std::cout<<']';
        if(root_exact) {
            std::cout<<",\"native_rhs_error_cells\":";dump(native.combined_rhs_error.cell_bounds);
            std::cout<<",\"native_eval_cells\":";dump(native.native_residual_error.cell_bounds);
            std::cout<<",\"native_rhs_error_upper\":"<<native.conditional.rhs_error_upper
                <<",\"native_residual_upper\":"<<native.conditional.residual_norm_upper
                <<",\"native_total_upper\":"<<native.conditional.total_residual_upper
                <<",\"native_tolerance_safe\":"<<native.conditional.tolerance_safe
                <<",\"native_rhs_norm_lower\":"<<native.conditional.rhs_norm_lower;
        }
        std::cout<<",\"source\":";dump(source);std::cout<<",\"rhs\":";dump(rhs);
        std::cout<<",\"volumes\":";dump(op.volumes());std::cout<<",\"weights\":";dump(op.norm_weights());
        std::cout<<",\"face_lower\":";dump(ring.lower);std::cout<<",\"face_upper\":";dump(ring.upper);
        std::cout<<",\"source_lower\":";dump(result.source_error.lower);std::cout<<",\"source_upper\":";dump(result.source_error.upper);
        std::cout<<",\"combined_cells\":";dump(result.combined_rhs_error.cell_bounds);
        std::cout<<",\"combined_norm_upper\":"<<result.combined_rhs_error.norm_upper
            <<",\"source_norm_upper\":"<<result.source_error.norm_upper
            <<",\"boundary_norm_upper\":"<<result.boundary_error.norm_upper
            <<",\"assembly_norm_upper\":"<<result.assembly_error.norm_upper
            <<",\"residual_norm_upper\":"<<result.residual_error.norm_upper
            <<",\"total_residual_upper\":"<<result.conditional.total_residual_upper
            <<",\"tolerance_safe\":"<<result.conditional.tolerance_safe<<",\"faces\":[";
        bool first_face=true;
        for(std::size_t f=0;f<op.faces().size();++f) {
            const auto& face=op.faces()[f];
            if(face.boundary_side<0)continue;
            if(!first_face)std::cout<<',';first_face=false;
            std::cout<<"{\"index\":"<<f<<",\"left\":"<<face.left<<",\"right\":"<<face.right
                <<",\"axis\":"<<face.axis<<",\"boundary_side\":"<<face.boundary_side
                <<",\"root_scope\":"<<(root_errors[f].scope==elliptic::NativeRzPotentialScope::RootDyadicSourceAndObserver)
                <<",\"center\":["<<face.center[0]<<','<<face.center[1]<<']'
                <<",\"area\":"<<face.area<<",\"boundary_coefficient\":"<<face.boundary_coefficient<<'}';
        }
        std::cout<<"]}";
    }
    std::cout<<"]}\n";
}

/** Actual isolated nonzero finite-ring solve; no driver/timestep/output. */
/** Retire actual ring certificates after density/time/storage or topology change.
 * Static operator fixture only: not a simulation stage or a production publish.
 */
void ring_source_retirement_contract() {
    using namespace Physical::Gravity;
    for(double origin:{0.,.5}) {
        auto base=base_mesh(2,4);base.geometry=elliptic::Geometry::Cylindrical;
        base.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;base.origin={origin,-.5,0.};
        elliptic::CompositePoisson op(base,make_cells(base,true),elliptic::BoundaryKind::CurvilinearIsolated);
        GravityBoundary tree(op,{9});GravitySolveIdentity id;id.topology={9};
        id.gravitational_constant=constants::gravity::cgs::gravitational_constant;
        id.operator_revision=id.boundary_revision=id.accuracy_revision=1;
        id.inputs.push_back({{{1},{9}},state::StateSlot::Current,{1},1});
        std::vector<double> density(op.size(),1.);
        tree.update(density,id);RingBoundaryControl control;
        control.face_absolute_target=1.e-5;control.maximum_boxes_per_leaf=8;
        const auto ring=tree.ring_boundary(op,id,control);
        require(ring.status==RingBoundaryStatus::Bounded,"retirement fixture ring failed");
        tree.require_current_ring(op,ring);
        const auto before=tree.root_scoped_ring_errors(op,ring);
        require(op.native_rz_propagate_potential_error(before).status==elliptic::BoundaryErrorStatus::Bounded,
            "retirement fixture missing current root certificate");
        auto changed=id;changed.input_time=.125;
        changed.inputs[0].version={2};changed.inputs[0].storage_generation=2;
        density[0]=1.25;tree.update(density,changed);
        bool rejected=false;
        try{tree.root_scoped_ring_errors(op,ring);}catch(const std::logic_error&){rejected=true;}
        require(rejected,"actual density update retained old root certificate");
        rejected=false;
        try{tree.ring_boundary(op,id,control);}catch(const std::logic_error&){rejected=true;}
        require(rejected,"actual density update accepted old request identity");
        const auto current=tree.ring_boundary(op,changed,control);
        require(current.status==RingBoundaryStatus::Bounded&&current.source==changed
            &&current.source_generation>ring.source_generation,"updated density not bound to new generation");
        tree.require_current_ring(op,current);
        auto wrong_epoch=changed;wrong_epoch.topology={10};wrong_epoch.inputs[0].block.epoch={10};
        rejected=false;
        try{tree.update(density,wrong_epoch);}catch(const std::logic_error&){rejected=true;}
        require(rejected,"bound tree accepted changed AMR epoch");
        rejected=false;
        try{tree.require_current_ring(op,current);}catch(const std::logic_error&){rejected=true;}
        require(rejected,"rejected source update left certificate consumable");
    }
    std::cout<<"RING_ACTUAL_SOURCE_RETIREMENT_PASS cases=2 static_mixed_mesh=1 no_simulation=1\n";
}

void native_ring_solved_probe(bool mixed=false,bool dynamic_budget=false,bool budget_only=false) {
    using namespace Physical::Gravity;
    const auto dump=[](const auto& x){std::cout<<'[';bool first=true;for(auto v:x){if(!first)std::cout<<',';first=false;std::cout<<v;}std::cout<<']';};
    std::cout<<std::setprecision(17)<<"{\"cases\":[";bool first=true;
    for(double origin:{0.,.5}) {
        auto base=base_mesh(2,4);base.geometry=elliptic::Geometry::Cylindrical;
        base.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;base.origin={origin,-.5,0.};
        multigrid::CompositeMultigrid solver(base,make_cells(base,mixed),elliptic::BoundaryKind::CurvilinearIsolated);
        const auto& op=solver.op();GravityBoundary tree(op,{9});GravitySolveIdentity id;id.topology={9};
        id.gravitational_constant=constants::gravity::cgs::gravitational_constant;
        id.operator_revision=id.boundary_revision=id.accuracy_revision=1;
        id.inputs.push_back({{{1},{9}},state::StateSlot::Current,{1},1});
        std::vector<double> density(op.size(),1.);
        if(budget_only)for(int i=0;i<op.size();++i)density[i]=.25+.125*((3*i)%11);
        tree.update(density,id);
        auto& execution=solver.execution();auto src=execution.array<double>(op.size());
        execution.linear(src,-4.*constants::math::pi*constants::gravity::cgs::gravitational_constant,
            execution.upload(density),0.,{},0.);
        const auto source=execution.download(src);
        RingBoundaryControl control;control.face_absolute_target=1.e-18;
        const auto proposal=tree.propose_ring_budget(op,id,source,1.e-10,0.);
        if(dynamic_budget) {
            require(proposal.status==RingBudgetStatus::Proposed,"dynamic budget proposal failed");
            control=proposal.control;
        }
        control.maximum_boxes_per_leaf=65536;
        if(budget_only) {
            require(proposal.basis==RingBudgetBasis::PositiveIsolatedRhs,
                "heterogeneous source missing proved RHS basis");
            const auto bounds=bound_isolated_gravity_source(op,density,source);
            if(!first)std::cout<<',';first=false;
            std::cout<<"{\"budget_only\":true,\"dynamic_budget\":true,\"mixed\":"<<mixed
                <<",\"radial_origin\":"<<origin<<",\"origin\":";dump(base.origin);
            std::cout<<",\"spacing\":";dump(base.spacing);
            std::cout<<",\"source_lower\":";dump(bounds.lower);
            std::cout<<",\"source_upper\":";dump(bounds.upper);
            std::cout<<",\"proposal_basis\":"<<int(proposal.basis)
                <<",\"proposal_source_norm_lower\":"<<proposal.source_norm_lower
                <<",\"proposal_rhs_norm_lower\":"<<proposal.rhs_norm_lower
                <<",\"proposal_mass_lower\":"<<proposal.mass_lower
                <<",\"proposal_distance_upper\":"<<proposal.maximum_distance_upper
                <<",\"proposal_potential_magnitude_lower\":"<<proposal.potential_magnitude_lower
                <<",\"proposal_tolerance\":"<<proposal.initial_tolerance
                <<",\"boundary_sensitivity_upper\":"<<proposal.boundary_sensitivity_upper
                <<",\"face_target\":"<<control.face_absolute_target
                <<",\"proposal_rhs_cell_lower\":";dump(proposal.rhs_cell_magnitude_lower);
            std::cout<<",\"sensitivity_cells\":";dump(op.native_rz_boundary_sensitivity().cell_coefficients);
            std::cout<<",\"cells\":[";
            for(int i=0;i<op.size();++i) {
                if(i)std::cout<<',';std::cout<<"{\"level\":"<<op.cells()[i].level
                    <<",\"index\":";dump(op.cells()[i].index);
                std::cout<<",\"density\":"<<density[i]<<'}';
            }
            std::cout<<"],\"faces\":[";
            for(std::size_t i=0;i<op.faces().size();++i) {
                if(i)std::cout<<',';const auto& f=op.faces()[i];
                std::cout<<"{\"axis\":"<<f.axis<<",\"side\":"<<f.boundary_side%2
                    <<",\"left\":"<<f.left<<",\"right\":"<<f.right
                    <<",\"construction\":"<<int(f.construction)
                    <<",\"boundary_side\":"<<f.boundary_side<<",\"samples\":";dump(f.samples);
                std::cout<<'}';
            }
            std::cout<<"]}";continue;
        }
        const auto ring=tree.ring_boundary(op,id,control);
        if(ring.status!=RingBoundaryStatus::Bounded) {
            std::cerr<<"RING_BUDGET_DIAGNOSTIC {\"origin\":"<<origin
                <<",\"rootCells\":"<<op.size()<<",\"status\":"<<int(ring.status)
                <<",\"faceAbsoluteTarget\":"<<control.face_absolute_target
                <<",\"maximumBoxesPerLeaf\":"<<control.maximum_boxes_per_leaf
                <<",\"maximumLeafEvaluations\":"<<control.maximum_leaf_evaluations
                <<",\"leafEvaluations\":"<<ring.leaf_evaluations
                <<",\"parentEvaluations\":"<<ring.parent_evaluations
                <<",\"rangeEvaluations\":"<<ring.range_evaluations
                <<",\"kernelEnclosures\":"<<ring.kernel_enclosures
                <<",\"agmIterations\":"<<ring.agm_iterations<<"}\n";
        }
        require(ring.status==RingBoundaryStatus::Bounded,"actual native solve ring budget failed");
        const auto rhs=op.effective_rhs(source,ring.values);
        const auto solved=solver.solve(rhs,{1.e-10,0.,200});
        require(solved.report.status==multigrid::SolveStatus::Converged,"actual native algebraic solve failed");
        std::vector<double> residual(op.size());op.apply(solved.potential,residual);
        for(int i=0;i<op.size();++i)residual[i]-=rhs[i];
        const auto result=tree.assess_native_ring_rhs(op,ring,source,rhs,solved.potential,residual,1.e-10,0.);
        if(result.conditional.status!=elliptic::BoundaryResidualStatus::Accepted) {
            std::cerr<<std::setprecision(17)<<"NATIVE_RESIDUAL_DIAGNOSTIC {\"origin\":"<<origin
                <<",\"mixed\":"<<mixed<<",\"cells\":"<<op.size()
                <<",\"status\":"<<int(result.conditional.status)
                <<",\"totalResidualUpper\":"<<result.conditional.total_residual_upper
                <<",\"toleranceSafe\":"<<result.conditional.tolerance_safe
                <<",\"rhsErrorUpper\":"<<result.conditional.rhs_error_upper
                <<",\"evaluationErrorUpper\":"<<result.native_residual_error.native_norm_upper<<"}\n";
        }
        require(result.conditional.status==elliptic::BoundaryResidualStatus::Accepted,
            "actual solved native residual exceeds original request");
        require(result.physical_status==elliptic::BoundaryResidualStatus::UncertifiedInput,
            "native solved residual became full physics release");
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"mixed\":"<<mixed<<",\"radial_origin\":"<<origin<<",\"origin\":";dump(base.origin);
        std::cout<<",\"spacing\":";dump(base.spacing);
        std::cout<<",\"cells\":[";bool cf=true;
        for(int i=0;i<op.size();++i) {
            const auto& cell=op.cells()[i];
            if(!cf)std::cout<<',';cf=false;
            const auto center=op.center(i);
            std::cout<<"{\"level\":"<<cell.level<<",\"index\":";dump(cell.index);
            std::cout<<",\"density\":"<<density[i]<<",\"edges\":["
                <<center[0]-.5*op.width(i,0)<<','<<center[0]+.5*op.width(i,0)<<','
                <<center[1]-.5*op.width(i,1)<<','<<center[1]+.5*op.width(i,1)<<"]}";
        }
        tree.require_current_ring(op,ring);
        std::cout<<"],\"source_identity\":{\"topology\":"<<ring.source.topology.value
            <<",\"time\":"<<ring.source.input_time<<",\"G\":"<<ring.source.gravitational_constant
            <<",\"operator_revision\":"<<ring.source.operator_revision
            <<",\"boundary_revision\":"<<ring.source.boundary_revision
            <<",\"accuracy_revision\":"<<ring.source.accuracy_revision
            <<",\"generation\":"<<ring.source_generation<<",\"inputs\":[";
        bool dependency_first=true;
        for(const auto& input:ring.source.inputs) {
            if(!dependency_first)std::cout<<',';dependency_first=false;
            std::cout<<"{\"uid\":"<<input.block.uid.value<<",\"epoch\":"<<input.block.epoch.value
                <<",\"slot\":"<<int(input.slot)<<",\"version\":"<<input.version.value
                <<",\"storage_generation\":"<<input.storage_generation<<'}';
        }
        std::cout<<"]},\"source_scope\":\"static numerical domain; abstract dependency, not Runtime block publication\"";
        std::cout<<",\"coalesced_attempts\":"<<ring.coalesced_parent_attempts
            <<",\"coalesced_acceptances\":"<<ring.coalesced_parent_acceptances
            <<",\"coalesced_native_leaves\":"<<ring.coalesced_native_leaves;
        std::cout<<",\"dynamic_budget\":"<<dynamic_budget
            <<",\"face_target\":"<<control.face_absolute_target
            <<",\"proposal_source_norm_lower\":"<<proposal.source_norm_lower

            <<",\"proposal_basis\":"<<int(proposal.basis)
            <<",\"proposal_rhs_norm_lower\":"<<proposal.rhs_norm_lower
            <<",\"proposal_mass_lower\":"<<proposal.mass_lower
            <<",\"proposal_distance_upper\":"<<proposal.maximum_distance_upper
            <<",\"proposal_potential_magnitude_lower\":"<<proposal.potential_magnitude_lower
            <<",\"proposal_rhs_cell_lower\":";dump(proposal.rhs_cell_magnitude_lower);
        std::cout            <<",\"proposal_tolerance\":"<<proposal.initial_tolerance
            <<",\"boundary_sensitivity_upper\":"<<proposal.boundary_sensitivity_upper
            <<",\"sensitivity_cells\":";dump(op.native_rz_boundary_sensitivity().cell_coefficients);
        std::cout<<",\"source\":";dump(source);std::cout<<",\"rhs\":";dump(rhs);
        std::cout<<",\"source_lower\":";dump(result.source_error.lower);
        std::cout<<",\"source_upper\":";dump(result.source_error.upper);
        std::cout<<",\"potential\":";dump(solved.potential);std::cout<<",\"residual\":";dump(residual);
        std::cout<<",\"face_values\":";dump(ring.values);std::cout<<",\"face_lower\":";dump(ring.lower);
        std::cout<<",\"face_upper\":";dump(ring.upper);
        std::cout<<",\"rhs_error_cells\":";dump(result.combined_rhs_error.cell_bounds);
        std::cout<<",\"evaluation_cells\":";dump(result.native_residual_error.cell_bounds);
        std::cout<<",\"total_residual_upper\":"<<result.conditional.total_residual_upper
            <<",\"tolerance_safe\":"<<result.conditional.tolerance_safe
            <<",\"rhs_error_upper\":"<<result.conditional.rhs_error_upper
            <<",\"residual_upper\":"<<result.conditional.residual_norm_upper
            <<",\"rhs_norm_lower\":"<<result.conditional.rhs_norm_lower
            <<",\"work\":"<<ring.leaf_evaluations+ring.parent_evaluations
            <<",\"range_evaluations\":"<<ring.range_evaluations<<",\"faces\":[";
        bool ff=true;for(std::size_t f=0;f<op.faces().size();++f) {
            if(!ff)std::cout<<',';ff=false;const auto& face=op.faces()[f];
            std::cout<<"{\"index\":"<<f<<",\"axis\":"<<face.axis
                <<",\"side\":"<<face.boundary_side%2<<",\"left\":"<<face.left
                <<",\"right\":"<<face.right<<",\"construction\":"<<int(face.construction)<<",\"boundary_side\":"<<face.boundary_side
                <<",\"center\":";dump(face.center);std::cout<<",\"samples\":";dump(face.samples);
            std::cout<<",\"coefficients\":";dump(face.coefficients);
            std::cout<<",\"boundary_coefficient\":"<<face.boundary_coefficient
                <<",\"area\":"<<face.area<<",\"gradient\":"
                <<op.face_gradient(solved.potential,face,
                    face.boundary_side>=0?ring.values[f]:0.)<<'}';
        }
        std::cout<<"]}";
    }
    std::cout<<"]}\n";
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

void isolated_gravity_source_probe() {
    using namespace Physical::Gravity;
    auto base=base_mesh(2,4);base.geometry=elliptic::Geometry::Cylindrical;
    base.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
    base.origin={0.,-.5,0.};
    elliptic::CompositePoisson op(base,make_cells(base,true),elliptic::BoundaryKind::CurvilinearIsolated);
    const double samples[]{0.,1.e-310,1.e-300,1.e-10,1.,1.e7,1.e100,1.e300,
        std::numeric_limits<double>::max()};
    std::vector<double> density(op.size()),source(op.size());
    const double factor=-4.*constants::math::pi*constants::gravity::cgs::gravitational_constant;
    for(int i=0;i<op.size();++i){density[i]=samples[i%9];source[i]=factor*density[i];}
    const auto bounds=bound_isolated_gravity_source(op,density,source);
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
    require(bound_isolated_gravity_source(op,zero,zero).norm_upper==0.,
        "exact zero source acquired hidden floor");
    auto tiny=density;std::fill(tiny.begin(),tiny.end(),std::numeric_limits<double>::denorm_min());
    require(bound_isolated_gravity_source(op,tiny,zero).status==GravitySourceBoundStatus::CollapsedToZero,
        "positive unrepresentable source accepted as zero");
    tiny[0]=-1.;
    require(bound_isolated_gravity_source(op,tiny,source).status==GravitySourceBoundStatus::InvalidInput,
        "negative physical density accepted");
    auto invalid=source;invalid[0]=std::numeric_limits<double>::quiet_NaN();
    require(bound_isolated_gravity_source(op,density,invalid).status==GravitySourceBoundStatus::InvalidInput,
        "NaN source acquired certificate");
    invalid=source;invalid[8]=std::numeric_limits<double>::max();
    require(bound_isolated_gravity_source(op,density,invalid).status==GravitySourceBoundStatus::Overflow,
        "unrepresentable error distance acquired finite certificate");
    auto cart=base_mesh(2,4);
    elliptic::CompositePoisson periodic(cart,make_cells(cart,false),elliptic::BoundaryKind::Periodic);
    require(bound_isolated_gravity_source(periodic,{},{}).status==GravitySourceBoundStatus::UnsupportedPeriodic,
        "total density certified as periodic contrast source");
    std::cout<<",\"negativePass\":true}\n";
}

void native_arithmetic_ledger_probe() {
    using namespace elliptic;
    std::cout<<std::setprecision(17)<<"{\"cases\":[";bool first=true;
    const auto dump=[](const auto& values) {
        std::cout<<'[';bool first_value=true;
        for(auto value:values){if(!first_value)std::cout<<',';first_value=false;std::cout<<value;}
        std::cout<<']';
    };
    for(bool rz:{false,true})for(bool mixed:{false,true})for(double origin:{0.,.5}) {
        if(!rz&&origin!=0.)continue;
        auto base=base_mesh(2,4);
        if(rz){base.geometry=Geometry::Cylindrical;base.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;}
        base.origin={origin,-.5,0.};
        CompositePoisson op(base,make_cells(base,mixed),
            rz?BoundaryKind::CurvilinearIsolated:BoundaryKind::Dirichlet);
        for(int lane=0;lane<3;++lane) {
            std::vector<double> phi(op.size()),source(op.size()),boundary(op.faces().size()),residual(op.size());
            for(int i=0;i<op.size();++i) {
                if(lane==1){phi[i]=std::sin(.4*i)+.125;source[i]=.1+.2*i;}
                if(lane==2){phi[i]=1.e12+i*.0001220703125;source[i]=(i%2?-.03:.03);}
            }
            for(std::size_t i=0;i<boundary.size();++i) {
                if(lane==1)boundary[i]=.3+.1*i;
                if(lane==2)boundary[i]=1.e12-i*.0001220703125;
            }
            const auto rhs=op.effective_rhs(source,boundary);
            op.apply(phi,residual);for(int i=0;i<op.size();++i)residual[i]-=rhs[i];
            const auto assembly=op.bound_rhs_assembly_roundoff(source,boundary,rhs);
            const auto evaluation=op.bound_residual_evaluation_roundoff(phi,rhs,residual);
            require(assembly.status==BoundaryErrorStatus::Bounded
                &&evaluation.status==BoundaryErrorStatus::Bounded
                &&assembly.scope==PoissonArithmeticScope::StoredNativeCoefficients,
                "native canonical arithmetic ledger unavailable");
            if(lane==0)require(assembly.norm_upper==0.&&evaluation.norm_upper==0.,
                "exact zero operator arithmetic acquired hidden floor");
            if(!first)std::cout<<',';first=false;
            std::cout<<"{\"rz\":"<<rz<<",\"mixed\":"<<mixed<<",\"origin\":"<<origin<<",\"lane\":"<<lane;
            std::cout<<",\"phi\":";dump(phi);std::cout<<",\"source\":";dump(source);
            std::cout<<",\"boundary\":";dump(boundary);std::cout<<",\"rhs\":";dump(rhs);
            std::cout<<",\"residual\":";dump(residual);std::cout<<",\"volumes\":";dump(op.volumes());
            std::cout<<",\"weights\":";dump(op.norm_weights());
            std::cout<<",\"rhsBounds\":";dump(assembly.cell_bounds);
            std::cout<<",\"residualBounds\":";dump(evaluation.cell_bounds);
            std::cout<<",\"rhsNormUpper\":"<<assembly.norm_upper
                <<",\"residualNormUpper\":"<<evaluation.norm_upper<<",\"faces\":[";
            bool first_face=true;
            for(const auto& f:op.faces()) {
                if(!first_face)std::cout<<',';first_face=false;
                std::cout<<"{\"left\":"<<f.left<<",\"right\":"<<f.right<<",\"area\":"<<f.area
                    <<",\"bc\":"<<f.boundary_coefficient<<",\"samples\":";dump(f.samples);
                std::cout<<",\"coefficients\":";dump(f.coefficients);std::cout<<'}';
            }
            std::cout<<"]}";
            auto wrong=residual;wrong[0]+=1.;
            require(op.bound_residual_evaluation_roundoff(phi,rhs,wrong).cell_bounds[0]>=.99,
                "mismatched computed residual acquired zero arithmetic certificate");
            wrong[0]=std::numeric_limits<double>::quiet_NaN();
            require(op.bound_residual_evaluation_roundoff(phi,rhs,wrong).status==BoundaryErrorStatus::InvalidInput,
                "nonfinite residual acquired certificate");
            require(op.bound_rhs_assembly_roundoff({},boundary,rhs).status==BoundaryErrorStatus::InvalidInput,
                "missing source acquired certificate");
            if(lane==0) {
                auto huge=phi;
                for(std::size_t i=0;i<huge.size();++i)
                    huge[i]=i%2?-std::numeric_limits<double>::max():std::numeric_limits<double>::max();
                require(op.bound_residual_evaluation_roundoff(huge,rhs,residual).status==BoundaryErrorStatus::Overflow,
                    "overflowed canonical residual acquired finite certificate");
                auto huge_boundary=boundary;
                std::fill(huge_boundary.begin(),huge_boundary.end(),std::numeric_limits<double>::max());
                require(op.bound_rhs_assembly_roundoff(source,huge_boundary,rhs).status==BoundaryErrorStatus::Overflow,
                    "overflowed boundary assembly acquired finite certificate");
            }

        }
    }
    std::cout<<"],\"negativePass\":true}\n";
}

/** Actual adaptive-workspace updates, independent interval-sum reference input. */
void ring_balanced_reduction_probe() {
    using namespace Physical::Gravity;
    using namespace finite_ring_detail;
    std::cout<<std::setprecision(17)<<"{\"cases\":[";bool first=true;
    for(std::size_t capacity:{1u,3u,16u,257u,4096u,65536u}) {
        RingBoxReduction reduction(capacity);
        require(reduction.total().integral.lower==0.&&reduction.total().integral.upper==0.,
            "empty balanced ring workspace acquired floor");
        std::size_t depth=0,extent=1;while(extent<capacity){extent*=2;++depth;}
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"capacity\":"<<capacity<<",\"steps\":[";
        for(std::size_t k=0;k<120;++k) {
            const std::size_t index=(k*37)%std::min(capacity,std::size_t(97));
            PositiveInterval input{};
            switch(k%6) {
                case 0:input={0.,0.};break;
                case 1:input={1.,1.};break;
                case 2:input={1.e-300,2.e-300};break;
                case 3:input={1.e300,std::nextafter(1.e300,std::numeric_limits<double>::infinity())};break;
                case 4:input={.1,.100001};break;
                default:input={std::numeric_limits<double>::denorm_min(),2.*std::numeric_limits<double>::denorm_min()};
            }
            reduction.replace(index,input,RingIntervalStatus::Bounded);
            const auto& total=reduction.total();
            require(total.status==RingIntervalStatus::Bounded,"finite balanced sum rejected");
            require(reduction.node_updates()==(k+1)*(depth+1),"balanced workspace exceeded logarithmic update bound");
            if(k)std::cout<<',';
            std::cout<<"{\"index\":"<<index<<",\"lower\":"<<input.lower<<",\"upper\":"<<input.upper
                <<",\"total_lower\":"<<total.integral.lower<<",\"total_upper\":"<<total.integral.upper
                <<",\"worst\":"<<total.worst_index<<",\"updates\":"<<reduction.node_updates()<<'}';
        }
        bool rejected=false;try{reduction.replace(capacity,{},RingIntervalStatus::Bounded);}
        catch(const std::out_of_range&){rejected=true;}
        require(rejected,"ring workspace bypassed capacity");
        std::cout<<"]}";
    }
    RingBoxReduction tied(3);
    tied.replace(2,{1.,2.},RingIntervalStatus::Bounded);
    tied.replace(0,{1.,2.},RingIntervalStatus::Bounded);
    tied.replace(1,{1.,2.},RingIntervalStatus::Bounded);
    require(tied.total().worst_index==0,"ring workspace changed earliest-index tie break");
    for(int i=0;i<3;++i)tied.replace(i,{},RingIntervalStatus::Bounded);
    require(tied.total().integral.lower==0.&&tied.total().integral.upper==0.,"zero workspace acquired floor");
    tied.replace(0,{1.,std::numeric_limits<double>::infinity()},RingIntervalStatus::Bounded);
    require(tied.total().status==RingIntervalStatus::PrecisionLimit,"nonfinite ring interval certified");
    tied.replace(0,{0.,std::numeric_limits<double>::quiet_NaN()},RingIntervalStatus::Bounded);
    require(tied.total().status==RingIntervalStatus::PrecisionLimit,"NaN ring interval certified");
    tied.replace(0,{-1.,1.},RingIntervalStatus::Bounded);
    require(tied.total().status==RingIntervalStatus::PrecisionLimit,"negative ring lower bound certified");
    RingBoxReduction overflow(2);
    overflow.replace(0,{1.e308,1.e308},RingIntervalStatus::Bounded);
    overflow.replace(1,{1.e308,1.e308},RingIntervalStatus::Bounded);
    require(overflow.total().status==RingIntervalStatus::PrecisionLimit,"overflow ring sum certified");
    std::cout<<"]}\n";
}

void finite_ring_contact_gauss3_contract() {
    using namespace Physical::Gravity;
    RingEnclosureControl control{};control.relative_target=1.e-10;
    control.maximum_boxes=16384;
    for(auto point:{std::array<double,2>{1.,0.},std::array<double,2>{1.,.375},
                    std::array<double,2>{.75,0.}}) {
        const auto bound=finite_ring_potential_enclosure(.5,1.,-.375,.375,1.,
            point[0],point[1],arch::constants::gravity::cgs::gravitational_constant,control);
        require(bound.bound_valid&&bound.status==RingIntervalStatus::Bounded
            &&bound.absolute_error<=control.relative_target*std::abs(bound.value),
            "matched contact Gauss3 failed original internal target");
        std::cout<<"RZ_CONTACT_GAUSS3 r="<<point[0]<<" z="<<point[1]
            <<" lower="<<bound.lower<<" upper="<<bound.upper
            <<" error="<<bound.absolute_error<<" boxes="<<bound.leaf_boxes
            <<" range_evaluations="<<bound.range_evaluations
            <<" kernel_enclosures="<<bound.kernel_enclosures
            <<" agm_iterations="<<bound.agm_iterations<<'\n';
    }
    std::cout<<"RZ_CONTACT_GAUSS3_PASS full_RZ_science=not_complete production_values=gated\n";
}

void finite_ring_contact_log_contract() {
    using namespace Physical::Gravity;
    using namespace finite_ring_detail;
    for(auto point:{std::array<double,2>{1.,0.},std::array<double,2>{1.,.001},
                    std::array<double,2>{.9995,0.}}) {
        const auto bound=contact_log_integral_enclosure(.999,1.,-.001,.001,
            point[0],point[1]);
        require(interval_finite(bound)&&bound.lower>0.&&bound.upper>=bound.lower,
            "contact log main-part reliable bound missing");
        const auto box=enclose_box(.999,1.,-.001,.001,point[0],point[1]);
        require(box.status==RingIntervalStatus::Bounded&&box.integral.lower>0.
            &&box.integral.lower>=bound.lower&&box.integral.upper<=bound.upper,
            "contact main-part not consumed by original rectangle owner");
        std::cout<<"RZ_CONTACT_LOG r="<<point[0]<<" z="<<point[1]
            <<" lower="<<bound.lower<<" upper="<<bound.upper<<'\n';
    }
    require(!interval_finite(contact_log_integral_enclosure(
        .5,1.,-.375,.375,1.,2.)),"non-contact accepted as contact integral");
    require(!interval_finite(positive_log_point_enclosure(0.)),
        "log zero invented finite value");
    require(!interval_finite(positive_atan_point_enclosure(-1.)),
        "negative positive-atan accepted");
    RingEnclosureControl control{};control.relative_target=1.e-10;
    control.maximum_boxes=128;
    const auto limited=finite_ring_potential_enclosure(.5,1.,-.375,.375,1.,1.,0.,1.,control);
    require(limited.bound_valid&&limited.status==RingIntervalStatus::WorkLimit,
        "contact logarithmic refinement falsely advertised strict convergence");
    std::cout<<"RZ_CONTACT_LOG_PASS strict_target=not_complete production_values=gated\n";
}

void finite_ring_separated_gauss_contract() {
    using namespace Physical::Gravity;
    RingEnclosureControl control{};control.relative_target=1.e-10;
    control.maximum_boxes=8192;
    for(auto point:{std::array<double,2>{2.,0.},
                    std::array<double,2>{.75,2.},
                    std::array<double,2>{.01,2.},
                    std::array<double,2>{2.,-.7}}) {
        const auto bound=finite_ring_potential_enclosure(.5,1.,-.23,.71,1.,
            point[0],point[1],1.,control);
        require(bound.bound_valid&&bound.status==RingIntervalStatus::Bounded
            &&bound.absolute_error<=control.relative_target*std::abs(bound.value)
            &&bound.kernel_enclosures==28*bound.range_evaluations&&bound.agm_iterations>0,
            "separated Gauss derivative bound failed unchanged internal target");
        const auto reference24=independent_ring_potential(.5,1.,-.23,.71,
            {point[0],0.,point[1]},24);
        const auto reference32=independent_ring_potential(.5,1.,-.23,.71,
            {point[0],0.,point[1]},32);
        require(bound.lower<=reference24&&reference24<=bound.upper
            &&bound.lower<=reference32&&reference32<=bound.upper,
            "independent Newton separated diagnostic escaped derivative enclosure");
        std::cout<<"RZ_SEPARATED_GAUSS r="<<point[0]<<" z="<<point[1]
            <<" lower="<<bound.lower<<" upper="<<bound.upper
            <<" error="<<bound.absolute_error<<" boxes="<<bound.leaf_boxes
            <<" range_evaluations="<<bound.range_evaluations
            <<" kernel_enclosures="<<bound.kernel_enclosures
            <<" agm_iterations="<<bound.agm_iterations<<'\n';
    }
    auto capped=control;capped.maximum_boxes=8;
    const auto limited=finite_ring_potential_enclosure(.5,1.,-.23,.71,1.,2.,0.,1.,capped);
    require(limited.bound_valid&&limited.status==RingIntervalStatus::WorkLimit
        &&limited.leaf_boxes==capped.maximum_boxes,
        "strict target at deliberately low internal resource cap silently accepted");
    auto zero=control;zero.relative_target=0.;zero.maximum_boxes=8;
    const auto failed=finite_ring_potential_enclosure(.5,1.,-.23,.71,1.,2.,0.,1.,zero);
    require(failed.bound_valid&&failed.status==RingIntervalStatus::WorkLimit
        &&failed.absolute_error>0.,"separated source accepted zero request with hidden floor");
    std::cout<<"RZ_SEPARATED_GAUSS_PASS contact_science=not_complete production_values=gated\n";
}

void finite_ring_far_leaf_contract() {
    using namespace Physical::Gravity;
    RingEnclosureControl control{};control.relative_target=1.e-10;
    control.maximum_boxes=1;
    for(double rl:{0.,.5})for(double distance:{1.e3,1.e6,1.e12})
        for(auto direction:{std::array<double,2>{1.,0.},
                            std::array<double,2>{.6,.8},
                            std::array<double,2>{.6,-.8}}) {
        const double ro=distance*direction[0],zo=distance*direction[1];
        const auto bound=finite_ring_potential_enclosure(rl,1.,-.23,.71,1.,
            ro,zo,1.,control);
        require(bound.bound_valid&&bound.status==RingIntervalStatus::Bounded
            &&bound.range_evaluations==0&&bound.upper<0.,
            "far leaf did not pass certified unchanged internal target");
        const auto reference8=independent_ring_potential(rl,1.,-.23,.71,{ro,0.,zo},8);
        const auto reference12=independent_ring_potential(rl,1.,-.23,.71,{ro,0.,zo},12);
        require(bound.lower<=reference8&&reference8<=bound.upper
            &&bound.lower<=reference12&&reference12<=bound.upper,
            "independent full-ring Newton diagnostic escaped far leaf enclosure");
        std::cout<<"RZ_FAR_LEAF rl="<<rl<<" r="<<ro<<" z="<<zo
            <<" lower="<<bound.lower<<" upper="<<bound.upper
            <<" error="<<bound.absolute_error<<" newton8="<<static_cast<double>(reference8)
            <<" newton12="<<static_cast<double>(reference12)<<'\n';
    }
    require(!finite_ring_detail::interval_finite(
        finite_ring_detail::single_leaf_far_potential_enclosure(
            .5,1.,-.375,.375,1.,.75,0.,1.)),
        "observer inside complete support sphere accepted far expansion");
    auto zero=control;zero.relative_target=0.;
    const auto failed=finite_ring_potential_enclosure(.5,1.,-.23,.71,1.,1.e6,0.,1.,zero);
    require(failed.bound_valid&&failed.status!=RingIntervalStatus::Bounded
        &&failed.range_evaluations>0,"zero target silently accepted far roundoff");
    const auto near=finite_ring_potential_enclosure(.5,1.,-.375,.375,1.,1.,0.,1.,control);
    require(near.bound_valid&&near.status==RingIntervalStatus::WorkLimit
        &&near.range_evaluations>0,"contact bypassed original source failure path");
    std::cout<<"RZ_FAR_LEAF_PASS parent_symmetry=not_assumed production_values=gated\n";
}

void finite_ring_axis_enclosure_contract() {
    using namespace Physical::Gravity;
    RingEnclosureControl control{};control.relative_target=1.e-10;
    for(double rl:{0.,.5})for(double z:{0.,.375,2.,100.,-100.,1.e6,-1.e6,1.e12,-1.e12,1.e20,-1.e20}) {
        const auto value=finite_ring_potential_enclosure(rl,1.,-.375,.375,1.,0.,z,1.,control);
        require(value.bound_valid && value.lower<=value.value && value.value<=value.upper
            && value.upper<=0. && value.leaf_boxes==0 && value.range_evaluations==0,
            "axis analytical enclosure missing or used rectangle quadrature");
        const auto estimate=finite_ring_potential_estimate(rl,1.,-.375,.375,1.,0.,z,1.);
        // The estimate is not an independent truth value: its rounding can
        // exceed the tighter certified interval in far cancellation regimes.
        require(value.lower<=estimate.value+estimate.estimated_error
            && estimate.value-estimate.estimated_error<=value.upper,
            "legacy estimate diagnostic band is disjoint from certified interval");
        if(z==0.)require(value.status==RingIntervalStatus::Bounded,
            "well-conditioned axis case failed explicit internal target");
        if(std::abs(z)>=1.e6)require(value.status==RingIntervalStatus::Bounded,
            "far-axis derivative enclosure failed explicit internal target");
        std::cout<<"RZ_AXIS_ENCLOSURE rl="<<rl<<" z="<<z<<" lower="<<value.lower
            <<" upper="<<value.upper<<" error="<<value.absolute_error
            <<" status="<<static_cast<int>(value.status)<<'\n';
    }
    for(double z:{0.,.375,2.,-2.,1.e6,-1.e6,1.e20,-1.e20}) {
        const auto thin=finite_ring_potential_enclosure(1.,std::nextafter(1.,2.),
            -.375,.375,1.,0.,z,1.,control);
        require(thin.bound_valid && thin.status==RingIntervalStatus::Bounded
            && thin.upper<0. && thin.absolute_error>0.,
            "adjacent-double radial source lost its reliable interval");
    }
    require(!finite_ring_detail::interval_finite(
        finite_ring_detail::log1p_enclosure({-1.,0.})),
        "negative log1p interval accepted");
    auto tight=control;tight.relative_target=0.;
    const auto failed=finite_ring_potential_enclosure(.5,1.,-.375,.375,1.,0.,0.,1.,tight);
    require(failed.bound_valid && failed.status==RingIntervalStatus::PrecisionLimit
        && failed.absolute_error>0.,"axis zero target acquired hidden floor");
    const auto huge=finite_ring_potential_enclosure(.5,1.,-.375,.375,1.,0.,
        std::numeric_limits<double>::max(),1.,control);
    require(!huge.bound_valid && huge.status==RingIntervalStatus::PrecisionLimit,
        "unrepresentable axis arithmetic silently accepted");
    std::cout<<"RZ_AXIS_ENCLOSURE_PASS production_values=gated\n";
}

void finite_ring_enclosure_contract() {
    using namespace Physical::Gravity;
    RingEnclosureControl tight{};tight.maximum_boxes=128;
    for(auto point:{std::array<double,2>{2.,0.},
                    std::array<double,2>{.75,2.},
                    std::array<double,2>{1.,0.},
                    std::array<double,2>{1.,.375},
                    std::array<double,2>{.75,0.}}) {
        auto short_budget=tight;short_budget.maximum_boxes=16;
        const auto broad=finite_ring_potential_enclosure(.5,1.,-.375,.375,1.,
            point[0],point[1],1.,short_budget);
        const auto fine=finite_ring_potential_enclosure(.5,1.,-.375,.375,1.,
            point[0],point[1],1.,tight);
        require(broad.bound_valid && fine.bound_valid
            && broad.status==RingIntervalStatus::WorkLimit
            && fine.status==RingIntervalStatus::WorkLimit,
            "zero target produced fake certified convergence");
        require(fine.lower<=fine.value && fine.value<=fine.upper
            && fine.absolute_error<broad.absolute_error,
            "range refinement failed to improve valid enclosure");
        const auto estimate=finite_ring_potential_estimate(.5,1.,-.375,.375,1.,
            point[0],point[1],1.);
        require(fine.lower<=estimate.value && estimate.value<=fine.upper,
            "diagnostic ring estimate outside reliable interval");
        std::cout<<"RZ_RING_ENCLOSURE r="<<point[0]<<" z="<<point[1]
            <<" lower="<<fine.lower<<" upper="<<fine.upper
            <<" error="<<fine.absolute_error<<" broad_error="<<broad.absolute_error
            <<" boxes="<<fine.leaf_boxes<<" range_evaluations="<<fine.range_evaluations
            <<" target_status=WorkLimit\n";
    }
    const auto axis=finite_ring_potential_enclosure(.5,1.,-.375,.375,1.,0.,0.,1.,tight);
    require(axis.bound_valid && axis.status==RingIntervalStatus::PrecisionLimit
        && axis.leaf_boxes==0 && axis.range_evaluations==0,
        "axis analytic zero-target precision failure bypassed");
    auto loose=tight;loose.absolute_target=10.;
    const auto accepted=finite_ring_potential_enclosure(.5,1.,-.375,.375,1.,1.,0.,1.,loose);
    require(accepted.bound_valid && accepted.status==RingIntervalStatus::Bounded
        && accepted.absolute_error<=loose.absolute_target,"explicit loose algorithm target failed");
    // Reuse only an original successful signed interval; no geometry/source grant is implied.
    RingPotentialEnclosure reused;
    require(reuse_bounded_ring_interval({accepted.lower,accepted.upper},1.,1.,
        accepted.leaf_boxes,loose,reused)&&reused.lower==accepted.lower
        &&reused.upper==accepted.upper&&reused.value==accepted.value
        &&reused.absolute_error==accepted.absolute_error&&reused.range_evaluations==0
        &&reused.kernel_enclosures==0&&reused.agm_iterations==0,
        "same-density interval reuse changed original endpoint or work arithmetic");
    for(double rho:{.5,2.}) {
        auto scaled=loose;scaled.absolute_target=rho*loose.absolute_target;
        require(reuse_bounded_ring_interval({accepted.lower,accepted.upper},1.,rho,
            accepted.leaf_boxes,scaled,reused)&&reused.lower<=rho*accepted.lower
            &&reused.upper>=rho*accepted.upper&&reused.absolute_error<=scaled.absolute_target,
            "positive density interval scaling lost original linear enclosure");
    }
    RingPotentialEnclosure sentinel;sentinel.value=123.;
    auto too_few=loose;too_few.maximum_boxes=1;
    require(!reuse_bounded_ring_interval({accepted.lower,accepted.upper},1.,1.,2,
        too_few,sentinel)&&sentinel.value==123.,
        "interval history bypassed current box cost or published a failed reuse");
    require(!reuse_bounded_ring_interval({accepted.lower,accepted.upper},1.,1.,
        accepted.leaf_boxes,tight,sentinel)&&sentinel.value==123.,
        "interval history bypassed the original zero-target precision limit");
    for(double bad:{0.,-1.,std::numeric_limits<double>::denorm_min(),
                    std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::quiet_NaN()}) {
        require(!reuse_bounded_ring_interval({accepted.lower,accepted.upper},1.,bad,
            accepted.leaf_boxes,loose,sentinel)
            &&!reuse_bounded_ring_interval({accepted.lower,accepted.upper},bad,1.,
                accepted.leaf_boxes,loose,sentinel)&&sentinel.value==123.,
            "invalid or subnormal density entered memo scaling instead of the original kernel");
    }
    // Exact binary powers: both rho inputs are normal, but 2^(-40-1000)
    // and 2^(-44-1000) are subnormal. Original old-density integral endpoints
    // times those ratios are normal finite outputs; only the ratio scope,
    // rather than a final-product overflow/underflow, must force fallback.
    const double large_density=std::scalbn(1.,1000);
    auto large_control=loose;large_control.absolute_target=std::scalbn(loose.absolute_target,1000);
    const auto large_original=finite_ring_potential_enclosure(.5,1.,-.375,.375,
        large_density,1.,0.,1.,large_control);
    require(large_original.bound_valid&&large_original.status==RingIntervalStatus::Bounded,
        "exact-power memo refusal fixture lacks an original bounded integral");
    for(int exponent:{-40,-44}) {
        const double small_density=std::scalbn(1.,exponent);
        const double exact_ratio=std::scalbn(1.,exponent-1000);
        const auto ratio=finite_ring_detail::interval_quotient_positive(
            {small_density,small_density},{large_density,large_density});
        require(std::isnormal(large_density)&&std::isnormal(small_density)
            &&exact_ratio>0.&&!std::isnormal(exact_ratio)
            &&finite_ring_detail::interval_finite(ratio)&&ratio.lower>0.
            &&!std::isnormal(ratio.lower)&&!std::isnormal(ratio.upper)
            &&std::isnormal(large_original.lower*exact_ratio)
            &&std::isnormal(large_original.upper*exact_ratio),
            "exact-power source did not isolate a positive subnormal ratio with normal outputs");
        auto small_control=loose;
        small_control.absolute_target=std::scalbn(loose.absolute_target,exponent);
        require(!reuse_bounded_ring_interval({large_original.lower,large_original.upper},
            large_density,small_density,large_original.leaf_boxes,small_control,sentinel)
            &&sentinel.value==123.,
            "subnormal outward ratio was accepted or changed failed output before original fallback");
    }
    require(!reuse_bounded_ring_interval({accepted.lower,accepted.upper},
        std::numeric_limits<double>::min(),std::numeric_limits<double>::max(),
        accepted.leaf_boxes,loose,sentinel)&&sentinel.value==123.,
        "overflowing density ratio was silently memoized");
    const auto zero=finite_ring_potential_enclosure(.5,1.,-.375,.375,0.,1.,0.,1.,tight);
    require(zero.bound_valid && zero.status==RingIntervalStatus::Bounded
        && zero.lower==0. && zero.upper==0. && zero.absolute_error==0.,
        "zero density enclosure not exact");
    auto invalid=tight;invalid.maximum_boxes=0;
    require(!finite_ring_potential_enclosure(.5,1.,-.375,.375,1.,1.,0.,1.,invalid).bound_valid,
        "zero work budget accepted");
    require(!finite_ring_potential_enclosure(.5,1.,-.375,.375,-1.,1.,0.,1.,tight).bound_valid,
        "negative density accepted");
    const auto overflow=finite_ring_potential_enclosure(.5,1.,-.375,.375,1.,
        1.,0.,std::numeric_limits<double>::max(),tight);
    require(!overflow.bound_valid && overflow.status==RingIntervalStatus::PrecisionLimit,
        "overflowed potential bound silently accepted");
    const auto unsplittable=finite_ring_potential_enclosure(1.,std::nextafter(1.,2.),
        0.,std::numeric_limits<double>::epsilon(),1.,2.,2.,1.,tight);
    require(unsplittable.status==RingIntervalStatus::PrecisionLimit,
        "nonrepresentable midpoint bypassed subdivision failure");
    for(double x:{1.,2.,4.,1.e100,std::numeric_limits<double>::max()})
        require(finite_ring_detail::logarithm_upper(x)>=std::log(x),
            "positive log series failed to enclose existing log");
    std::cout<<"RZ_RING_ENCLOSURE_PASS production_values=gated tight_budget_not_converged\n";
}

void finite_ring_agm_interval_contract() {
    using namespace Physical::Gravity;
    for(double root:{1.,.5,.01,1.e-12,1.e-100,1.e-300,
                     std::numeric_limits<double>::denorm_min()}) {
        const auto interval=ring_elliptic_k_interval(root);
        require(interval.status==RingIntervalStatus::Bounded
            && interval.lower>0. && std::isfinite(interval.upper)
            && interval.lower<=interval.upper,"AGM enclosure failed");
        const auto estimate=ring_elliptic_k_complementary_root(root);
        if(estimate.status==RingPotentialStatus::EstimatedConverged)
            require(interval.lower<=estimate.value && estimate.value<=interval.upper,
                "existing AGM estimate escaped enclosure");
        std::cout<<"RZ_RING_K_INTERVAL root="<<root<<" lower="<<interval.lower
            <<" upper="<<interval.upper<<" iterations="<<interval.iterations<<'\n';
    }
    require(ring_elliptic_k_interval(0.).status==RingIntervalStatus::SingularSample,
        "zero complementary root softened");
    for(double bad:{-1.,2.,std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::quiet_NaN()})
        require(ring_elliptic_k_interval(bad).status==RingIntervalStatus::InvalidInput,
            "invalid complementary root accepted");
    std::cout<<"RZ_RING_K_INTERVAL_PASS integral_certified=false production_values=gated\n";
}

void finite_ring_kernel_contract() {
    using namespace Physical::Gravity;
    const auto potential=[](double r,double z,RingQuadratureControl control={}) {
        return finite_ring_potential_estimate(.5,1.,-.375,.375,1.,r,z,1.,control);
    };
    for(double root:{1.,.5,1.e-12,1.e-100,1.e-300}) {
        const auto value=ring_elliptic_k_complementary_root(root);
        require(value.status==RingPotentialStatus::EstimatedConverged
            && std::isfinite(value.value),"ring complementary-root AGM failed");
        if(root==1.)require(value.value==pi/2.,"ring exact K(0) axis limit");
        if(root<=1.e-12)
            require(std::abs(value.value-std::log(4./root))<1.e-12*value.value,
                "ring small complementary root asymptotic failed");
        std::cout<<"RZ_RING_AGM root="<<root<<" value="<<value.value
            <<" iterations="<<value.iterations<<'\n';
    }
    require(ring_elliptic_k_complementary_root(0.).status==RingPotentialStatus::SingularSample,
        "ring exact contact kernel softened");
    for(double z:{0.,.375,2.,100.}) {
        const auto value=potential(0.,z);
        require(value.status==RingPotentialStatus::AnalyticAxis && !value.error_is_certified,
            "ring analytic axis not explicit or incorrectly certified");
        // Independent long-double axis primitive, not the production stabilization.
        const auto primitive=[](long double radius,long double u) {
            return .5L*(u*std::sqrt(radius*radius+u*u)
                +radius*radius*std::asinh(u/radius));
        };
        const long double lower=-.375L-z,upper=.375L-z;
        const long double reference=-2*3.141592653589793238462643383279502884L*
            (primitive(1.L,upper)-primitive(1.L,lower)
            -primitive(.5L,upper)+primitive(.5L,lower));
        require(std::abs(value.value-reference)<2.e-10L*std::abs(reference),
            "ring analytic axis independent primitive mismatch");
        std::cout<<"RZ_RING_AXIS z="<<z<<" value="<<value.value
            <<" reference_error="<<static_cast<double>(std::abs(value.value-reference))
            <<" roundoff_estimate="<<value.estimated_error<<'\n';
    }
    for(auto point:{std::array<double,3>{2.,0.,0.},
                    std::array<double,3>{.75,0.,2.},
                    std::array<double,3>{1.e-12,0.,2.}}) {
        const auto value=potential(point[0],point[2]);
        const auto reference=independent_ring_potential(.5,1.,-.375,.375,point,32);
        require(value.status==RingPotentialStatus::EstimatedConverged && !value.error_is_certified,
            "ring separated finite-volume estimate failed");
        require(std::abs(value.value-reference)<1.e-9L*std::abs(reference),
            "ring off-axis kernel independent 3D source mismatch");
        std::cout<<"RZ_RING_KERNEL r="<<point[0]<<" z="<<point[2]<<" value="<<value.value
            <<" reference_error="<<static_cast<double>(std::abs(value.value-reference))
            <<" estimate="<<value.estimated_error<<" order="<<value.last_order
            <<" evaluations="<<value.kernel_evaluations<<'\n';
    }
    for(auto point:{std::array<double,2>{1.,0.},
                    std::array<double,2>{1.,.375},
                    std::array<double,2>{.75,0.}}) {
        RingQuadratureControl control{};control.relative_estimate_target=1.e-7;
        const auto value=potential(point[0],point[1],control);
        require((value.status==RingPotentialStatus::EstimatedConverged
                 || value.status==RingPotentialStatus::WorkLimit)
            && std::isfinite(value.value) && !value.error_is_certified
            && value.kernel_evaluations<=control.maximum_kernel_evaluations,
            "ring Duffy contact failed or was certified");
        const auto mirrored=potential(point[0],-point[1],control);
        require(std::abs(value.value-mirrored.value)<2.e-12*std::abs(value.value),
            "ring contact reflection symmetry failed");
        std::cout<<"RZ_RING_CONTACT r="<<point[0]<<" z="<<point[1]<<" value="<<value.value
            <<" estimate="<<value.estimated_error<<" status="<<static_cast<int>(value.status)
            <<" order="<<value.last_order<<" evaluations="<<value.kernel_evaluations<<'\n';
    }
    const auto default_contact=potential(.75,0.);
    require(default_contact.status==RingPotentialStatus::WorkLimit
        && std::isfinite(default_contact.value) && !default_contact.error_is_certified,
        "ring default unresolved contact silently converged");
    std::cout<<"RZ_RING_DEFAULT_CONTACT status=WorkLimit order="<<default_contact.last_order
        <<" evaluations="<<default_contact.kernel_evaluations
        <<" estimate="<<default_contact.estimated_error<<'\n';
    auto limited=RingQuadratureControl{};limited.maximum_kernel_evaluations=1;
    const auto short_work=potential(.75,0.,limited);
    require(short_work.status==RingPotentialStatus::WorkLimit
        && short_work.kernel_evaluations==1 && !short_work.error_is_certified,
        "ring work limit silently converged");
    auto exact=RingQuadratureControl{};exact.relative_estimate_target=exact.absolute_estimate_target=0.;
    require(potential(0.,2.,exact).status==RingPotentialStatus::PrecisionLimit,
        "axis zero tolerance gained hidden floor");
    require(finite_ring_potential_estimate(.5,1.,-.375,.375,-1.,1.,0.,1.).status
        ==RingPotentialStatus::InvalidInput,"ring negative source accepted");
    std::cout<<"RZ_RING_KERNEL_CONTACT_PASS precision=estimate_only production=gated\n";
}

void rz_boundary_guard() {
    auto rz=base_mesh(2,4);
    rz.geometry=elliptic::Geometry::Cylindrical;
    rz.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
    rz.origin={0.,-.5,0.};
    const elliptic::CompositePoisson rz_op(rz,make_cells(rz,false),
        elliptic::BoundaryKind::CurvilinearIsolated);
    auto rejected=[](auto call) {
        bool refused=false;
        try {call();}
        catch(const std::invalid_argument& error) {
            refused=std::string(error.what()).find("finite-ring contract pending")!=std::string::npos;
        }
        require(refused,"RZ reached legacy isolated boundary without explicit contract error");
    };
    Physical::Gravity::GravityBoundary ring(rz_op);
    ring.update(std::vector<double>(rz_op.size(),1.));
    rejected([&] {(void)ring.values(rz_op,constants::gravity::cgs::gravitational_constant);});
    auto polar=rz;polar.semantics=GridMetrics::GeometrySemantics::Existing;
    polar.origin[1]=0.;polar.spacing[1]=2.*pi/polar.cells[1];
    const elliptic::CompositePoisson polar_op(polar,make_cells(polar,false),
        elliptic::BoundaryKind::CurvilinearIsolated);
    Physical::Gravity::GravityBoundary legacy(polar_op);
    legacy.update(std::vector<double>(polar_op.size(),1.));
    rejected([&] {(void)ring.values(polar_op,constants::gravity::cgs::gravitational_constant);});
    const auto values=legacy.values(polar_op,constants::gravity::cgs::gravitational_constant);
    require(values.size()==polar_op.faces().size(),"Legacy boundary lost output shape");
    for(double value:values)require(std::isfinite(value),"Legacy boundary no longer finite");
    rejected([&] {(void)legacy.values(rz_op,constants::gravity::cgs::gravitational_constant);});
    std::cout<<"RZ_BOUNDARY_GUARD_PASS ring_cache=ready ring_values=refused cached_legacy=refused legacy=preserved\n";
}

/** Independent point monomial, evaluated without the production fit basis.
 * The binary-root fixtures have exact dyadic centers, so degree <=3 products
 * are exactly representable in the tested range before the double conversion.
 */
long double canonical_rz_point_monomial(const std::array<double,3>& point,
    int radial_power,int axial_power) {
    long double value=1.;
    for(int i=0;i<radial_power;++i)value*=static_cast<long double>(point[0]);
    for(int i=0;i<axial_power;++i)value*=static_cast<long double>(point[1]);
    return value;
}

/** Independent analytic normal derivative p_r*r^(p_r-1)*z^p_z (or axial).
 * This differentiates the input monomial; it never reads a fitted coefficient.
 */
long double canonical_rz_point_derivative(const std::array<double,3>& point,
    int radial_power,int axial_power,int axis) {
    const int exponent=axis==0?radial_power:axial_power;
    if(exponent==0)return 0.;
    return exponent*canonical_rz_point_monomial(point,
        radial_power-(axis==0?1:0),axial_power-(axis==1?1:0));
}

/** Compare final-owner cached proofs with the original derived-MG routines.
 * The hierarchy's real first coarse operator is constructed with a fine owner
 * and therefore remains uncached. A public final operator with its exact base,
 * ordered cells and named boundary uses the eager cache. Compare every public
 * payload field exactly; the existing monomial/measure tests remain independent
 * scientific oracles. Returned-value mutation and copy/move checks protect owned
 * cache lifetime without a public test-only cache switch or repeated formula.
 */
void native_rz_geometry_cache_contract() {
    const auto same_measure=[](const elliptic::NativeRzMeasureEnclosure& a,
                               const elliptic::NativeRzMeasureEnclosure& b) {
        return a.status==b.status&&a.volume_lower==b.volume_lower
            &&a.volume_upper==b.volume_upper&&a.volume_error_upper==b.volume_error_upper
            &&a.weight_lower==b.weight_lower&&a.weight_upper==b.weight_upper
            &&a.weight_error_upper==b.weight_error_upper
            &&a.total_volume_lower==b.total_volume_lower&&a.total_volume_upper==b.total_volume_upper;
    };
    const auto same_stencil=[](const elliptic::NativeRzStencilEnclosure& a,
                               const elliptic::NativeRzStencilEnclosure& b) {
        return a.status==b.status&&a.construction==b.construction&&a.face_index==b.face_index
            &&a.coefficient_lower==b.coefficient_lower&&a.coefficient_upper==b.coefficient_upper
            &&a.coefficient_error_upper==b.coefficient_error_upper
            &&a.boundary_lower==b.boundary_lower&&a.boundary_upper==b.boundary_upper
            &&a.boundary_error_upper==b.boundary_error_upper
            &&a.inverse_residual_upper==b.inverse_residual_upper
            &&a.inverse_norm_upper==b.inverse_norm_upper&&a.lambda_error_upper==b.lambda_error_upper;
    };
    const auto same_face=[](const elliptic::NativeRzFaceEnclosure& a,
                            const elliptic::NativeRzFaceEnclosure& b) {
        return a.status==b.status&&a.face_index==b.face_index&&a.center_lower==b.center_lower
            &&a.center_upper==b.center_upper&&a.center_error_upper==b.center_error_upper
            &&a.area_lower==b.area_lower&&a.area_upper==b.area_upper
            &&a.area_error_upper==b.area_error_upper
            &&a.area_over_volume_lower==b.area_over_volume_lower
            &&a.area_over_volume_upper==b.area_over_volume_upper
            &&a.area_over_volume_error_upper==b.area_over_volume_error_upper
            &&a.boundary_map_lower==b.boundary_map_lower&&a.boundary_map_upper==b.boundary_map_upper
            &&a.boundary_map_error_upper==b.boundary_map_error_upper;
    };
    for(double inner:{0.,1.}) {
        auto mesh=base_mesh(2,8);mesh.geometry=elliptic::Geometry::Cylindrical;
        mesh.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
        mesh.native_canonical_domain=true;mesh.origin={inner,-.5,0.};
        mesh.root_upper={inner+1.,.5,0.};
        const multigrid::CompositeMultigrid hierarchy(mesh,make_cells(mesh,true),
            elliptic::BoundaryKind::CurvilinearIsolated);
        require(hierarchy.level_count()>1,"cache witness lacks a real derived operator");
        const auto& original=hierarchy.level_operator(1);
        const elliptic::CompositePoisson cached(original.base(),original.cells(),original.boundary_kind());
        require(cached.cells()==original.cells()&&cached.faces().size()==original.faces().size(),
            "cached and uncached geometry ownership differs");
        require(same_measure(cached.native_rz_measure_enclosure(),original.native_rz_measure_enclosure()),
            "cached native measure changed an original payload field");
        for(std::size_t face=0;face<original.faces().size();++face) {
            require(same_stencil(cached.native_rz_stencil_enclosure(face),original.native_rz_stencil_enclosure(face)),
                "cached native stencil changed an original payload field");
            require(same_face(cached.native_rz_face_enclosure(face),original.native_rz_face_enclosure(face)),
                "cached native map changed an original payload field");
        }
        const auto out_of_range=original.faces().size();
        require(same_stencil(cached.native_rz_stencil_enclosure(out_of_range),
                             original.native_rz_stencil_enclosure(out_of_range))
            &&same_face(cached.native_rz_face_enclosure(out_of_range),
                         original.native_rz_face_enclosure(out_of_range)),
            "cache bypassed original invalid-face payload");
        auto returned_measure=cached.native_rz_measure_enclosure();
        auto returned_stencil=cached.native_rz_stencil_enclosure(0);
        auto returned_face=cached.native_rz_face_enclosure(0);
        require(!returned_measure.volume_lower.empty()&&!returned_stencil.coefficient_lower.empty(),
            "cache isolation witness lacks actual measure/stencil entries");
        returned_measure.volume_lower[0]=-1.;returned_stencil.coefficient_lower[0]=-1.;
        returned_face.area_lower=-1.;
        require(same_measure(cached.native_rz_measure_enclosure(),original.native_rz_measure_enclosure())
            &&same_stencil(cached.native_rz_stencil_enclosure(0),original.native_rz_stencil_enclosure(0))
            &&same_face(cached.native_rz_face_enclosure(0),original.native_rz_face_enclosure(0)),
            "by-value certificate exposed mutable cached storage");
        auto copied=cached;auto moved=std::move(copied);
        require(same_measure(moved.native_rz_measure_enclosure(),original.native_rz_measure_enclosure())
            &&same_stencil(moved.native_rz_stencil_enclosure(0),original.native_rz_stencil_enclosure(0))
            &&same_face(moved.native_rz_face_enclosure(0),original.native_rz_face_enclosure(0)),
            "operator copy/move detached immutable cached geometry");
    }
}

/** Genuine canonical Native boundary-cubic and matching stencil-proof gate.
 * Workflow:
 * 1. Build actual axis/annulus and uniform/mixed dyadic leaves from configured
 *    root geometry; retain the original unbound Native owner as role control.
 * 2. Check physical boundary construction and the unchanged internal/CF rows.
 * 3. Compare actual point gradients with ten independent analytic monomials;
 *    require the public ideal coefficient enclosure to contain their gradients.
 * This tests point-stencil arithmetic/certificates, not continuous self-gravity
 * or a source/field acceptance. The original 1e-10 polynomial budget is reused.
 */
void native_rz_canonical_boundary_stencil_contract() {
    native_rz_geometry_cache_contract();
    constexpr double original_polynomial_budget=1e-10;
    constexpr std::array<std::array<int,2>,10> powers{{
        {0,0},{1,0},{0,1},{2,0},{1,1},{0,2},{3,0},{2,1},{1,2},{0,3}}};
    for(double inner:{0.,1.})for(bool mixed:{false,true}) {
        // N4 explicitly exercises the minimum cubic root support. N8 gives
        // genuine coarse/fine internal fragments without a synthetic stencil.
        const int n=mixed?8:4;
        auto mesh=base_mesh(2,n);mesh.geometry=elliptic::Geometry::Cylindrical;
        mesh.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
        mesh.origin={inner,-.5,0.};mesh.root_upper={inner+1.,.5,0.};
        mesh.native_canonical_domain=true;
        require(mesh.cells[0]>=4&&mesh.cells[1]>=4,"cubic fixture lost genuine root support");
        const auto leaves=make_cells(mesh,mixed);
        const elliptic::CompositePoisson op(mesh,leaves,
            elliptic::BoundaryKind::CurvilinearIsolated);
        auto unbound=mesh;unbound.native_canonical_domain=false;
        const elliptic::CompositePoisson original(unbound,leaves,
            elliptic::BoundaryKind::CurvilinearIsolated);
        require(op.faces().size()==original.faces().size()
            &&op.cells()==original.cells(),"canonical policy changed actual leaf/face ownership");
        std::vector<elliptic::NativeRzStencilEnclosure> proofs;
        proofs.reserve(op.faces().size());
        std::array<int,4> physical_sides{};
        int internal=0,interfaces=0;
        for(std::size_t f=0;f<op.faces().size();++f) {
            const auto& face=op.faces()[f];const auto& old=original.faces()[f];
            require(face.left==old.left&&face.right==old.right&&face.axis==old.axis
                &&face.boundary_side==old.boundary_side&&face.center==old.center,
                "canonical fit changed point/fragment identity on binary fixture");
            require(!(inner==0.&&face.axis==0&&face.center[0]==0.),
                "canonical axis fixture emitted a nonzero-area origin face");
            if(face.boundary_side>=0) {
                ++physical_sides[face.boundary_side];
                require(face.construction==elliptic::FaceStencilConstruction::PolynomialFit,
                    "canonical physical boundary recovered instead of exercising cubic fit");
                require(face.samples.size()>=10,"physical boundary lost actual cubic support");
            } else {
                ++internal;
                const bool cf=op.cells()[face.left].level!=op.cells()[face.right].level;
                if(cf)++interfaces;
                // On exact binary geometry, unchanged quadratic/two-point roles
                // must match actual old rows, not merely fit the same constant.
                require(face.construction==old.construction&&face.samples==old.samples
                    &&face.coefficients==old.coefficients
                    &&face.boundary_coefficient==old.boundary_coefficient
                    &&face.anchor_coefficient==old.anchor_coefficient
                    &&face.value_samples==old.value_samples
                    &&face.value_coefficients==old.value_coefficients
                    &&face.value_boundary_coefficient==old.value_boundary_coefficient,
                    "canonical boundary upgrade changed internal/CF six-term policy");
                require(cf?face.construction==elliptic::FaceStencilConstruction::PolynomialFit
                    :face.construction==elliptic::FaceStencilConstruction::TwoPoint,
                    "canonical internal/CF role was silently replaced");
            }
            auto proof=op.native_rz_stencil_enclosure(f);
            // An unresolved inverse is an actual failed gate, not a skipped
            // polynomial or permission to use the old six-term certificate.
            require(proof.status==elliptic::BoundaryErrorStatus::Bounded,
                "canonical native stencil has no actual bounded coefficient certificate");
            require(proof.face_index==f&&proof.construction==face.construction
                &&proof.coefficient_lower.size()==face.samples.size()
                &&proof.coefficient_upper.size()==face.samples.size()
                &&proof.coefficient_error_upper.size()==face.samples.size(),
                "canonical coefficient proof detached from actual final row");
            if(face.construction==elliptic::FaceStencilConstruction::PolynomialFit)
                require(std::isfinite(proof.inverse_residual_upper)
                    &&proof.inverse_residual_upper>=0.&&proof.inverse_residual_upper<1.
                    &&std::isfinite(proof.inverse_norm_upper)&&proof.inverse_norm_upper>0.
                    &&std::isfinite(proof.lambda_error_upper)&&proof.lambda_error_upper>=0.,
                    "canonical fitted row bypassed the existing Neumann inverse proof");
            require(std::isfinite(proof.boundary_lower)&&std::isfinite(proof.boundary_upper)
                &&proof.boundary_lower<=proof.boundary_upper
                &&std::isfinite(proof.boundary_error_upper)&&proof.boundary_error_upper>=0.,
                "canonical boundary coefficient enclosure is malformed");
            for(std::size_t s=0;s<face.samples.size();++s)
                require(std::isfinite(proof.coefficient_lower[s])
                    &&std::isfinite(proof.coefficient_upper[s])
                    &&proof.coefficient_lower[s]<=proof.coefficient_upper[s]
                    &&std::isfinite(proof.coefficient_error_upper[s])
                    &&proof.coefficient_error_upper[s]>=0.,
                    "canonical fitted coefficient enclosure is malformed");
            proofs.push_back(std::move(proof));
        }
        require(internal>0&&physical_sides[1]>0&&physical_sides[2]>0&&physical_sides[3]>0,
            "canonical fixture lacks required real face roles");
        require(inner==0.?physical_sides[0]==0:physical_sides[0]>0,
            "canonical annulus/axis physical-side coverage is wrong");
        require(!mixed||interfaces>0,"canonical mixed fixture lacks genuine CF rows");
        double largest_error=0.;
        std::vector<double> phi(op.size());
        for(std::size_t monomial=0;monomial<powers.size();++monomial) {
            const int rp=powers[monomial][0],zp=powers[monomial][1];
            for(int cell=0;cell<op.size();++cell)
                phi[cell]=static_cast<double>(canonical_rz_point_monomial(op.center(cell),rp,zp));
            for(std::size_t f=0;f<op.faces().size();++f) {
                const auto& face=op.faces()[f];
                if(face.boundary_side<0&&monomial>=6)continue; // unchanged internal quadratic role
                const auto& proof=proofs[f];
                const long double target=canonical_rz_point_derivative(face.center,rp,zp,face.axis);
                const long double face_value=face.boundary_side>=0
                    ?canonical_rz_point_monomial(face.center,rp,zp):0.;
                const double actual=op.face_gradient(phi,face,static_cast<double>(face_value));
                const double error=static_cast<double>(std::abs(static_cast<long double>(actual)-target));
                require(std::isfinite(actual)&&error<original_polynomial_budget,
                    "canonical point-gradient monomial reproduction failed original budget");
                largest_error=std::max(largest_error,error);
                // Independent interval contraction of the PUBLIC ideal row.
                // The operands are exact dyadic monomial differences here;
                // no production polynomial/Gram reconstruction acts as oracle.
                const int anchor=face.left>=0?face.left:face.right;
                const long double anchor_value=phi[anchor];
                long double lower=0.,upper=0.;
                for(std::size_t s=0;s<face.samples.size();++s) {
                    const long double delta=static_cast<long double>(phi[face.samples[s]])-anchor_value;
                    const long double a=proof.coefficient_lower[s]*delta;
                    const long double b=proof.coefficient_upper[s]*delta;
                    lower+=std::min(a,b);upper+=std::max(a,b);
                }
                if(face.boundary_side>=0) {
                    const long double delta=face_value-anchor_value;
                    const long double a=proof.boundary_lower*delta,b=proof.boundary_upper*delta;
                    lower+=std::min(a,b);upper+=std::max(a,b);
                }
                require(target>=lower&&target<=upper,
                    "public ideal coefficient proof does not enclose selected analytic point gradient");
            }
        }
        require(op.native_rz_stencil_enclosure(op.faces().size()).status
            ==elliptic::BoundaryErrorStatus::InvalidInput,
            "canonical coefficient proof accepted a nonexistent final face");
        // Independent exact constant field: the ideal and actual anchored
        // gradient of phi=datum=M is zero, even when the final boundary-fit
        // coefficient has a nonzero construction interval. Keeping its SAME
        // A/B defect correlated must therefore give an exact zero bound.
        // A homogeneous construction still has its original nonzero bound.
        const double constant=std::ldexp(1.,30);
        const std::vector<double> constant_phi(op.size(),constant),
            constant_data(op.faces().size(),constant),zero_data(op.faces().size(),0.);
        const auto joint=op.native_rz_prescribed_residual_construction_error(constant_phi,constant_data);
        const auto separate_a=op.native_rz_operator_construction_error(constant_phi);
        const auto separate_b=op.native_rz_boundary_construction_error(constant_data);
        require(joint.status==elliptic::BoundaryErrorStatus::Bounded&&joint.native_norm_upper==0.
            &&separate_a.status==elliptic::BoundaryErrorStatus::Bounded&&separate_a.native_norm_upper>0.
            &&separate_b.status==elliptic::BoundaryErrorStatus::Bounded&&separate_b.native_norm_upper>0.,
            "same prescribed coefficient correlation lost exact constant-gradient identity");
        const auto homogeneous=op.native_rz_prescribed_residual_construction_error(constant_phi,zero_data);
        require(homogeneous.status==separate_a.status&&homogeneous.cell_bounds==separate_a.cell_bounds
            &&homogeneous.native_norm_upper==separate_a.native_norm_upper,
            "joint prescribed proof changed the original zero-datum homogeneous proof");
        require(op.native_rz_prescribed_residual_construction_error(constant_phi,{}).status
            ==elliptic::BoundaryErrorStatus::InvalidInput,"missing actual prescribed data was certified");
        auto invalid_data=constant_data;invalid_data[0]=std::numeric_limits<double>::quiet_NaN();
        require(op.native_rz_prescribed_residual_construction_error(constant_phi,invalid_data).status
            ==elliptic::BoundaryErrorStatus::InvalidInput,"nonfinite prescribed data was certified");

        // The RHS lower bound retains its original separate error E_b even
        // when the complete residual error E_c is different. These exact
        // mathematical ledger inputs are API tests, never physical grants.
        std::vector<double> one(op.size(),1.),zero(op.size(),0.);
        elliptic::BoundaryRhsError rhs_error;rhs_error.status=elliptic::BoundaryErrorStatus::Bounded;
        rhs_error.cell_bounds.assign(op.size(),.6);rhs_error.norm_upper=op.native_rz_norm_interval(rhs_error.cell_bounds).upper;
        elliptic::NativeRzCompleteResidualError complete;complete.status=elliptic::BoundaryErrorStatus::Bounded;
        complete.cell_bounds.assign(op.size(),.1);complete.native_norm_upper=op.native_rz_norm_interval(complete.cell_bounds).upper;
        const auto original_safe=op.assess_boundary_residual(one,zero,rhs_error,0.,0.,
            elliptic::BoundaryErrorQuality::CertifiedAbsolute,.5,0.,elliptic::BoundaryResidualNormScope::RootDyadicRzWeights);
        const auto correlated=op.assess_native_rz_correlated_residual(one,zero,rhs_error,complete,.5,0.);
        require(original_safe.status==elliptic::BoundaryResidualStatus::ResidualTooLarge
            &&correlated.status==elliptic::BoundaryResidualStatus::Accepted
            &&correlated.error_composition==elliptic::ResidualErrorComposition::CorrelatedPrescribedBoundary
            &&correlated.tolerance_safe==original_safe.tolerance_safe
            &&correlated.rhs_error_upper==original_safe.rhs_error_upper
            &&correlated.complete_residual_error_upper==op.native_rz_norm_interval(complete.cell_bounds).upper,
            "correlated residual proof changed original tolerance/RHS lower bound or lost complete ledger");
        auto excessive=complete;excessive.cell_bounds.assign(op.size(),.3);
        excessive.native_norm_upper=op.native_rz_norm_interval(excessive.cell_bounds).upper;
        require(op.assess_native_rz_correlated_residual(one,zero,rhs_error,excessive,.5,0.).status
            ==elliptic::BoundaryResidualStatus::ResidualTooLarge,"complete residual error was omitted");
        auto missing=complete;missing.cell_bounds.clear();
        require(op.assess_native_rz_correlated_residual(one,zero,rhs_error,missing,.5,0.).status
            ==elliptic::BoundaryResidualStatus::InvalidInput,"missing complete error ledger was certified");
        missing=complete;missing.cell_bounds[0]=-1.;
        require(op.assess_native_rz_correlated_residual(one,zero,rhs_error,missing,.5,0.).status
            ==elliptic::BoundaryResidualStatus::InvalidInput,"negative complete error ledger was certified");
        missing=complete;missing.status=elliptic::BoundaryErrorStatus::UncertifiedInput;
        require(op.assess_native_rz_correlated_residual(one,zero,rhs_error,missing,.5,0.).status
            ==elliptic::BoundaryResidualStatus::UncertifiedInput,"uncertified complete error ledger was certified");
        missing=complete;missing.status=elliptic::BoundaryErrorStatus::Overflow;
        missing.native_norm_upper=std::numeric_limits<double>::infinity();
        require(op.assess_native_rz_correlated_residual(one,zero,rhs_error,missing,.5,0.).status
            ==elliptic::BoundaryResidualStatus::Overflow,"overflow complete error lost its actual failure class");
        std::cout<<"RZ_CANONICAL_BOUNDARY_STENCIL_PASS inner="<<inner<<" mixed="<<mixed
            <<" root="<<n<<" boundary_terms=10 internal_terms=6 point_error="<<largest_error
            <<" interfaces="<<interfaces<<" certificate=actual_public_row\n";
    }
}

/** Frozen RZ polynomial, point-valued potential and analytic face derivatives. */
void rz_manufactured() {
    constexpr double a=.75,b=1.25;
    for(double inner:{0.,.5}) for(bool mixed:{false,true}) {
        auto base=base_mesh(2,8);base.geometry=elliptic::Geometry::Cylindrical;
        base.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
        base.origin={inner,-.5,0.};
        const auto exact=[](const std::array<double,3>& x) {return a*x[0]*x[0]+b*x[1]*x[1];};
        multigrid::CompositeMultigrid solver(base,make_cells(base,mixed),
            elliptic::BoundaryKind::CurvilinearIsolated);
        const auto& op=solver.op();
        std::vector<double> phi(op.size()),rhs(op.size(),-(4.*a+2.*b)),
            bc(op.faces().size()),applied(op.size());
        for(int i=0;i<op.size();++i)phi[i]=exact(op.center(i));
        for(std::size_t f=0;f<bc.size();++f)
            if(op.faces()[f].boundary_side>=0)bc[f]=exact(op.faces()[f].center);
        const auto lifted=op.effective_rhs(rhs,bc);
        op.apply(phi,applied);
        double operator_error=0.,force_error=0.,total_volume=0.;
        int coarse_fine=0,axial_boundaries=0;
        for(int i=0;i<op.size();++i) {
            operator_error=std::max(operator_error,std::abs(applied[i]-lifted[i]));
            total_volume+=op.volumes()[i];
        }
        for(std::size_t i=0;i<op.faces().size();++i) {
            const auto& face=op.faces()[i];
            const double target=2.*(face.axis==0?a:b)*face.center[face.axis];
            force_error=std::max(force_error,std::abs(op.face_gradient(phi,face,
                face.boundary_side>=0?bc[i]:0.)-target));
            if(face.boundary_side==2 || face.boundary_side==3)++axial_boundaries;
            if(face.left>=0 && face.right>=0
                && op.cells()[face.left].level!=op.cells()[face.right].level)++coarse_fine;
            require(!(inner==0. && face.axis==0 && face.center[0]==0.),
                "RZ axis emitted nonzero-area face");
        }
        const double volume=pi*((inner+1.)*(inner+1.)-inner*inner);
        // Arithmetic polynomial-reproduction gate only; not a science acceptance budget.
        require(std::abs(total_volume-volume)<1e-12*volume,"RZ full volume mismatch");
        require(axial_boundaries>0,"RZ z boundaries became periodic");
        require(!mixed || coarse_fine>0,"RZ witness lacks coarse/fine faces");
        require(operator_error<1e-10 && force_error<1e-10,"RZ polynomial reproduction failed");
        const auto result=solver.solve(lifted,{1e-11,0.,300});
        require(result.report.status==multigrid::SolveStatus::Converged
            && result.report.residual<=result.report.target,"RZ MG failed checked residual");
        double phi_error=0.,solved_face_error=0.;
        for(std::size_t i=0;i<op.faces().size();++i) {
            const auto& face=op.faces()[i];
            const double target=2.*(face.axis==0?a:b)*face.center[face.axis];
            solved_face_error=std::max(solved_face_error,std::abs(op.face_gradient(
                result.potential,face,face.boundary_side>=0?bc[i]:0.)-target));
        }
        for(int i=0;i<op.size();++i)
            phi_error=std::max(phi_error,std::abs(result.potential[i]-phi[i]));
        std::cout<<"RZ_MANUFACTURED inner="<<inner<<" mixed="<<mixed
            <<" cells="<<op.size()<<" coarse_fine="<<coarse_fine
            <<" volume="<<total_volume<<" operator_error="<<operator_error
            <<" face_error="<<force_error<<" phi_error="<<phi_error
            <<" solved_face_error="<<solved_face_error
            <<" cycles="<<result.report.cycles<<" residual="<<result.report.residual
            <<" target="<<result.report.target<<'\n';
        // Report solution error separately; residual alone is not science acceptance.
    }
}


/** Independent smooth native-RZ continuum data, fixed before execution.
 *
 * Phi(r,z) = r^4 + z^4/2 + r^2 z^2/4 + r^2 + z^2/2.
 * Lap_RZ(Phi) = (1/r) d_r(r d_r Phi) + d_zz Phi
 *             = 5 + (33/2) r^2 + 7 z^2.
 *
 * For the actual full-ring cell [a,b] x [c,d], V=pi(b^2-a^2)(d-c).
 * Integrating r*Lap(Phi) independently gives
 * <Lap(Phi)>_V = 5 + (33/4)(a^2+b^2)
 *                 + (7/3)(c^2+cd+d^2).
 * A=-Lap, so the solver RHS is the negative analytic cell average. The
 * corresponding physical density is positive <Lap(Phi)>_V/(4*pi*G).
 * No discrete A*desiredPhi, inferred density, source projection, numerical
 * quadrature or production density-moment implementation supplies this source.
 */
struct NativeRzSmoothReference {
    static double potential(const std::array<double,3>& x) {
        const double r2=x[0]*x[0],z2=x[1]*x[1];
        return r2*r2+.5*z2*z2+.25*r2*z2+r2+.5*z2;
    }
    /** Physical point derivatives in the genuine (r,z) native chart. */
    static std::array<double,3> gradient(const std::array<double,3>& x) {
        const double r=x[0],z=x[1];
        return {4.*r*r*r+.5*r*z*z+2.*r,2.*z*z*z+.5*r*r*z+z,0.};
    }
    /** Closed antiderivatives under the actual native V measure, not midpoint source. */
    static double mean_laplacian(double a,double b,double c,double d) {
        return 5.+(33./4.)*(a*a+b*b)+(7./3.)*(c*c+c*d+d*d);
    }
};

/** One optional N32 actual-operator attribution, never a convergence gate.
 * The independent V-mean source and point Dirichlet values remain unchanged.
 * A*analytic point Phi is used only to observe truncation, never as solver RHS.
 * Cell categories overlap explicitly; masked counts disclose intersections.
 */
void rz_order_diagnostic() {
    constexpr int n=32;
    constexpr double original_polynomial_budget=1e-10;
    auto base=base_mesh(2,n);base.geometry=elliptic::Geometry::Cylindrical;
    base.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
    base.origin={0.,-.5,0.};base.root_upper={1.,.5,0.};
    base.native_canonical_domain=true;
    multigrid::CompositeMultigrid solver(base,make_cells(base,true),
        elliptic::BoundaryKind::CurvilinearIsolated);
    const auto& op=solver.op();
    require(op.base().semantics==GridMetrics::GeometrySemantics::AxisymmetricRz
        &&op.base().native_canonical_domain,"order diagnostic lost actual native geometry");
    const int cells=op.size();
    std::vector<double> source(cells),exact(cells),bc(op.faces().size()),tau(cells),
        true_divergence(cells,0.),flux_tau(cells,0.),solution_error(cells),residual(cells);
    std::vector<unsigned char> cf(cells,0),physical(cells,0),axis(cells,0);
    for(int c=0;c<cells;++c) {
        const double a=op.lower(c,0),b=op.upper(c,0),zlo=op.lower(c,1),zhi=op.upper(c,1);
        require(a>=0.&&b>a&&zhi>zlo&&std::isfinite(a)&&std::isfinite(b)
            &&std::isfinite(zlo)&&std::isfinite(zhi),"order diagnostic has invalid actual cell bounds");
        const double lap=NativeRzSmoothReference::mean_laplacian(a,b,zlo,zhi);
        const double rho=lap/(4.*pi*constants::gravity::cgs::gravitational_constant);
        require(std::isfinite(lap)&&lap>0.&&std::isfinite(rho)&&rho>0.,
            "order diagnostic independent physical source is invalid");
        source[c]=-lap;exact[c]=NativeRzSmoothReference::potential(op.center(c));
        axis[c]=a==0.;
    }
    for(std::size_t f=0;f<op.faces().size();++f)
        if(op.faces()[f].boundary_side>=0)
            bc[f]=NativeRzSmoothReference::potential(op.faces()[f].center);
    const auto lifted=op.effective_rhs(source,bc);
    const auto solved=solver.solve(lifted,{1e-11,0.,300});
    require(solved.report.status==multigrid::SolveStatus::Converged
        &&solved.report.residual<=solved.report.target,
        "order diagnostic solve missed the original residual contract");
    op.apply(solved.potential,residual);op.apply(exact,tau);
    for(int c=0;c<cells;++c) {
        residual[c]-=lifted[c];tau[c]-=lifted[c];
        solution_error[c]=solved.potential[c]-exact[c];
    }
    require(op.norm(residual)<=solved.report.target,
        "order diagnostic actual solved residual missed original target");
    struct FaceStats {
        int count=0;long double area=0.,point_squared=0.,mean_squared=0.;
        double point_max=0.,mean_max=0.,prediction_max=0.;int predicted=0;
        /** Retain weighted point/true-mean errors without an empirical cutoff. */
        void add(double measure,double point_error,double mean_error) {
            ++count;area+=measure;point_squared+=static_cast<long double>(measure)*point_error*point_error;
            mean_squared+=static_cast<long double>(measure)*mean_error*mean_error;
            point_max=std::max(point_max,std::abs(point_error));mean_max=std::max(mean_max,std::abs(mean_error));
        }
    } face_stats[3][2];
    int construction_count[3]{};double prediction_max=0.;
    for(std::size_t f=0;f<op.faces().size();++f) {
        const auto& face=op.faces()[f];int kind=-1;
        switch(face.construction) {
        case elliptic::FaceStencilConstruction::TwoPoint:kind=0;break;
        case elliptic::FaceStencilConstruction::PolynomialFit:kind=1;break;
        case elliptic::FaceStencilConstruction::EllipticRecovery:kind=2;break;
        }
        require(kind>=0&&face.axis>=0&&face.axis<2&&face.native_bounds
            &&std::isfinite(face.area)&&face.area>0.,"order diagnostic invalid actual face construction");
        ++construction_count[kind];
        if(face.boundary_side>=0) {
            if(face.left>=0)physical[face.left]=1;if(face.right>=0)physical[face.right]=1;
        }
        if(face.left>=0&&face.right>=0
            &&op.cells()[face.left].level!=op.cells()[face.right].level)cf[face.left]=cf[face.right]=1;
        const double R=face.center[0],Z=face.center[1];
        const double a=face.fragment_lower[0],b=face.fragment_upper[0];
        const double zlo=face.fragment_lower[1],zhi=face.fragment_upper[1];
        // Radial surface measure is constant R*dz; axial surface is r*dr.
        // These are independent closed antiderivatives over ACTUAL fragments.
        const double mean_gradient=face.axis==0
            ?4.*R*R*R+2.*R+.5*R*(zlo*zlo+zlo*zhi+zhi*zhi)/3.
            :2.*Z*Z*Z+Z+.5*Z*(a*a+b*b)/2.;
        const double point_gradient=NativeRzSmoothReference::gradient(face.center)[face.axis];
        const double numerical=op.face_gradient(exact,face,face.boundary_side>=0?bc[f]:0.);
        require(std::isfinite(mean_gradient)&&std::isfinite(numerical),
            "order diagnostic gradient is nonfinite");
        const double point_error=numerical-point_gradient,mean_error=numerical-mean_gradient;
        face_stats[kind][face.axis].add(face.area,point_error,mean_error);
        const auto add_flux=[&](int c,double sign) {
            if(c<0)return;
            true_divergence[c]+=sign*face.area*mean_gradient/op.volumes()[c];
            flux_tau[c]+=sign*face.area*mean_error/op.volumes()[c];
        };
        add_flux(face.left,-1.);add_flux(face.right,1.);
        if(kind==0&&face.left>=0&&face.right>=0
            &&op.cells()[face.left].level==op.cells()[face.right].level) {
            const double h=op.width(face.left,face.axis);
            require(h==op.width(face.right,face.axis),"order diagnostic equal-level widths differ");
            const double dr=b-a,dz=zhi-zlo;
            // Actual normal widths, not tangent metrics or root spacing.
            const double predicted=face.axis==0?R*h*h-R*dz*dz/24.
                :.5*Z*h*h-Z*dr*dr/8.;
            const double mismatch=std::abs(mean_error-predicted);
            ++face_stats[kind][face.axis].predicted;
            face_stats[kind][face.axis].prediction_max=std::max(face_stats[kind][face.axis].prediction_max,mismatch);
            prediction_max=std::max(prediction_max,mismatch);
        }
    }
    double fv_identity_max=0.,tau_identity_max=0.;
    for(int c=0;c<cells;++c) {
        fv_identity_max=std::max(fv_identity_max,std::abs(true_divergence[c]-source[c]));
        tau_identity_max=std::max(tau_identity_max,std::abs(tau[c]-flux_tau[c]));
    }
    // Reuse the original polynomial reproduction budget, not a fitted tolerance.
    require(fv_identity_max<original_polynomial_budget,
        "order diagnostic true face integrals disagree with independent V source");
    require(tau_identity_max<original_polynomial_budget,
        "order diagnostic actual tau differs from independent face-error sum");
    require(prediction_max<original_polynomial_budget,
        "order diagnostic regular-face analytic truncation identity failed");
    struct CellStats {
        int count=0;long double volume=0.,l1=0.,squared=0.,signed_sum=0.,source_sum=0.,source_abs=0.;
        double maximum=0.;
        /** Keep actual V and source-scaled signed sums; categories may overlap. */
        void add(double V,double value,double rhs) {
            require(std::isfinite(V)&&V>0.&&std::isfinite(value)&&std::isfinite(rhs),
                "order diagnostic category contains invalid values");
            ++count;volume+=V;l1+=static_cast<long double>(V)*std::abs(value);
            squared+=static_cast<long double>(V)*value*value;signed_sum+=static_cast<long double>(V)*value;
            source_sum+=static_cast<long double>(V)*rhs;source_abs+=static_cast<long double>(V)*std::abs(rhs);
            maximum=std::max(maximum,std::abs(value));
        }
        /** Print bounded summaries, not per-cell raw arrays or scientific PASS. */
        void print(const char* category,const char* field) const {
            std::cout<<"RZ_ORDER_CELL category="<<category<<" field="<<field<<" count="<<count
                <<" V="<<volume<<" V_L1="<<(volume?l1/volume:0.)
                <<" V_RMS="<<(volume?std::sqrt(squared/volume):0.)<<" Linf="<<maximum
                <<" signed_V_sum="<<signed_sum<<" signed_V_mean="<<(volume?signed_sum/volume:0.)
                <<" source_V_sum="<<source_sum<<" source_abs_V_sum="<<source_abs
                <<" signed_source_ratio="<<(source_abs?signed_sum/source_abs:0.)<<'\n';
        }
    };
    CellStats stats[5][2];int masks[16]{};
    for(int c=0;c<cells;++c) {
        const bool regular=!cf[c]&&!physical[c];
        const unsigned mask=(regular?1U:0U)|(cf[c]?2U:0U)|(physical[c]?4U:0U)|(axis[c]?8U:0U);
        ++masks[mask];const bool selected[5]{true,regular,cf[c]!=0,physical[c]!=0,axis[c]!=0};
        for(int category=0;category<5;++category)if(selected[category]) {
            stats[category][0].add(op.volumes()[c],tau[c],source[c]);
            stats[category][1].add(op.volumes()[c],solution_error[c],source[c]);
        }
    }
    const char* categories[5]{"all","uniform_interior","CF_adjacent","physical_boundary","axis_first_row"};
    std::cout<<std::setprecision(17)<<"RZ_ORDER_DIAGNOSTIC n=32 mixed=1 axis=1 cells="<<cells
        <<" source=independent_V_mean physical_qualified=false convergence_qualified=false"
        <<" cycles="<<solved.report.cycles<<" residual="<<op.norm(residual)<<" target="<<solved.report.target
        <<" fv_identity_max="<<fv_identity_max<<" tau_identity_max="<<tau_identity_max
        <<" regular_prediction_max="<<prediction_max<<'\n';
    for(int category=0;category<5;++category) {
        stats[category][0].print(categories[category],"tau");stats[category][1].print(categories[category],"point_phi_error");
    }
    for(int mask=0;mask<16;++mask)std::cout<<"RZ_ORDER_INTERSECTION mask="<<mask
        <<" bits=regular1_CF2_physical4_axis8 count="<<masks[mask]<<'\n';
    const char* constructions[3]{"TwoPoint","PolynomialFit","EllipticRecovery"};
    for(int kind=0;kind<3;++kind)for(int direction=0;direction<2;++direction) {
        const auto& stats=face_stats[kind][direction];
        std::cout<<"RZ_ORDER_FACE construction="<<constructions[kind]<<" direction="<<direction
            <<" total_construction_count="<<construction_count[kind]<<" count="<<stats.count<<" area="<<stats.area
            <<" point_RMS="<<(stats.area?std::sqrt(stats.point_squared/stats.area):0.)
            <<" mean_RMS="<<(stats.area?std::sqrt(stats.mean_squared/stats.area):0.)
            <<" point_Linf="<<stats.point_max<<" mean_Linf="<<stats.mean_max
            <<" predicted_count="<<stats.predicted<<" prediction_mismatch="<<stats.prediction_max<<'\n';
    }
}

/** Smooth point-potential/full-ring-source convergence through the real MG.
 * Workflow:
 * 1. Construct actual native canonical endpoint meshes and the existing mixed
 *    dyadic central refinement topology, separately on axis and annulus.
 * 2. Supply independent analytic V-mean source and point Dirichlet values.
 * 3. Solve with the unchanged 1e-11/0/300 contract and check actual residual.
 * 4. Compare true geometric-point Phi and actual fragment-center derivatives
 *    using V-RMS/area-RMS; all consecutive orders must reach original >=1.8.
 *
 * This qualifies a prescribed-Dirichlet native composite Poisson discretization
 * only. It grants no finite-ring isolated boundary, field force consumer,
 * Runtime evolution, source inspection, CUDA, or public native-RZ capability.
 */
void rz_smooth_convergence() {
    bool all_orders_accepted=true;
    for(double inner:{0.,1.})for(bool mixed:{false,true}) {
        double previous_phi=0.,previous_face=0.,previous_interface=0.;
        int checked_orders=0;
        for(int n:{16,32,64,128}) {
            auto base=base_mesh(2,n);base.geometry=elliptic::Geometry::Cylindrical;
            base.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
            base.origin={inner,-.5,0.};base.root_upper={inner+1.,.5,0.};
            base.native_canonical_domain=true;
            multigrid::CompositeMultigrid solver(base,make_cells(base,mixed),
                elliptic::BoundaryKind::CurvilinearIsolated);
            const auto& op=solver.op();
            require(op.base().semantics==GridMetrics::GeometrySemantics::AxisymmetricRz
                &&op.base().native_canonical_domain,"smooth reference lost actual native RZ geometry");
            std::vector<double> rhs(op.size()),bc(op.faces().size()),error(op.size()),residual(op.size());
            double rho_min=std::numeric_limits<double>::infinity(),rho_max=0.;
            for(int cell=0;cell<op.size();++cell) {
                const double a=op.lower(cell,0),b=op.upper(cell,0);
                const double c=op.lower(cell,1),d=op.upper(cell,1);
                require(std::isfinite(a)&&std::isfinite(b)&&b>a&&a>=0.
                    &&std::isfinite(c)&&std::isfinite(d)&&d>c,
                    "smooth manufactured source has invalid actual cell endpoints");
                const double mean=NativeRzSmoothReference::mean_laplacian(a,b,c,d);
                const double rho=mean/(4.*pi*constants::gravity::cgs::gravitational_constant);
                require(std::isfinite(mean)&&mean>0.&&std::isfinite(rho)&&rho>0.,
                    "smooth manufactured V-mean physical density is nonpositive/nonfinite");
                rhs[cell]=-mean;rho_min=std::min(rho_min,rho);rho_max=std::max(rho_max,rho);
            }
            for(std::size_t f=0;f<op.faces().size();++f)
                if(op.faces()[f].boundary_side>=0)
                    bc[f]=NativeRzSmoothReference::potential(op.faces()[f].center);
            const auto lifted=op.effective_rhs(rhs,bc);
            const auto result=solver.solve(lifted,{1e-11,0.,300});
            require(result.report.status==multigrid::SolveStatus::Converged
                &&result.report.residual<=result.report.target,
                "native RZ smooth manufactured solve did not meet original residual");
            // A*actualSolvedPhi is only an independent residual check. It does
            // not construct the source, reference Phi or reference gradient.
            op.apply(result.potential,residual);
            for(int cell=0;cell<op.size();++cell) {
                residual[cell]-=lifted[cell];
                error[cell]=result.potential[cell]-NativeRzSmoothReference::potential(op.center(cell));
            }
            require(op.norm(residual)<=result.report.target,
                "native RZ smooth independent solved residual exceeds original target");
            const double phi=op.norm(error); // Actual normalized native V-RMS.
            double face_squared=0.,face_area=0.,interface_squared=0.,interface_area=0.;
            int interface_faces=0,axial_faces=0;
            for(std::size_t f=0;f<op.faces().size();++f) {
                const auto& face=op.faces()[f];
                require(face.native_bounds&&std::isfinite(face.area)&&face.area>0.,
                    "smooth reference face lacks its actual native fragment measure");
                require(!(inner==0.&&face.axis==0&&face.center[0]==0.),
                    "smooth reference axis emitted a nonzero-area flux face");
                if(face.boundary_side==2||face.boundary_side==3)++axial_faces;
                const double analytic=NativeRzSmoothReference::gradient(face.center)[face.axis];
                const double numerical=op.face_gradient(result.potential,face,
                    face.boundary_side>=0?bc[f]:0.);
                const double difference=numerical-analytic;
                face_squared+=face.area*difference*difference;face_area+=face.area;
                if(face.left>=0&&face.right>=0
                    &&op.cells()[face.left].level!=op.cells()[face.right].level) {
                    ++interface_faces;interface_squared+=face.area*difference*difference;
                    interface_area+=face.area;
                }
            }
            require(axial_faces>0&&face_area>0.&&(!mixed||(interface_faces>0&&interface_area>0.)),
                "smooth reference omitted real physical axial or coarse/fine faces");
            const double face=std::sqrt(face_squared/face_area);
            const double interface_force=mixed?std::sqrt(interface_squared/interface_area):0.;
            require(std::isfinite(phi)&&phi>0.&&std::isfinite(face)&&face>0.
                &&(!mixed||(std::isfinite(interface_force)&&interface_force>0.)),
                "smooth reference error norm vanished or became nonfinite");
            std::cout<<"RZ_SMOOTH_CONVERGENCE inner="<<inner<<" mixed="<<mixed<<" n="<<n
                <<" cells="<<op.size()<<" interface_faces="<<interface_faces
                <<" phi_v_rms="<<phi<<" face_area_rms="<<face
                <<" interface_area_rms="<<interface_force<<" rho_v_min="<<rho_min<<" rho_v_max="<<rho_max
                <<" cycles="<<result.report.cycles<<" residual="<<op.norm(residual)
                <<" target="<<result.report.target;
            if(previous_phi>0.) {
                const double qp=std::log2(previous_phi/phi),qf=std::log2(previous_face/face);
                const double qi=mixed?std::log2(previous_interface/interface_force):0.;
                std::cout<<" orders="<<qp<<','<<qf;if(mixed)std::cout<<','<<qi;
                std::cout<<std::endl; // Keep the failing pair's evidence before assertion.
                // Preserve every frozen gate; collect the remaining domains before
                // reporting a failed owner so one coarse pair cannot hide others.
                all_orders_accepted=all_orders_accepted
                    &&qp>=1.8&&qf>=1.8&&(!mixed||qi>=1.8);
                ++checked_orders;
            } else std::cout<<std::endl;
            previous_phi=phi;previous_face=face;previous_interface=interface_force;
        }
        require(checked_orders==3,"native RZ smooth convergence lost consecutive resolution pairs");
    }
    require(all_orders_accepted,
        "native RZ smooth potential/face/coarse-fine order below original 1.8");
}


/** Closed polynomial reference for TWO prescribed smooth fields, not A*Phi.
 * Phi0=r^4+z^4/2+r^2*z^2/4+r^2+z^2/2.
 * Phi1=2*r^4+z^4/4+r^2*z^2/2+r^2/2+2*z^2+r^2*z.
 * Cylindrical Laplacians are L0=5+33*r^2/2+7*z^2 and
 * L1=6+33*r^2+5*z^2+4*z, positive on both frozen reference domains.
 * Raw term products keep exact binary coefficients; all integrals below are
 * independent polynomial antiderivatives under dV/(2*pi)=r*dr*dz.
 */
struct RzGreenPolynomial {
    struct Term {long double coefficient;int radial,axial;};
    using Terms=std::vector<Term>;
    static Terms potential(int field) {
        return field==0?Terms{{1.,4,0},{.5,0,4},{.25,2,2},{1.,2,0},{.5,0,2}}
            :Terms{{2.,4,0},{.25,0,4},{.5,2,2},{.5,2,0},{2.,0,2},{1.,2,1}};
    }
    /** Independent continuum Laplacian coefficients, never operator rows. */
    static Terms laplacian(int field) {
        return field==0?Terms{{5.,0,0},{16.5,2,0},{7.,0,2}}
            :Terms{{6.,0,0},{33.,2,0},{5.,0,2},{4.,0,1}};
    }
    /** Differentiate an explicit physical polynomial in the selected chart axis. */
    static Terms derivative(const Terms& terms,int axis) {
        Terms result;
        for(auto term:terms) {
            const int power=axis==0?term.radial:term.axial;if(!power)continue;
            term.coefficient*=power;if(axis==0)--term.radial;else --term.axial;
            result.push_back(term);
        }
        return result;
    }
    /** Product subtraction for the independent Green cross polynomials. */
    static Terms cross(const Terms& a,const Terms& b,const Terms& c,const Terms& d) {
        Terms result;
        for(const auto& x:a)for(const auto& y:b)
            result.push_back({x.coefficient*y.coefficient,x.radial+y.radial,x.axial+y.axial});
        for(const auto& x:c)for(const auto& y:d)
            result.push_back({-x.coefficient*y.coefficient,x.radial+y.radial,x.axial+y.axial});
        return result;
    }
    /** Integer powers use only multiplication, with degree at most eight here. */
    static long double power(long double x,int degree) {
        long double result=1.;for(int i=0;i<degree;++i)result*=x;return result;
    }
    /** Independent physical point evaluation; no cell-average interpretation. */
    static long double point(const Terms& terms,long double r,long double z) {
        long double result=0.;for(const auto& term:terms)
            result+=term.coefficient*power(r,term.radial)*power(z,term.axial);
        return result;
    }
    struct Integral {long double value=0.,scale=0.;};
    /** Exact formula integral r*P dr dz. Scale retains BOTH endpoint powers
     * before subtraction, bounding antiderivative cancellation prospectively.
     */
    static Integral volume(const Terms& terms,long double a,long double b,long double c,long double d) {
        Integral result;
        for(const auto& term:terms) {
            const int rp=term.radial+2,zp=term.axial+1;
            const long double bl=power(b,rp),al=power(a,rp),du=power(d,zp),cl=power(c,zp);
            result.value+=term.coefficient*((bl-al)/rp)*((du-cl)/zp);
            result.scale+=std::abs(term.coefficient)*(std::abs(bl)+std::abs(al))/rp
                *(std::abs(du)+std::abs(cl))/zp;
        }
        return result;
    }
    /** Exact face integral of r*P, per full-circle factor 2*pi. The normal
     * coordinate is fixed; tangential integration uses actual fragment bounds.
     */
    static Integral face(const Terms& terms,int axis,long double normal,long double lo,long double hi) {
        Integral result;
        for(const auto& term:terms) {
            const int degree=axis==0?term.axial+1:term.radial+2;
            const long double h=power(hi,degree),l=power(lo,degree);
            const long double fixed=power(normal,axis==0?term.radial+1:term.axial);
            result.value+=term.coefficient*fixed*(h-l)/degree;
            result.scale+=std::abs(term.coefficient*fixed)*(std::abs(h)+std::abs(l))/degree;
        }
        return result;
    }
    /** Each expression above uses <4096 long-double operations per integral
     * (<=48 raw cross terms, <=8 multiplications/power). gamma_m bounds forward
     * roundoff against the absolute raw-endpoint scale, not physical error.
     * epsilon is conservative (2u); factor four encloses evaluation of the
     * budget/scale itself. This is fixed BEFORE results, not an observed fit.
     */
    static long double allowance(std::size_t operations,long double scale) {
        const long double eps=std::numeric_limits<long double>::epsilon();
        const long double meps=operations*eps;
        require(std::isfinite(scale)&&scale>=0.&&meps<.5L,
            "two-field Green roundoff count/scale is invalid");
        return 4.L*(meps/(1.L-meps))*scale;
    }
};

/** Genuine native two-field Green consistency with no additional MG solve.
 * Workflow: use the ORIGINAL axis/annulus, uniform/mixed N16/32/64/128 meshes;
 * integrate independent continuum sources/boundary polynomials; sample the
 * actual stored point fields and original face gradients; verify continuous
 * and face-incidence identities with prospective arithmetic bounds ONLY.
 *
 * C=integral(Phi1*LapPhi0-Phi0*LapPhi1)dV equals boundary Green exactly.
 * For stored fields, C_h-B_h=T_internal+T_boundary-T_residual, with
 * R_k=sum sigma*A*g_k/V-LapPhi_k(Vmean). Every physical/CF/axis cell remains.
 * Point-vs-integrated and original operator/fit errors are DIAGNOSTICS, without
 * a new empirical physical allowance or isolated/coupled qualification.
 * Original true MG, 1e-11/300, >=1.8 order tests remain separate and unchanged.
 */
void rz_two_field_green_consistency() {
    using Reference=RzGreenPolynomial;
    const auto phi0=Reference::potential(0),phi1=Reference::potential(1);
    const auto lap0=Reference::laplacian(0),lap1=Reference::laplacian(1);
    const auto cross=Reference::cross(phi1,lap0,phi0,lap1);
    const std::array<Reference::Terms,2> boundary_cross{
        Reference::cross(phi1,Reference::derivative(phi0,0),phi0,Reference::derivative(phi1,0)),
        Reference::cross(phi1,Reference::derivative(phi0,1),phi0,Reference::derivative(phi1,1))};
    for(double inner:{0.,1.})for(bool mixed:{false,true})for(int n:{16,32,64,128}) {
        auto base=base_mesh(2,n);base.geometry=elliptic::Geometry::Cylindrical;
        base.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
        base.origin={inner,-.5,0.};base.root_upper={inner+1.,.5,0.};base.native_canonical_domain=true;
        elliptic::CompositePoisson op(base,make_cells(base,mixed),elliptic::BoundaryKind::CurvilinearIsolated);
        const auto exact_cross=Reference::volume(cross,inner,inner+1.,-.5,.5);
        Reference::Integral exact_boundary;
        for(int axis=0;axis<2;++axis)for(int side=0;side<2;++side) {
            const long double coordinate=axis==0?inner+side:(side?.5L:-.5L);
            const auto value=Reference::face(boundary_cross[axis],axis,coordinate,
                axis==0?-.5L:inner,axis==0?.5L:inner+1.);
            exact_boundary.value+=(side?1.L:-1.L)*value.value;exact_boundary.scale+=value.scale;
        }
        const auto analytic_allowance=Reference::allowance(4096*5,exact_cross.scale+exact_boundary.scale);
        require(std::abs(exact_cross.value-exact_boundary.value)<=analytic_allowance,
            "two-field continuum volume/face-integrated Green identity failed");
        require(std::abs(exact_cross.value)>analytic_allowance,
            "two-field reference cross integral vanished; Green witness is degenerate");
        std::array<std::vector<double>,2> phi,lap,bc,rhs,applied;
        std::array<std::vector<long double>,2> divergence;
        for(int k=0;k<2;++k) {
            phi[k].resize(op.size());lap[k].resize(op.size());rhs[k].resize(op.size());
            applied[k].resize(op.size());bc[k].resize(op.faces().size());divergence[k].resize(op.size());
        }
        long double leaf_integral=0.,leaf_scale=0.,face_integral=0.,face_scale=0.;
        long double stored_cross=0.,stored_scale=0.,boundary=0.,analytic_point_boundary=0.;
        long double internal=0.,physical=0.,residual_cross=0.,identity_scale=0.;
        int axis_cells=0,physical_faces=0,cf_faces=0;
        for(int cell=0;cell<op.size();++cell) {
            const double a=op.lower(cell,0),b=op.upper(cell,0),c=op.lower(cell,1),d=op.upper(cell,1);
            require(a>=0.&&b>a&&d>c,"two-field Green has invalid actual cell endpoints");
            if(a==0.)++axis_cells;
            const auto integral=Reference::volume(cross,a,b,c,d);
            leaf_integral+=integral.value;leaf_scale+=integral.scale;
            const long double measure=(static_cast<long double>(b)*b-static_cast<long double>(a)*a)*(d-c)/2.L;
            for(int k=0;k<2;++k) {
                const auto& p=k?phi1:phi0;const auto& l=k?lap1:lap0;
                phi[k][cell]=static_cast<double>(Reference::point(p,op.center(cell)[0],op.center(cell)[1]));
                lap[k][cell]=static_cast<double>(Reference::volume(l,a,b,c,d).value/measure);
                require(std::isfinite(phi[k][cell])&&std::isfinite(lap[k][cell])&&lap[k][cell]>0.,
                    "two-field independent V-mean physical source is nonpositive/nonfinite");
                rhs[k][cell]=-lap[k][cell]; // NEVER A*analyticPhi.
            }
            const long double v=op.volumes()[cell];
            stored_cross+=v*(static_cast<long double>(phi[1][cell])*lap[0][cell]
                -static_cast<long double>(phi[0][cell])*lap[1][cell]);
            stored_scale+=v*(std::abs(static_cast<long double>(phi[1][cell])*lap[0][cell])
                +std::abs(static_cast<long double>(phi[0][cell])*lap[1][cell]));
        }
        for(std::size_t f=0;f<op.faces().size();++f) {
            const auto& face=op.faces()[f];const long double area=face.area;
            require(face.native_bounds&&face.area>0.&&face.axis<2,
                "two-field Green lacks genuine positive native face fragments");
            std::array<double,2> gradient{};
            for(int k=0;k<2;++k) {
                bc[k][f]=face.boundary_side>=0?static_cast<double>(Reference::point(k?phi1:phi0,face.center[0],face.center[1])):0.;
                gradient[k]=op.face_gradient(phi[k],face,bc[k][f]);
                require(std::isfinite(gradient[k]),"two-field actual face gradient is nonfinite");
                if(face.left>=0)divergence[k][face.left]+=area*gradient[k];
                if(face.right>=0)divergence[k][face.right]-=area*gradient[k];
            }
            for(int cell:{face.left,face.right})if(cell>=0)
                identity_scale+=area*(std::abs(static_cast<long double>(phi[1][cell])*gradient[0])
                    +std::abs(static_cast<long double>(phi[0][cell])*gradient[1]));
            if(face.boundary_side>=0) {
                ++physical_faces;
                const long double sign=(face.boundary_side&1)?1.L:-1.L;
                const int cell=face.left>=0?face.left:face.right;
                const auto integrated=Reference::face(boundary_cross[face.axis],face.axis,face.center[face.axis],
                    face.fragment_lower[1-face.axis],face.fragment_upper[1-face.axis]);
                face_integral+=sign*integrated.value;face_scale+=integrated.scale;
                boundary+=sign*area*(static_cast<long double>(bc[1][f])*gradient[0]
                    -static_cast<long double>(bc[0][f])*gradient[1]);
                analytic_point_boundary+=sign*area*Reference::point(boundary_cross[face.axis],face.center[0],face.center[1]);
                physical+=sign*area*((static_cast<long double>(phi[1][cell])-bc[1][f])*gradient[0]
                    -(static_cast<long double>(phi[0][cell])-bc[0][f])*gradient[1]);
                identity_scale+=2.L*area*(std::abs(static_cast<long double>(bc[1][f])*gradient[0])
                    +std::abs(static_cast<long double>(bc[0][f])*gradient[1]));
            } else {
                require(face.left>=0&&face.right>=0,"two-field internal face has missing actual incidence");
                if(op.cells()[face.left].level!=op.cells()[face.right].level)++cf_faces;
                internal+=area*((static_cast<long double>(phi[1][face.left])-phi[1][face.right])*gradient[0]
                    -(static_cast<long double>(phi[0][face.left])-phi[0][face.right])*gradient[1]);
            }
        }
        long double max_face_residual=0.;
        for(int cell=0;cell<op.size();++cell) {
            const long double v=op.volumes()[cell];
            const long double r0=divergence[0][cell]/v-lap[0][cell],r1=divergence[1][cell]/v-lap[1][cell];
            residual_cross+=v*(phi[1][cell]*r0-phi[0][cell]*r1);
            max_face_residual=std::max(max_face_residual,std::max(std::abs(r0),std::abs(r1)));
        }
        identity_scale=4.L*(identity_scale+stored_scale);
        const std::size_t samples=static_cast<std::size_t>(op.size())+op.faces().size()+4;
        // <=256 primitive long-double operations per cell/face for this identity;
        // gamma includes all incidence and final signed reductions/divisions.
        const auto discrete_allowance=Reference::allowance(256*samples,identity_scale);
        const long double incidence_defect=stored_cross-boundary-internal-physical+residual_cross;
        require(std::abs(incidence_defect)<=discrete_allowance,
            "two-field original-face discrete Green incidence identity failed");
        require(std::abs(leaf_integral-exact_cross.value)<=Reference::allowance(4096*samples,leaf_scale+exact_cross.scale)
            &&std::abs(face_integral-exact_boundary.value)<=Reference::allowance(4096*samples,face_scale+exact_boundary.scale),
            "two-field actual cells/physical fragments lost the continuum integration domain");
        require(physical_faces>0&&(!mixed||cf_faces>0)&&(inner!=0.||axis_cells>0),
            "two-field Green omitted physical/coarse-fine/axis cell coverage");
        double operator_residual=0.;
        for(int k=0;k<2;++k) {
            const auto lifted=op.effective_rhs(rhs[k],bc[k]);op.apply(phi[k],applied[k]);
            for(int cell=0;cell<op.size();++cell)applied[k][cell]-=lifted[cell];
            operator_residual=std::max(operator_residual,op.norm(applied[k]));
        }
        std::cout<<std::setprecision(17)<<"RZ_TWO_FIELD_GREEN_DIAGNOSTIC inner="<<inner<<" mixed="<<mixed<<" n="<<n
            <<" cells="<<op.size()<<" physical_faces="<<physical_faces<<" cf_faces="<<cf_faces<<" axis_cells="<<axis_cells
            <<" continuous_cross_per_2pi="<<exact_cross.value<<" continuous_boundary_per_2pi="<<exact_boundary.value
            <<" continuum_identity_allowance="<<analytic_allowance
            <<" point_volume_defect="<<stored_cross-2.L*pi*exact_cross.value
            <<" analytic_face_center_quadrature_defect="<<analytic_point_boundary-2.L*pi*exact_boundary.value
            <<" actual_stencil_vs_analytic_face_sample_defect="<<boundary-analytic_point_boundary
            <<" internal="<<internal<<" boundary_anchor="<<physical<<" residual_cross="<<residual_cross
            <<" stored_cross_minus_boundary="<<stored_cross-boundary<<" incidence_defect="<<incidence_defect
            <<" incidence_roundoff_allowance="<<discrete_allowance
            <<" analytic_point_operator_rms="<<operator_residual<<" analytic_point_face_residual_max="<<max_face_residual
            <<" physical_qualified=0 no_extra_mg=1"<<std::endl;
    }
}

void curved_manufactured(bool singular=false, bool seam_refined=false) {
    for(auto geometry:{elliptic::Geometry::Cylindrical,elliptic::Geometry::Spherical})
        for(int dim:{2,3}) for(bool refined:{false,true}) {
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
    for(bool rz:{false,true})for(bool refined:{false,true}) {
        auto base=base_mesh(2,4);
        if(rz) {
            base.geometry=elliptic::Geometry::Cylindrical;
            base.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
        }
        CompositePoisson op(base,make_cells(base,refined),
            rz?BoundaryKind::CurvilinearIsolated:BoundaryKind::Dirichlet);
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
        const auto ring_estimate=Physical::Gravity::finite_ring_potential_estimate(
            .5,1.,-.375,.375,1.,2.,0.,constants::gravity::cgs::gravitational_constant);
        require(!ring_estimate.error_is_certified,"ring estimate silently became certified");
        invalid[used].absolute_error=ring_estimate.estimated_error;
        invalid[used].quality=ring_estimate.error_is_certified
            ?BoundaryErrorQuality::CertifiedAbsolute:BoundaryErrorQuality::Estimate;
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
        std::cout<<"BOUNDARY_RHS_LEDGER rz="<<rz<<" refined="<<refined
            <<" cells="<<op.size()<<" max_bound_ratio="<<maximum_ratio<<'\n';
    }
    std::cout<<"BOUNDARY_RHS_LEDGER_PASS\n";
}

void boundary_original_rhs_acceptance_contract() {
    using namespace elliptic;
    auto base=base_mesh(2,4);base.geometry=elliptic::Geometry::Cylindrical;
    base.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
    CompositePoisson op(base,make_cells(base,true),BoundaryKind::CurvilinearIsolated);
    std::vector<double> rhs(op.size(),1.),residual(op.size(),1.e-5),zero(op.size(),0.);
    std::vector<BoundaryPotentialError> errors(op.faces().size(),
        {1.e-9,BoundaryErrorQuality::CertifiedAbsolute});
    const auto budget=op.propagate_boundary_error(errors);
    const auto quality=BoundaryErrorQuality::CertifiedAbsolute;
    const auto accepted=op.assess_boundary_residual(rhs,residual,budget,0.,0.,quality,1.e-3,0.);
    require(accepted.status==BoundaryResidualStatus::Accepted,"sufficient original RHS criterion rejected");
    require(accepted.tolerance_safe<=1.e-3,"safe target relaxed the original request");
    for(int pattern=0;pattern<16;++pattern) {
        std::vector<double> face(errors.size());
        for(std::size_t i=0;i<face.size();++i)face[i]=((i+pattern)%3==0?-1.:1.)*errors[i].absolute_error;
        const auto perturbation=op.effective_rhs(zero,face);
        std::vector<double> exact_rhs(rhs),exact_residual(residual);
        for(int i=0;i<op.size();++i) {exact_rhs[i]+=perturbation[i];exact_residual[i]+=perturbation[i];}
        require(op.norm(exact_residual)<=1.e-3*op.norm(exact_rhs),
            "accepted bound failed original perturbed RHS request");
        const auto interval=op.norm_interval(exact_rhs);
        require(interval.status==BoundaryErrorStatus::Bounded
            &&interval.lower<=op.norm(exact_rhs)&&op.norm(exact_rhs)<=interval.upper,
            "canonical weighted norm escaped its interval");
    }
    auto zero_errors=errors;for(auto& e:zero_errors)e.absolute_error=0.;
    const auto no_error=op.propagate_boundary_error(zero_errors);
    const auto exact_zero=op.assess_boundary_residual(zero,zero,no_error,0.,0.,quality,1.e-3,0.);
    require(exact_zero.status==BoundaryResidualStatus::Accepted
        &&exact_zero.tolerance_safe==0.&&exact_zero.total_residual_upper==0.,
        "zero request gained a hidden tolerance floor");
    const auto cancellation=op.assess_boundary_residual(zero,zero,budget,0.,0.,quality,1.e-3,0.);
    require(cancellation.status==BoundaryResidualStatus::ResidualTooLarge
        &&cancellation.tolerance_safe==0.,"cancelled approximate RHS hid finite boundary error");
    require(op.assess_boundary_residual(rhs,residual,budget,0.,0.,
        BoundaryErrorQuality::Estimate,1.e-3,0.).status==BoundaryResidualStatus::UncertifiedInput,
        "uncertified assembly/residual arithmetic was accepted");
    require(op.assess_boundary_residual(rhs,residual,budget,0.,1.,
        quality,1.e-3,0.).status==BoundaryResidualStatus::ResidualTooLarge,
        "large residual-evaluation uncertainty ignored");
    require(op.assess_boundary_residual(rhs,residual,budget,0.,0.,
        quality,-1.,0.).status==BoundaryResidualStatus::InvalidInput,
        "negative requested tolerance accepted");
    std::vector<double> tiny(op.size(),std::numeric_limits<double>::denorm_min());
    const auto subnormal=op.norm_interval(tiny);
    require(subnormal.status==BoundaryErrorStatus::Bounded&&subnormal.upper>0.,
        "positive subnormal norm silently became certified zero");
    std::cout<<"BOUNDARY_ORIGINAL_RHS_ACCEPTANCE_PASS cells="<<op.size()
        <<" safe_target="<<accepted.tolerance_safe<<" residual_upper="
        <<accepted.total_residual_upper<<" cancellation_target="<<cancellation.tolerance_safe
        <<" cancellation_status=ResidualTooLarge\n";
}

int main(int argc,char** argv) {
    try {
        if(argc>1 && std::string(argv[1])=="periodic-source-bounds-probe") {periodic_gravity_source_probe();return 0;}
        if(argc>1 && std::string(argv[1])=="projection-ledger-probe") {constant_mode_projection_probe();return 0;}
        if(argc>1 && std::string(argv[1])=="gravity-source-bounds-probe") {isolated_gravity_source_probe();return 0;}
        if(argc>1 && std::string(argv[1])=="arithmetic-ledger-probe") {native_arithmetic_ledger_probe();return 0;}
        if(argc>1 && std::string(argv[1])=="ring-contact-gauss3") {std::cout<<std::setprecision(17);finite_ring_contact_gauss3_contract();return 0;}
        if(argc>1 && std::string(argv[1])=="ring-contact-log") {std::cout<<std::setprecision(17);finite_ring_contact_log_contract();return 0;}
        if(argc==3 && (std::string(argv[1])=="ring-atan-probe"||
                      std::string(argv[1])=="ring-positive-log-probe")) {
            std::cout<<std::setprecision(17);
            const double x=std::strtod(argv[2],nullptr);
            const auto bound=std::string(argv[1])=="ring-atan-probe"?
                Physical::Gravity::finite_ring_detail::positive_atan_point_enclosure(x):
                Physical::Gravity::finite_ring_detail::positive_log_point_enclosure(x);
            require(Physical::Gravity::finite_ring_detail::interval_finite(bound),"invalid primitive probe");
            std::cout<<"{\"lower\":"<<bound.lower<<",\"upper\":"<<bound.upper<<"}\n";return 0;
        }
        if(argc==4 && std::string(argv[1])=="ring-quadrant-log-probe") {
            std::cout<<std::setprecision(17);
            const double a=std::stod(argv[2]),b=std::stod(argv[3]);
            const auto bound=Physical::Gravity::finite_ring_detail::quadrant_log_distance_integral({a,a},{b,b});
            require(Physical::Gravity::finite_ring_detail::interval_finite(bound),"invalid quadrant probe");
            std::cout<<"{\"lower\":"<<bound.lower<<",\"upper\":"<<bound.upper<<"}\n";return 0;
        }
        if(argc==8 && std::string(argv[1])=="ring-contact-log-probe") {
            std::cout<<std::setprecision(17);
            const auto bound=Physical::Gravity::finite_ring_detail::contact_log_integral_enclosure(
                std::stod(argv[2]),std::stod(argv[3]),std::stod(argv[4]),std::stod(argv[5]),
                std::stod(argv[6]),std::stod(argv[7]));
            require(Physical::Gravity::finite_ring_detail::interval_finite(bound),"invalid contact log probe");
            std::cout<<"{\"lower\":"<<bound.lower<<",\"upper\":"<<bound.upper<<"}\n";return 0;
        }
        if(argc>1 && std::string(argv[1])=="ring-separated-gauss") {std::cout<<std::setprecision(17);finite_ring_separated_gauss_contract();return 0;}
        if(argc>1 && std::string(argv[1])=="ring-far-leaf") {std::cout<<std::setprecision(17);finite_ring_far_leaf_contract();return 0;}
        if(argc>1 && std::string(argv[1])=="rz-stencil-probe") {native_rz_stencil_probe();return 0;}
        if(argc>1 && std::string(argv[1])=="rz-measure-probe") {native_rz_measure_probe();return 0;}
        if(argc>1 && std::string(argv[1])=="ring-balanced-reduction-probe") {ring_balanced_reduction_probe();return 0;}
        if(argc>1 && std::string(argv[1])=="ring-uniform-quartet") {finite_ring_uniform_quartet_contract();return 0;}
        if(argc>1 && std::string(argv[1])=="ring-source-retirement") {ring_source_retirement_contract();return 0;}
        if(argc>1 && std::string(argv[1])=="native-ring-mixed-probe") {native_ring_solved_probe(true);return 0;}
        if(argc>1 && std::string(argv[1])=="native-ring-positive-budget-probe") {native_ring_solved_probe(true,true,true);return 0;}
        if(argc>1 && std::string(argv[1])=="native-ring-dynamic-solved-probe") {native_ring_solved_probe(false,true);return 0;}
        if(argc>1 && std::string(argv[1])=="native-ring-dynamic-mixed-probe") {native_ring_solved_probe(true,true);return 0;}
        if(argc>1 && std::string(argv[1])=="native-ring-solved-probe") {native_ring_solved_probe();return 0;}
        if(argc>1 && std::string(argv[1])=="ring-rhs-probe") {finite_ring_rhs_probe();return 0;}
        if(argc>1 && std::string(argv[1])=="ring-parent-probe") {finite_ring_parent_probe();return 0;}
        if(argc>1 && std::string(argv[1])=="ring-native-face") {std::cout<<std::setprecision(17);finite_ring_tree_boundary_contract();return 0;}
        if(argc>1 && std::string(argv[1])=="ring-axis-enclosure") {std::cout<<std::setprecision(17);finite_ring_axis_enclosure_contract();return 0;}
        if(argc>1 && std::string(argv[1])=="ring-enclosure") {std::cout<<std::setprecision(17);finite_ring_enclosure_contract();return 0;}
        if(argc>1 && std::string(argv[1])=="ring-k-interval") {std::cout<<std::setprecision(17);finite_ring_agm_interval_contract();return 0;}
        if(argc>1 && std::string(argv[1])=="boundary-acceptance") {boundary_original_rhs_acceptance_contract();return 0;}
        if(argc>1 && std::string(argv[1])=="boundary-ledger") {boundary_error_ledger_contract();return 0;}
        std::cout<<std::setprecision(17);
        if(argc==3 && std::string(argv[1])=="ring-log1p-probe") {
            const double x=std::strtod(argv[2],nullptr);
            const auto bound=Physical::Gravity::finite_ring_detail::log1p_enclosure({x,x});
            require(Physical::Gravity::finite_ring_detail::interval_finite(bound),
                "log1p probe outside finite domain");
            std::cout<<"{\"lower\":"<<bound.lower<<",\"upper\":"<<bound.upper<<"}\n";return 0;
        }
        if(argc==3 && std::string(argv[1])=="ring-log-probe") {
            const double x=std::stod(argv[2]);
            const auto upper=Physical::Gravity::finite_ring_detail::logarithm_upper(x);
            const auto lower=Physical::Gravity::finite_ring_detail::logarithm_lower(x);
            require(std::isfinite(lower)&&std::isfinite(upper),"log probe outside finite domain");
            std::cout<<"{\"lower\":"<<lower<<",\"upper\":"<<upper<<"}\n";return 0;
        }
        if(argc>=10 && std::string(argv[1])=="ring-enclosure-probe") {
            Physical::Gravity::RingEnclosureControl control{};
            control.maximum_boxes=std::stoull(argv[9]);
            if(argc>10)control.relative_target=std::stod(argv[10]);
            if(argc>11)control.absolute_target=std::stod(argv[11]);
            const auto value=Physical::Gravity::finite_ring_potential_enclosure(
                std::stod(argv[2]),std::stod(argv[3]),std::stod(argv[4]),std::stod(argv[5]),
                std::stod(argv[6]),std::stod(argv[7]),std::stod(argv[8]),
                constants::gravity::cgs::gravitational_constant,control);
            require(value.bound_valid,"probe failed to produce a valid interval");
            std::cout<<"{\"status\":"<<static_cast<int>(value.status)
                <<",\"lower\":"<<value.lower<<",\"upper\":"<<value.upper
                <<",\"value\":"<<value.value<<",\"absolute_error\":"<<value.absolute_error
                <<",\"boxes\":"<<value.leaf_boxes
                <<",\"range_evaluations\":"<<value.range_evaluations
                <<",\"kernel_enclosures\":"<<value.kernel_enclosures
                <<",\"agm_iterations\":"<<value.agm_iterations<<"}\n";return 0;
        }
        if(argc==3 && std::string(argv[1])=="ring-k-probe") {
            const auto value=Physical::Gravity::ring_elliptic_k_interval(std::strtod(argv[2],nullptr));
            std::cout<<"{\"status\":"<<static_cast<int>(value.status)
                <<",\"lower\":"<<value.lower<<",\"upper\":"<<value.upper
                <<",\"iterations\":"<<value.iterations<<"}\n";return 0;
        }
        if(argc>=9 && std::string(argv[1])=="ring-probe") {
            Physical::Gravity::RingQuadratureControl control{};
            if(argc>9)control.relative_estimate_target=std::stod(argv[9]);
            if(argc>10)control.maximum_order=std::stoi(argv[10]);
            if(argc>11)control.maximum_kernel_evaluations=std::stoull(argv[11]);
            const auto value=Physical::Gravity::finite_ring_potential_estimate(
                std::stod(argv[2]),std::stod(argv[3]),std::stod(argv[4]),std::stod(argv[5]),
                std::stod(argv[6]),std::stod(argv[7]),std::stod(argv[8]),
                constants::gravity::cgs::gravitational_constant,control);
            std::cout<<"{\"status\":"<<static_cast<int>(value.status)<<",\"value\":";
            if(std::isfinite(value.value))std::cout<<value.value;else std::cout<<"null";
            std::cout<<",\"estimated_error\":";
            if(std::isfinite(value.estimated_error))std::cout<<value.estimated_error;else std::cout<<"null";
            std::cout<<",\"error_is_certified\":false,\"order\":"<<value.last_order
                <<",\"kernel_evaluations\":"<<value.kernel_evaluations
                <<",\"agm_iterations\":"<<value.agm_iterations<<"}\n";return 0;
        }

        if(argc>1 && std::string(argv[1])=="rz-order-diagnostic") {rz_order_diagnostic();return 0;}
        if(argc>1 && std::string(argv[1])=="coarse-diagnostic") {coarse_mesh_diagnostic();return 0;}
        if (argc>1 && std::string(argv[1])=="rz") { finite_ring_moment_contract(); native_rz_canonical_boundary_stencil_contract(); rz_manufactured(); rz_smooth_convergence(); rz_two_field_green_consistency(); rz_boundary_guard(); return 0; }
        if (argc>1 && std::string(argv[1])=="ring") { finite_ring_moment_contract(); finite_ring_kernel_contract(); rz_boundary_guard(); return 0; }
        if (argc>1 && std::string(argv[1])=="contract") { contract(); coarse_mesh_diagnostic(); radial_convergence(); return 0; }
        if(argc>1 && std::string(argv[1])=="radial") {radial_convergence();return 0;}
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
            native_rz_canonical_boundary_stencil_contract();
            rz_manufactured();rz_smooth_convergence();rz_two_field_green_consistency();
            boundary_convergence(16);isolated_boundary();averaged_source_exactness();
            convergence(2);radial_convergence();curved_manufactured();
            curved_boundary_integral();curved_domain_extension();curved_gauss_law();curved_manufactured(true);curved_manufactured(true,true);
            mixed_cartesian_boundary();curved_mixed_boundary();
            pure_neumann_compatibility();extreme_flux_boundary();boundary_policy_rejection();return 0;
        }
        averaged_source_exactness();
        convergence(argc>1 ? std::stoi(argv[1]) : 3);
        std::cout<<"Composite Poisson analytic validation passed\n";
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
