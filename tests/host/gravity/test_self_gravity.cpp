#include "physics/gravity/GravitySolveTypes.h"
#include "physics/gravity/GravityExecution.h"
#include "amr/elliptic/EllipticMeshAdapter.h"
#include "numerics/multigrid/CompositeMultigrid.h"
#include "physics/gravity/self/SelfGravity.h"
#include "amr/AMRControl.h"
#include "physics/constant/PhysicalConstants.h"
#include <iostream>
#include <limits>
using namespace Physical::Gravity;
using namespace arch;
namespace {
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
template<class F> void rejects(F f,const char* message) { bool failed=false;try{f();}catch(const std::exception&){failed=true;}require(failed,message); }
struct Fixture {
    SimConfig config;
    amr::AMRControl control{64,1};
    std::vector<amr::BlockHandle> handles;
    GravitySolveIdentity identity;
    std::vector<GravityDensityView> views;
    Fixture() {
        config.grid.dim=1;config.grid.nblockx1=4;config.grid.nblockx2=config.grid.nblockx3=0;
        config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="periodic";
        config.amr.lrefinemax=1;
        control.tree->InitRootGrid(config,0);
        reset(1);
    }
    void reset(std::uint64_t epoch) {
        handles.clear();views.clear();identity={};
        identity.topology={epoch};identity.gravitational_constant=config.physics.gravity.G_const;
        identity.operator_revision=identity.boundary_revision=identity.accuracy_revision=1;
        for(int id:control.tree->GetActiveBlocks()) {
            auto& block=control.pool->GetBlock(id);const auto& grid=block.grid;
            handles.push_back({{static_cast<std::uint64_t>(id+1)},{epoch}});
            GravityInputIdentity input{handles.back(),state::StateSlot::Current,{epoch},epoch};
            identity.inputs.push_back(input);
            views.push_back({input,{block.fluid_state.rho.data(),block.fluid_state.rho.size(),amr::native_scalar_layout(grid),grid::FieldMemory::Host,epoch}});
            for(int i=0;i<grid.GetTotalX();++i) {
                const int c=grid.GetIndex(i,0,0);const double x=grid.GetPhysicalCoords(i,0,0).x;
                block.fluid_state.rho[c]=1.+0.1*std::cos(2.*constants::math::pi*x);
                block.fluid_state.eng[c]=10.;
            }
        }
    }
};
void lifecycle() {
    // Nonfinite face forces must fail before publishing a seemingly finite CFL.
    GravityCell cell{0,0,{1.,1.,1.}};
    double sides[6]{},g[3]{},inverse_dt=0.;
    sides[0]=std::numeric_limits<double>::quiet_NaN();
    CellAcceleration{1,1,&cell,sides,g,&inverse_dt}(0);
    require(!std::isfinite(inverse_dt),"nonfinite force hidden by CFL reduction");
    Fixture f;SelfGravity gravity(f.config.physics.gravity);
    rejects([&]{gravity.potential();},"unbound field read accepted");
    gravity.bind(amr::bind_elliptic_mesh(f.control,f.config.grid,f.handles));
    gravity.prepare({f.identity,f.views});
    require(gravity.potential().size()==64,"active mapping extent");
    require(gravity.report().residual<=gravity.report().target,"physical residual");
    auto& block=f.control.pool->GetBlock(f.control.tree->GetActiveBlocks()[0]);
    std::vector<FluidVector> delta(block.grid.GetTotalSize());
    gravity.add_sources_on_patch(delta,block.fluid_state,block.grid,1.);
    for(int i=block.grid.Is();i<block.grid.Ie();++i) {
        const auto c=block.grid.GetIndex(i,0,0);
        const double exact=-2.*f.config.physics.gravity.G_const*0.1*std::sin(2.*constants::math::pi*block.grid.GetPhysicalCoords(i,0,0).x);
        require(std::abs(delta[c].mom_u/block.fluid_state.rho[c]-exact)<0.001*2.*f.config.physics.gravity.G_const*0.1,"force sign/amplitude");
        require(delta[c].eng==0.,"momentum callback double counts energy");
    }
    std::vector<FluidVector> flux(block.grid.GetTotalSize());
    for (auto& face:flux) face.rho=.37;
    gravity.add_flux_work_on_patch(delta,flux,block.fluid_state,block.grid,1.,0);
    for(int i=block.grid.Is();i<block.grid.Ie();++i) {
        const int cell=block.grid.GetIndex(i,0,0);
        require(std::abs(delta[cell].eng-.37*delta[cell].mom_u/block.fluid_state.rho[cell])<1e-22,
                "energy did not use the actual face mass flux");
    }
    rejects([&]{gravity.add_sources_on_patch(delta,block.state_next,block.grid,1.);},"wrong slot accepted");
    gravity.invalidate();rejects([&]{gravity.potential();},"invalidated field read");
    gravity.prepare({f.identity,f.views});
    auto bad_views=f.views; bad_views.back().density.layout.stride[0]=2;
    rejects([&]{gravity.prepare({f.identity,bad_views});},"wrong scalar layout accepted");
    auto wrong_operator=f.identity;wrong_operator.operator_revision=2;
    rejects([&]{gravity.prepare({wrong_operator,f.views});},"unbound operator revision accepted");
    auto wrong=f.identity;wrong.inputs.back().version.value++;
    rejects([&]{gravity.prepare({wrong,f.views});},"nonfirst dependency mismatch accepted");
    rejects([&]{gravity.potential();},"failed solve left old publication usable");
    gravity.prepare({f.identity,f.views});
    block.fluid_state.rho[block.grid.Is()]=std::numeric_limits<double>::quiet_NaN();
    rejects([&]{gravity.prepare({f.identity,f.views});},"NaN density accepted");
    rejects([&]{gravity.potential();},"NaN failure publication");
    f.reset(1);
    // The bounded coarse LU now solves this 64-cell periodic root in one
    // Krylov step at the ordinary tolerance. Demand a residual below double
    // precision roundoff to exercise the failed-solve publication contract.
    auto controls=f.config.physics.gravity;controls.max_cycles=1;
    controls.relative_tolerance=1e-20;controls.absolute_tolerance=0.;
    SelfGravity limited(controls);limited.bind(amr::bind_elliptic_mesh(f.control,f.config.grid,f.handles));
    bool convergence_failed=false;
    try { limited.prepare({f.identity,f.views}); }
    catch (const std::runtime_error& error) {
        convergence_failed=std::string_view(error.what()).find("Self-gravity Poisson solve failed:")!=std::string_view::npos;
    }
    require(convergence_failed,"failed convergence accepted");
    rejects([&]{limited.potential();},"nonconvergence publication");
    // Actual tree refinement, coarsening and identity turnover, not a synthetic
    // Cartesian replacement for the AMR adapter.
    f.config.amr.refine_threshold=0.01;f.config.amr.refine_on_rho=true;
    auto transaction=f.control.tree->PrepareRegrid(f.config,{}, {}, [&]{
        for(int id:f.control.tree->GetActiveBlocks()) f.control.pool->GetBlock(id).refine_flag=(id==0?1:0);
    });
    require(transaction.topology_changed(),"refine witness missing");
    std::vector<amr::BlockHandle> next;
    for(int id:transaction.proposed_active_blocks()) next.push_back({{static_cast<std::uint64_t>(id+1)},{2}});
    transaction.BuildMigrationPlans(f.handles,next,{1,{1},{2}});
    transaction.ExecuteMigration();transaction.ActivateForFinalization();transaction.PublishNoexcept();transaction.ReleaseRetired();
    gravity.invalidate();f.reset(2);
    gravity.bind(amr::bind_elliptic_mesh(f.control,f.config.grid,f.handles));
    rejects([&]{gravity.prepare({wrong,f.views});},"old topology accepted");
    gravity.prepare({f.identity,f.views});
    require(gravity.potential().size()==80,"refined leaf layout");
    f.config.amr.refine_on_rho=false;f.config.amr.derefine_threshold=1.;f.config.amr.refine_threshold=1.;
    require(f.control.tree->Regrid(f.config),"coarsen witness missing");
    gravity.invalidate();f.reset(3);gravity.bind(amr::bind_elliptic_mesh(f.control,f.config.grid,f.handles));
    gravity.prepare({f.identity,f.views});require(gravity.potential().size()==64,"coarsened leaf layout");
    require(!f.control.tree->Regrid(f.config),"no-change witness missing");
    for(int id:f.control.tree->GetActiveBlocks()) std::fill(f.control.pool->GetBlock(id).fluid_state.rho.begin(),f.control.pool->GetBlock(id).fluid_state.rho.end(),1e-100);
    gravity.prepare({f.identity,f.views});
    require(gravity.report().cycles==0,"constant tiny density is not zero source");
    for(double phi:gravity.potential()) require(phi==0.,"constant density generated gravity");
    for(int id:f.control.tree->GetActiveBlocks()) {
        auto& b=f.control.pool->GetBlock(id);const auto& grid=b.grid;
        for(int i=grid.Is();i<grid.Ie();++i) b.fluid_state.rho[grid.GetIndex(i,0,0)]=1e-100*(1.+0.1*std::cos(2.*constants::math::pi*grid.GetPhysicalCoords(i,0,0).x));
    }
    gravity.prepare({f.identity,f.views});
    require(gravity.report().rhs_rms>0. && gravity.report().residual<=gravity.report().target,"tiny nonuniform physical source lost");
    double maximum=0.;for(double phi:gravity.potential()) maximum=std::max(maximum,std::abs(phi/1e-100));
    require(maximum>0.02*f.config.physics.gravity.G_const,"tiny physical gravity was clamped away");

}
void native_components() {
    for(int dimension:{2,3}) {
        SimConfig config; config.grid.dim=dimension;
        config.grid.nblockx1=config.grid.nblockx2=2;
        config.grid.nblockx3=dimension==3?2:0;
        amr::AMRControl control(12,dimension);control.tree->InitRootGrid(config,0);
        GravitySolveIdentity identity;identity.topology={1};identity.gravitational_constant=config.physics.gravity.G_const;
        identity.operator_revision=identity.boundary_revision=identity.accuracy_revision=1;
        std::vector<amr::BlockHandle> handles;std::vector<GravityDensityView> views;
        for(int id:control.tree->GetActiveBlocks()) {
            auto& block=control.pool->GetBlock(id);const auto& grid=block.grid;
            handles.push_back({{static_cast<std::uint64_t>(id+1)},{1}});
            const GravityInputIdentity input{handles.back(),state::StateSlot::Current,{1},1};identity.inputs.push_back(input);
            views.push_back({input,{block.fluid_state.rho.data(),block.fluid_state.rho.size(),amr::native_scalar_layout(grid),grid::FieldMemory::Host,1}});
            for(int k=grid.Ks();k<grid.Ke();++k) for(int j=grid.Js();j<grid.Je();++j) for(int i=grid.Is();i<grid.Ie();++i) {
                const auto point=grid.GetPhysicalCoords(i,j,k);const double position[]{point.x,point.y,point.z};
                double mode=1.;for(int a=0;a<dimension;++a) mode*=std::cos(2.*constants::math::pi*position[a]+.17*(a+1));
                block.fluid_state.rho[grid.GetIndex(i,j,k)]=1.+.1*mode;
            }
        }
        SelfGravity gravity(config.physics.gravity);
        gravity.bind(amr::bind_elliptic_mesh(control,config.grid,handles));gravity.prepare({identity,views});
        double error[3]{},scale[3]{};std::size_t flat=0;
        for(int id:control.tree->GetActiveBlocks()) {
            const auto& block=control.pool->GetBlock(id);const auto& grid=block.grid;
            std::vector<FluidVector> delta(grid.GetTotalSize());gravity.add_sources_on_patch(delta,block.fluid_state,grid,1.);
            for(int k=grid.Ks();k<grid.Ke();++k) for(int j=grid.Js();j<grid.Je();++j) for(int i=grid.Is();i<grid.Ie();++i,++flat) {
                const auto point=grid.GetPhysicalCoords(i,j,k);const double position[]{point.x,point.y,point.z};
                const int cell=grid.GetIndex(i,j,k);const double momentum[]{delta[cell].mom_u,delta[cell].mom_v,delta[cell].mom_w};
                for(int axis=0;axis<dimension;++axis) {
                    double exact=-.2*config.physics.gravity.G_const/dimension;
                    for(int a=0;a<dimension;++a) exact*=a==axis?std::sin(2.*constants::math::pi*position[a]+.17*(a+1)):std::cos(2.*constants::math::pi*position[a]+.17*(a+1));
                    const double force=momentum[axis]/block.fluid_state.rho[cell];
                    error[axis]+=(force-exact)*(force-exact);scale[axis]+=exact*exact;
                    require(std::abs(force-gravity.acceleration()[axis][flat])<1e-22,"native component/output mapping differs");
                }
            }
        }
        for(int a=0;a<dimension;++a) require(std::sqrt(error[a]/scale[a])<.01,"native transverse force sign/amplitude");
        std::cout<<dimension<<"D native vector components passed\n";
    }
}

}
int main() { try {lifecycle();native_components();std::cout<<"Self-gravity lifecycle validation passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
