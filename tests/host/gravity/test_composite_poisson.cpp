#include "numerics/multigrid/CompositeMultigrid.h"
#include "physics/gravity/GravityBoundary.h"
#include "physics/constant/PhysicalConstants.h"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <limits>
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
int main(int argc,char** argv) {
    try {
        std::cout<<std::setprecision(17);
        if (argc>1 && std::string(argv[1])=="contract") { contract(); radial_convergence(); return 0; }
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
