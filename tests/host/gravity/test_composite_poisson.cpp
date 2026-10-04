#include "numerics/multigrid/CompositeMultigrid.h"
#include "physics/gravity/GravityBoundary.h"
#include "physics/constant/PhysicalConstants.h"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <limits>
#include <type_traits>
#include <cstdlib>
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
        if(argc>1 && std::string(argv[1])=="ring-k-interval") {std::cout<<std::setprecision(17);finite_ring_agm_interval_contract();return 0;}
        if(argc>1 && std::string(argv[1])=="boundary-acceptance") {boundary_original_rhs_acceptance_contract();return 0;}
        if(argc>1 && std::string(argv[1])=="boundary-ledger") {boundary_error_ledger_contract();return 0;}
        std::cout<<std::setprecision(17);
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

        if(argc>1 && std::string(argv[1])=="coarse-diagnostic") {coarse_mesh_diagnostic();return 0;}
        if (argc>1 && std::string(argv[1])=="rz") { finite_ring_moment_contract(); rz_manufactured(); rz_boundary_guard(); return 0; }
        if (argc>1 && std::string(argv[1])=="ring") { finite_ring_moment_contract(); finite_ring_kernel_contract(); rz_boundary_guard(); return 0; }
        if (argc>1 && std::string(argv[1])=="contract") { contract(); coarse_mesh_diagnostic(); radial_convergence(); return 0; }
        if(argc>1 && std::string(argv[1])=="radial") {radial_convergence();return 0;}
        if(argc>1 && std::string(argv[1])=="curved") {curved_manufactured();curved_boundary_integral();return 0;}
        if(argc>1 && std::string(argv[1])=="singular") {curved_manufactured(true);curved_manufactured(true,true);return 0;}
        if(argc>1 && std::string(argv[1])=="gauss") {curved_gauss_law();return 0;}
        if(argc>1 && std::string(argv[1])=="domain") {curved_domain_extension();return 0;}
        if(argc>1 && std::string(argv[1])=="boundary") {boundary_convergence();isolated_boundary();return 0;}
        if(argc>1 && std::string(argv[1])=="ci") {
            boundary_convergence(16);isolated_boundary();averaged_source_exactness();
            convergence(2);radial_convergence();curved_manufactured();
            curved_boundary_integral();curved_domain_extension();curved_gauss_law();curved_manufactured(true);curved_manufactured(true,true);return 0;
        }
        averaged_source_exactness();
        convergence(argc>1 ? std::stoi(argv[1]) : 3);
        std::cout<<"Composite Poisson analytic validation passed\n";
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
