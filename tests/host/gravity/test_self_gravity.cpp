#include "physics/gravity/GravitySolveTypes.h"
#include "physics/gravity/GravityExecution.h"
#include "amr/elliptic/EllipticMeshAdapter.h"
#include "numerics/multigrid/CompositeMultigrid.h"
#include "physics/gravity/self/SelfGravity.h"
#include "physics/gravity/self/GravityUserBoundary.h"
#include "physics/gravity/self/GravityWorkspace.h"
#include "amr/AMRControl.h"
#include "physics/boundary/UserBoundary.h"
#include "physics/constant/PhysicalConstants.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "driver/stages/GravityStage.h"
#include "physics/boundary/PhysicalBoundaryHandler.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <filesystem>
#include <fstream>
#include "core/config/ControlRelations.h"
#include <iostream>
#include <iomanip>
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
        identity.topology={epoch};identity.gravitational_constant=arch::constants::gravity::cgs::gravitational_constant;
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
/** Count actual delegated work, so malformed identity cannot hide a gather. */
class IdentityExecution final : public GravityExecution {
    std::shared_ptr<GravityExecution> owner_=make_host_gravity_execution();
public:
    std::size_t work_count=0;
    std::shared_ptr<multigrid::CompositeExecution> numeric() const override {return owner_->numeric();}
    void run(const GravityWork& work) override {++work_count;owner_->run(work);}
};
/** Original service and native four-block density: reject before computation,
 * retire the old field, then recover using the valid original request. */
void request_identity_preflight() {
    Fixture f;auto execution=std::make_shared<IdentityExecution>();
    SelfGravity gravity(f.config.physics.gravity);gravity.set_execution(execution);
    gravity.bind(amr::bind_elliptic_mesh(f.control,f.config.grid,f.handles));
    for(int lane=0;lane<5;++lane) {
        gravity.prepare({f.identity,f.views});
        auto bad=f.identity;auto views=f.views;
        if(lane==0)bad.input_time=std::numeric_limits<double>::quiet_NaN();
        if(lane==1)bad.input_time=std::numeric_limits<double>::infinity();
        if(lane==2)bad.inputs.back().version={0};
        if(lane==3)bad.inputs.back().storage_generation=0;
        if(lane==4)bad.inputs.back().slot=static_cast<state::StateSlot>(255);
        views.back().identity=bad.inputs.back();
        views.back().density.storage_generation=bad.inputs.back().storage_generation;
        const auto before=execution->work_count;
        rejects([&]{gravity.prepare({bad,views});},"malformed request identity accepted");
        require(execution->work_count==before,"malformed identity executed gravity work before rejection");
        rejects([&]{gravity.potential();},"identity rejection retained old publication");
    }
    gravity.prepare({f.identity,f.views});
    require(gravity.potential().size()==64,"valid request did not recover after identity rejection");
    std::cout<<"GRAVITY_REQUEST_PREFLIGHT_PASS lanes=5 native_blocks=4 work_before_rejection=0 recovery=1\n";
}
/** The real field owner requires all actual patch momentum/work consumers.
 * This checks a four-patch source receipt, not a substitute force model.
 */
void host_consumption_receipt() {
    Fixture f;SelfGravity gravity(f.config.physics.gravity);
    gravity.bind(amr::bind_elliptic_mesh(f.control,f.config.grid,f.handles));
    rejects([&]{gravity.begin_host_stage_consumption(0.);},"zero Host source interval accepted");
    gravity.begin_host_stage_consumption(.125);
    rejects([&]{gravity.begin_host_stage_consumption(.125);},"duplicate Host source receipt accepted");
    gravity.prepare({f.identity,f.views});
    rejects([&]{gravity.require_host_stage_consumption(f.identity);},
        "publication without real source/work consumption accepted");
    const auto& first=f.control.pool->GetBlock(f.control.tree->GetActiveBlocks().front());
    std::vector<FluidVector> rejected_delta(first.grid.GetTotalSize()),rejected_flux(first.grid.GetTotalSize());
    for(auto& face:rejected_flux)face.rho=.37;
    for(double wrong_dt:{0.,.25,std::numeric_limits<double>::quiet_NaN()}) {
        rejects([&]{gravity.add_sources_on_patch(rejected_delta,first.fluid_state,first.grid,wrong_dt);},
            "wrong actual source interval accepted");
        rejects([&]{gravity.add_flux_work_on_patch(rejected_delta,rejected_flux,
            first.fluid_state,first.grid,wrong_dt,0);},"wrong actual work interval accepted");
        for(const auto& value:rejected_delta)
            require(value.rho==0.&&value.mom_u==0.&&value.mom_v==0.
                &&value.mom_w==0.&&value.eng==0.,"wrong interval mutated the source increment");
    }
    auto consume=[&](bool omit_last_work) {
        const auto& active=f.control.tree->GetActiveBlocks();
        for(std::size_t b=0;b<active.size();++b) {
            const auto& block=f.control.pool->GetBlock(active[b]);
            std::vector<FluidVector> delta(block.grid.GetTotalSize()),flux(block.grid.GetTotalSize());
            for(auto& face:flux)face.rho=.37;
            gravity.add_sources_on_patch(delta,block.fluid_state,block.grid,.125);
            if(!omit_last_work||b+1!=active.size())
                gravity.add_flux_work_on_patch(delta,flux,block.fluid_state,block.grid,.125,0);
        }
    };
    consume(true);
    rejects([&]{gravity.require_host_stage_consumption(f.identity);},
        "nonfirst patch missing flux work accepted");
    auto& last=f.control.pool->GetBlock(f.control.tree->GetActiveBlocks().back());
    std::vector<FluidVector> delta(last.grid.GetTotalSize()),flux(last.grid.GetTotalSize());
    gravity.add_flux_work_on_patch(delta,flux,last.fluid_state,last.grid,.125,0);
    gravity.require_host_stage_consumption(f.identity);
    auto wrong=f.identity;wrong.inputs.back().version.value++;
    rejects([&]{gravity.require_host_stage_consumption(wrong);},"wrong source version accepted");
    rejects([&]{gravity.add_flux_work_on_patch(delta,flux,last.fluid_state,last.grid,.125,0);},
        "duplicate real patch work accepted");
    gravity.end_host_stage_consumption();
    rejects([&]{gravity.require_host_stage_consumption(f.identity);},"closed source receipt accepted");
    gravity.invalidate();
    std::cout<<"GRAVITY_HOST_CONSUMPTION_PASS patches=4 missing_nonfirst=1 duplicate=1 exact_identity=1\n";
}
/** The actual GravityStage holds rows privately, discards a failed prefix and
 * publishes a complete Euler source receipt only after explicit commit/flush.
 * This lifecycle test does not qualify new geometry or continuum accuracy.
 */
void host_stage_diagnostic_journal() {
    Fixture f;SpeciesManager species;species.add_species("X",1.,1.,1.4,1.);
    const auto directory=std::filesystem::path("self-gravity-host-journal-fixture-"
        +std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    require(std::filesystem::create_directory(directory),"Host journal output directory already exists");
    f.config.io.out_dir=directory.string();
    RunState start;start.repairs.reset(1);
    SimulationController controller(f.config,start);BCHandler boundary(f.config);
    driver::DriverRuntime runtime(f.control,boundary,f.config,species,controller);
    runtime.initialize_topology();auto context=runtime.stage_context();
    SelfGravity gravity(f.config.physics.gravity);
    driver::GravityStage stage(runtime,&gravity);
    require(stage.supports_host_macro_step_journal(),"real Host journal capability absent");
    stage.flush_committed_diagnostics();
    const auto read_rows=[&] {
        std::ifstream input(directory/"gravity_solves.tsv");
        return std::string(std::istreambuf_iterator<char>(input),{});
    };
    const auto original=read_rows();
    const auto& descriptor=scheduler::supported_hydro_time_plan(scheduler::HydroMethod::Euler).stages.front();
    const auto prepare=[&] {
        stage.prepare({scheduler::HydroMethod::Euler,descriptor,runtime.handles(),
            state::ExecutionSide::Host,context.ledger,2.,.125});
    };
    const auto consume=[&] {
        for(int id:f.control.tree->GetActiveBlocks()) {
            auto& block=f.control.pool->GetBlock(id);
            std::vector<FluidVector> delta(block.grid.GetTotalSize()),flux(block.grid.GetTotalSize());
            for(auto& face:flux)face.rho=.37;
            gravity.add_sources_on_patch(delta,block.fluid_state,block.grid,.125);
            gravity.add_flux_work_on_patch(delta,flux,block.fluid_state,block.grid,.125,0);
        }
    };
    stage.begin_macro_step();prepare();
    rejects([&]{stage.accept(descriptor);},"unconsumed real GravityStage was accepted");
    rejects([&]{stage.flush_committed_diagnostics();},"tentative source diagnostics flushed");
    stage.discard_macro_step();
    require(read_rows()==original,"discarded source prefix changed accepted diagnostics");
    rejects([&]{gravity.potential();},"discard retained readable candidate gravity");
    stage.begin_macro_step();prepare();consume();
    auto wrong=descriptor;wrong.flux_register_weight=.5;
    rejects([&]{stage.accept(wrong);},"different source descriptor accepted");
    stage.accept(descriptor);
    require(read_rows()==original,"accepted stage row leaked before macro-step commit");
    stage.commit_macro_step();
    require(read_rows()==original,"numerical commit performed fallible file publication");
    stage.flush_committed_diagnostics();
    const auto accepted=read_rows();
    require(accepted.size()>original.size()
        &&std::count(accepted.begin(),accepted.end(),'\n')
            ==std::count(original.begin(),original.end(),'\n')+1,
        "committed actual source receipt was not published exactly once");
    stage.flush_committed_diagnostics();
    require(read_rows()==accepted,"source diagnostics flush repeated a committed row");
    stage.begin_macro_step();prepare();consume();stage.accept(descriptor);
    stage.discard_macro_step();stage.flush_committed_diagnostics();
    require(read_rows()==accepted,"late failed source attempt erased accepted history or leaked a prefix");
    std::filesystem::remove_all(directory);
    std::cout<<"GRAVITY_HOST_DIAGNOSTIC_JOURNAL_PASS actual_runtime=1 actual_source=1 no_prefix=1 flush_once=1\n";
}
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
        const double exact=-2.*arch::constants::gravity::cgs::gravitational_constant*0.1*std::sin(2.*constants::math::pi*block.grid.GetPhysicalCoords(i,0,0).x);
        require(std::abs(delta[c].mom_u/block.fluid_state.rho[c]-exact)<0.001*2.*arch::constants::gravity::cgs::gravitational_constant*0.1,"force sign/amplitude");
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
    f.config.amr.refine_threshold=0.01;f.config.amr.derefine_threshold=.005;
    f.config.amr.refine_on_rho=true;
    require(arch::config::relations::CurvatureThresholds(f.config.amr.refine_threshold,
        f.config.amr.derefine_threshold),"refine fixture violates Core thresholds");
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
    // Keep DENS active and the original refine ceiling. The old equal pair
    // (1,1) was forbidden by Core; this legal stricter coarsen pair exercises
    // actual ordinary indicators rather than disabling every channel.
    f.config.amr.refine_on_rho=true;f.config.amr.derefine_threshold=.5;
    f.config.amr.refine_threshold=1.;
    require(arch::config::relations::CurvatureThresholds(f.config.amr.refine_threshold,
        f.config.amr.derefine_threshold),"coarsen fixture violates Core thresholds");
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
    require(maximum>0.02*arch::constants::gravity::cgs::gravitational_constant,"tiny physical gravity was clamped away");

}
/** One-block-per-root line fixture for the explicit gravity boundary names. */
struct Line {
    SimConfig config; amr::AMRControl control; std::vector<amr::BlockHandle> handles;
    GravitySolveIdentity identity; std::vector<GravityDensityView> views; double length=1.;
    Line(const std::string& faces,const std::string& boundary,int cells=64):control(cells,1) {
        config.grid.dim=1;config.grid.nblockx1=4;config.grid.nblockx2=config.grid.nblockx3=0;
        config.grid.x1l_boundary_type=config.grid.x1r_boundary_type=faces;
        config.grid.x1_min=0.;config.grid.x1_max=1.;
        config.physics.gravity.boundary=boundary;
        control.tree->InitRootGrid(config,0);
        identity.topology={1};identity.gravitational_constant=constants::gravity::cgs::gravitational_constant;
        identity.operator_revision=identity.boundary_revision=identity.accuracy_revision=1;
        for(int id:control.tree->GetActiveBlocks()) {
            auto& block=control.pool->GetBlock(id);const auto& grid=block.grid;
            handles.push_back({{static_cast<std::uint64_t>(id+1)},{1}});
            const GravityInputIdentity input{handles.back(),state::StateSlot::Current,{1},1};
            identity.inputs.push_back(input);
            views.push_back({input,{block.fluid_state.rho.data(),block.fluid_state.rho.size(),
                amr::native_scalar_layout(grid),grid::FieldMemory::Host,1}});
            for(int k=grid.Ks();k<grid.Ke();++k)for(int j=grid.Js();j<grid.Je();++j)
                for(int i=grid.Is();i<grid.Ie();++i)block.fluid_state.rho[grid.GetIndex(i,j,k)]=1.;
        }
    }
};
/** RMS of a per-cell error against the matching exact field. */
void line_error(Line& f,SelfGravity& gravity,const std::function<double(double)>& exact,
    const std::function<double(double)>& exact_force,const char* potential_what,const char* force_what,
    double shift=0.) {
    double error=0.,scale=0.,force=0.,fscale=0.;std::size_t flat=0;
    for(int id:f.control.tree->GetActiveBlocks()) {
        auto& block=f.control.pool->GetBlock(id);const auto& grid=block.grid;
        std::vector<FluidVector> delta(grid.GetTotalSize());
        gravity.add_sources_on_patch(delta,block.fluid_state,grid,1.);
        for(int i=grid.Is();i<grid.Ie();++i,++flat) {
            const double x=grid.GetPhysicalCoords(i,0,0).x,value=exact(x)+shift,rate=exact_force(x);
            const int cell=grid.GetIndex(i,0,0);
            error+=(gravity.potential()[flat]-value)*(gravity.potential()[flat]-value);scale+=value*value;
            force+=(delta[cell].mom_u-rate)*(delta[cell].mom_u-rate);fscale+=rate*rate;
        }
    }
    require(std::sqrt(error/scale)<1e-7,potential_what);
    require(std::sqrt(force/fscale)<1e-6,force_what);
}
/** Constant density with homogeneous zero Dirichlet faces reproduces the exact
 *  quadratic potential and force of the Poisson problem. */
void dirichlet_quadratic() {
    Line f("outflow","dirichlet");
    SelfGravity gravity(f.config.physics.gravity);
    gravity.bind(amr::bind_elliptic_mesh(f.control,f.config.grid,f.handles));
    gravity.prepare({f.identity,f.views});
    const double G=constants::gravity::cgs::gravitational_constant,L=f.length;
    const auto exact=[&](double x){return 2.*constants::math::pi*G*(x*x-L*x);};
    const auto force=[&](double x){return -2.*constants::math::pi*G*(2.*x-L);};
    line_error(f,gravity,exact,force,"Dirichlet quadratic potential","Dirichlet quadratic force");
    std::cout<<"Dirichlet quadratic passed\n";
}
/** A positive mass on a homogeneous pure-Neumann boundary must fail the shared
 *  Gauss-law compatibility check instead of being mean-projected. */
void neumann_compatibility() {
    Line f("outflow","neumann");
    SelfGravity gravity(f.config.physics.gravity);
    gravity.bind(amr::bind_elliptic_mesh(f.control,f.config.grid,f.handles));
    rejects([&]{gravity.prepare({f.identity,f.views});},"positive mass on homogeneous Neumann accepted");
    rejects([&]{gravity.potential();},"failed Neumann solve published a field");
    std::cout<<"Neumann Gauss-law rejection passed\n";
}
/** Periodic/nonperiodic mix: the periodic axis keeps no physical face and the
 *  Dirichlet axis keeps the operator nonsingular. */
void mixed_periodic() {
    SimConfig config;config.grid.dim=2;config.grid.nblockx1=config.grid.nblockx2=2;config.grid.nblockx3=0;
    config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="periodic";
    config.grid.x2l_boundary_type=config.grid.x2r_boundary_type="outflow";
    config.physics.gravity.boundary="dirichlet";
    amr::AMRControl control(16,2);control.tree->InitRootGrid(config,0);
    GravitySolveIdentity identity;identity.topology={1};identity.gravitational_constant=constants::gravity::cgs::gravitational_constant;
    identity.operator_revision=identity.boundary_revision=identity.accuracy_revision=1;
    std::vector<amr::BlockHandle> handles;std::vector<GravityDensityView> views;
    for(int id:control.tree->GetActiveBlocks()) {
        auto& block=control.pool->GetBlock(id);const auto& grid=block.grid;
        handles.push_back({{static_cast<std::uint64_t>(id+1)},{1}});
        const GravityInputIdentity input{handles.back(),state::StateSlot::Current,{1},1};identity.inputs.push_back(input);
        views.push_back({input,{block.fluid_state.rho.data(),block.fluid_state.rho.size(),
            amr::native_scalar_layout(grid),grid::FieldMemory::Host,1}});
        std::fill(block.fluid_state.rho.begin(),block.fluid_state.rho.end(),1.);
    }
    SelfGravity gravity(config.physics.gravity);
    gravity.bind(amr::bind_elliptic_mesh(control,config.grid,handles));
    gravity.prepare({identity,views});
    require(gravity.report().residual<=gravity.report().target,"mixed periodic/Dirichlet residual");
    std::cout<<"mixed periodic/dirichlet passed\n";
}
/** User Robin data, a datum-only time change and the coercive/topology rejections. */
void user_robin_time() {
    SimConfig config;config.grid.dim=1;config.grid.nblockx1=4;config.grid.nblockx2=config.grid.nblockx3=0;
    config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="outflow";
    config.grid.x1_min=0.;config.grid.x1_max=1.;
    config.physics.gravity.boundary="user";
    SpeciesManager species;
    const double G=constants::gravity::cgs::gravitational_constant,L=1.,A=2.*constants::math::pi*G,B=-2.*constants::math::pi*G*L;
    const auto exact=[&](double x){return A*x*x+B*x;};
    const auto force=[&](double x){return -(2.*A*x+B);};
    double shift=0.;
    arch::boundary::ResolvedUserBoundaries resolved;
    resolved.gravity=[&](const arch::boundary::GravityBoundaryContext& ctx){
        const double x=ctx.point.x,n=ctx.cartesian_normal[0];
        return arch::boundary::GravityBoundaryData::Robin(1.,1.,exact(x)+n*(2.*A*x+B)+shift);
    };
    arch::boundary::ScopedUserBoundarySelection scope(resolved,config,species);
    Line f("outflow","user");
    SelfGravity gravity(f.config.physics.gravity);
    gravity.bind(amr::bind_elliptic_mesh(f.control,f.config.grid,f.handles));
    gravity.prepare({f.identity,f.views});
    line_error(f,gravity,exact,force,"Robin quadratic potential","Robin quadratic force");
    shift=2.5;f.identity.input_time=1.0;
    gravity.prepare({f.identity,f.views});
    line_error(f,gravity,exact,force,"time-varying datum potential","time-varying datum force",shift);
    std::cout<<"user Robin/time-varying passed\n";
}
/** Reject a missing selection, a non-coercive Robin side and a periodic side
 *  that disagrees with the AMR topology. */
void user_rejections() {
    SimConfig config;config.grid.dim=1;config.grid.nblockx1=4;config.grid.nblockx2=config.grid.nblockx3=0;
    config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="outflow";
    config.physics.gravity.boundary="user";
    rejects([&]{SelfGravity missing(config.physics.gravity);},"user boundary without a selection accepted");
    SpeciesManager species;
    arch::boundary::ResolvedUserBoundaries noncoercive;
    noncoercive.gravity=[](const arch::boundary::GravityBoundaryContext&){return arch::boundary::GravityBoundaryData::Robin(1.,0.,0.);};
    arch::boundary::ScopedUserBoundarySelection bad_scope(noncoercive,config,species);
    Line f("outflow","user");
    SelfGravity noncoercive_gravity(config.physics.gravity);
    rejects([&]{noncoercive_gravity.bind(amr::bind_elliptic_mesh(f.control,f.config.grid,f.handles));},
        "non-coercive Robin side accepted");
}
void user_periodic_mismatch() {
    SimConfig config;config.grid.dim=1;config.grid.nblockx1=4;config.grid.nblockx2=config.grid.nblockx3=0;
    config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="outflow";
    config.physics.gravity.boundary="user";
    SpeciesManager species;
    arch::boundary::ResolvedUserBoundaries periodic;
    periodic.gravity=[](const arch::boundary::GravityBoundaryContext&){return arch::boundary::GravityBoundaryData::Periodic();};
    arch::boundary::ScopedUserBoundarySelection scope(periodic,config,species);
    Line f("outflow","user");
    SelfGravity gravity(config.physics.gravity);
    rejects([&]{gravity.bind(amr::bind_elliptic_mesh(f.control,f.config.grid,f.handles));},
        "periodic user side disagreed with the AMR topology");
    std::cout<<"user rejections passed\n";
}

/** Existing bounded native source, now consumed through the actual executor.
 * This abstract numerical-domain dependency is not full Runtime all-block proof.
 */
void ring_execution_identity() {
    elliptic::CartesianMesh base;base.dimension=2;
    base.cells={4,4,1};base.spacing={.25,.25,1.};base.origin={0.,-.5,0.};
    base.geometry=elliptic::Geometry::Cylindrical;
    base.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
    std::vector<elliptic::CompositeCell> cells;
    for(int j=0;j<4;++j)for(int i=0;i<4;++i)cells.push_back({0,{i,j,0}});
    elliptic::CompositePoisson op(base,cells,elliptic::BoundaryKind::CurvilinearIsolated);
    GravityBoundary tree(op,{9});
    // This genuine 4x4 source has 16 leaves, four intermediate parents and
    // one root. Its surface has four radial outer faces and four per z end;
    // the axis has no nonzero-area exterior face. Check the actual producer
    // records rather than counting all interior interfaces as boundary work.
    std::size_t leaves=0,parents=0,exterior=0;
    std::array<std::size_t,4> exterior_by_side{};
    for(const auto& node:tree.nodes()) {
        if(node.cell>=0)++leaves;
        else ++parents;
    }
    for(const auto& face:op.faces())if(face.boundary_side>=0) {
        require(face.boundary_side<4,"tiny ring published an inactive boundary side");
        ++exterior;++exterior_by_side[face.boundary_side];
    }
    require(leaves==16&&parents==5&&exterior==12
        &&exterior_by_side==std::array<std::size_t,4>{0,4,4,4},
        "tiny ring source tree or physical surface partition changed");
    // This finite ceiling covers a full traversal and an extra failed-quartet
    // attempt at each parent for each of the twelve real surface observers.
    // No density update is needed to establish this geometry-only resource.
    const auto traversal_bound=tree.full_ring_traversal_work_bound(op);
    require(traversal_bound==312,"actual ring work ceiling did not cover the known tiny topology");
    auto shifted_base=base;shifted_base.origin[0]=.5;
    elliptic::CompositePoisson shifted(shifted_base,cells,
        elliptic::BoundaryKind::CurvilinearIsolated);
    rejects([&]{(void)tree.full_ring_traversal_work_bound(shifted);},
        "ring resource policy accepted another actual source geometry");
    require(tree.full_ring_traversal_work_bound(op)==traversal_bound,
        "rejected foreign ring geometry mutated the bound resource policy");
    GravitySolveIdentity identity;
    identity.topology={9};identity.gravitational_constant=constants::gravity::cgs::gravitational_constant;
    identity.operator_revision=identity.boundary_revision=identity.accuracy_revision=1;
    identity.inputs.push_back({{{1},{9}},state::StateSlot::Current,{1},1});
    std::vector<double> density(op.size(),1.);tree.update(density,identity);
    RingBoundaryControl control;control.face_absolute_target=1.e-18;
    control.maximum_boxes_per_leaf=65536;
    control.maximum_leaf_evaluations=traversal_bound;
    RingBoundaryEvaluation result;auto execution=make_host_gravity_execution();
    execution->run(EvaluateRingBoundary{&tree,&op,&identity,&control,&result});
    require(result.status==RingBoundaryStatus::Bounded && result.source==identity
        && result.source_generation>0 && result.values.size()==op.faces().size(),
        "typed ring executor lost bounded source identity");
    tree.require_current_ring(op,result);
    require(result.leaf_evaluations+result.parent_evaluations<=traversal_bound
        &&result.represented_leaf_evaluations==exterior*leaves,
        "bounded ring traversal omitted source coverage or exceeded its actual resource policy");
    require(result.lower.size()==op.faces().size()&&result.upper.size()==op.faces().size()
        &&result.errors.size()==op.faces().size(),"bounded ring omitted full face enclosure arrays");
    for(std::size_t f=0;f<op.faces().size();++f)if(op.faces()[f].boundary_side>=0) {
        require(std::isfinite(result.lower[f])&&std::isfinite(result.upper[f])
            &&std::isfinite(result.values[f])&&result.lower[f]<=result.values[f]
            &&result.values[f]<=result.upper[f]
            &&result.errors[f].quality==elliptic::BoundaryErrorQuality::CertifiedAbsolute
            &&std::isfinite(result.errors[f].absolute_error)
            &&result.errors[f].absolute_error<=control.face_absolute_target,
            "bounded traversal failed an actual surface enclosure at the original target");
    }
    std::cout<<"RING_FULL_TRAVERSAL_ENGINEERING_PASS leaves="<<leaves
        <<" parents="<<parents<<" surface_faces="<<exterior<<" work_bound="<<traversal_bound
        <<" consumed="<<result.leaf_evaluations+result.parent_evaluations
        <<" complete_surface=1 original_target=1 physical_qualification=0\n";
    // Mathematical memo reuse cannot retire current source checks or tree work caps.
    const auto first=result;
    require(first.memo_admissions>0,"bounded original ring intervals were not admitted");
    execution->run(EvaluateRingBoundary{&tree,&op,&identity,&control,&result});
    require(result.memo_hits>0&&result.memo_misses<=first.memo_misses
        &&result.range_evaluations<=first.range_evaluations
        &&result.kernel_enclosures<=first.kernel_enclosures&&result.agm_iterations<=first.agm_iterations
        &&result.lower==first.lower&&result.upper==first.upper&&result.values==first.values
        &&result.leaf_evaluations==first.leaf_evaluations
        &&result.parent_evaluations==first.parent_evaluations,
        "same-density memo changed original intervals, work charges or kernel diagnostics");
    const auto cached_generation=result.source_generation;
    auto scaled_identity=identity;scaled_identity.inputs.front().version={2};
    std::fill(density.begin(),density.end(),2.);tree.update(density,scaled_identity);
    auto scaled_control=control;scaled_control.face_absolute_target=2.*control.face_absolute_target;
    RingBoundaryEvaluation scaled;
    execution->run(EvaluateRingBoundary{&tree,&op,&scaled_identity,&scaled_control,&scaled});
    require(scaled.status==RingBoundaryStatus::Bounded&&scaled.source==scaled_identity
        &&scaled.source_generation>cached_generation&&scaled.memo_hits>0,
        "positive-density memo scaling lost actual source generation or current error budget");
    tree.require_current_ring(op,scaled);
    const auto scaled_generation=scaled.source_generation;
    rejects([&]{tree.require_current_ring(op,first);},
        "memo entry granted authority to a stale original source generation");
    for(std::size_t f=0;f<op.faces().size();++f)if(op.faces()[f].boundary_side>=0) {
        // Two rigorous intervals for the same density-scaled source must overlap.
        // Direct shared-leaf tests separately check outward interval containment.
        require(scaled.lower[f]<=2.*first.upper[f]&&scaled.upper[f]>=2.*first.lower[f]
            &&scaled.errors[f].absolute_error<=scaled_control.face_absolute_target,
            "density-scaled ring intervals are disjoint or exceed the current target");
    }
    auto one_box=scaled_control;one_box.maximum_boxes_per_leaf=1;
    RingBoundaryEvaluation boxed;
    execution->run(EvaluateRingBoundary{&tree,&op,&scaled_identity,&one_box,&boxed});
    require(boxed.memo_misses>0&&boxed.status!=RingBoundaryStatus::Bounded,
        "memo hid the current one-box subdivision limit");
    auto zero_target=scaled_control;zero_target.face_absolute_target=0.;
    zero_target.maximum_boxes_per_leaf=1;
    RingBoundaryEvaluation tighter;
    execution->run(EvaluateRingBoundary{&tree,&op,&scaled_identity,&zero_target,&tighter});
    require(tighter.memo_hits==0&&tighter.memo_misses>0
        &&tighter.status!=RingBoundaryStatus::Bounded,
        "memo converted a tighter zero target into successful convergence");
    // Clear retires ALL previous-call entries without changing source authority.
    // Exact axial quotienting can create/reuse new equivalent entries DURING
    // the next cold traversal; those within-call hits are not retained history.
    tree.clear_ring_memo();
    require(tree.ring_memo_size()==0,"numeric-history clear retained previous-call entries");
    execution->run(EvaluateRingBoundary{&tree,&op,&scaled_identity,&scaled_control,&scaled});
    require(scaled.memo_misses>0&&scaled.memo_admissions>0
        &&scaled.kernel_enclosures>0&&scaled.source_generation==scaled_generation
        &&scaled.status==RingBoundaryStatus::Bounded,
        "numeric-history clear retained memo work or reset actual source authority");
    std::fill(density.begin(),density.end(),1.);tree.update(density,identity);
    execution->run(EvaluateRingBoundary{&tree,&op,&identity,&control,&result});
    require(result.status==RingBoundaryStatus::Bounded,"restored source did not meet original target");
    const auto generation=result.source_generation;
    auto stale=identity;stale.inputs.front().version={2};
    rejects([&]{execution->run(EvaluateRingBoundary{&tree,&op,&stale,&control,&result});},
        "typed ring executor accepted stale source");
    require(result.source_generation==0 && result.values.empty()
        && result.status!=RingBoundaryStatus::Bounded,
        "failed typed ring request retained a bounded result");
    auto limited=control;limited.maximum_leaf_evaluations=1;
    execution->run(EvaluateRingBoundary{&tree,&op,&identity,&limited,&result});
    require(result.status==RingBoundaryStatus::WorkLimit && result.source==identity
        && result.source_generation==generation,"typed ring executor hid WorkLimit provenance");
    rejects([&]{execution->run(EvaluateRingBoundary{nullptr,&op,&identity,&control,&result});},
        "typed ring executor accepted missing owner");
    require(result.values.empty() && result.source_generation==0,
        "incomplete descriptor retained partial output");
    EvaluateBoundary legacy{};legacy.semantics=base.semantics;
    rejects([&]{execution->run(legacy);},"RZ chart reached legacy log work");
    double previous_target=0.;
    for(double rho:{0.,1.e-6,1.,1.e6}) {
        std::fill(density.begin(),density.end(),rho);tree.update(density,identity);
        std::vector<double> source(op.size(),-4.*constants::math::pi*
            constants::gravity::cgs::gravitational_constant*rho);
        const auto proposal=tree.propose_ring_budget(op,identity,source,1.e-10,0.);
        require(proposal.source==identity&&proposal.source_generation>generation,
            "budget proposal lost current source identity");
        require(proposal.basis==RingBudgetBasis::PositiveIsolatedRhs,
            "exact positive native boundary map lacks proved RHS basis");
        if(rho==0.)require(proposal.status==RingBudgetStatus::ZeroBudget
            &&proposal.control.face_absolute_target==0.,"zero budget acquired a floor");
        else {
            require(proposal.status==RingBudgetStatus::Proposed
                &&proposal.control.face_absolute_target>previous_target,
                "budget did not follow source scale");
            require(proposal.control.face_absolute_target*proposal.boundary_sensitivity_upper
                <=.5*proposal.initial_tolerance,"budget exceeded initial half allocation");
            previous_target=proposal.control.face_absolute_target;
        }
        require(tree.propose_ring_budget(op,identity,source,0.,0.).status
            ==RingBudgetStatus::InvalidInput,"invalid request produced budget");
        rejects([&]{tree.propose_ring_budget(op,stale,source,1.e-10,0.);},
            "stale source produced a budget");
    }
    std::cout<<"RING_BUDGET_PROPOSAL_PASS scales=4 zero_floor=0 source_identity=1 stale_rejected=1\\n";
    std::cout<<"TYPED_RING_HOST_EXECUTION_PASS cells="<<op.size()
        <<" source_identity=1 stale_result_retired=1 work_limit=1 missing_owner=1 legacy_log_rejected=1\n";
}
/** Authentic RZ user service: same quadratic PDE and original analytic
 * windows as line_error. No isolated boundary or Runtime source authority. */
void rz_user_analytic_service() {
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    const double A=constants::math::pi*constants::gravity::cgs::gravitational_constant;
    for(double inner:{0.,1.}) {
        SimConfig config;config.grid.geometry="cylindrical";config.grid.dim=2;
        config.grid.nblockx1=2;config.grid.nblockx2=1;config.grid.nblockx3=0;
        config.grid.x1_min=inner;config.grid.x1_max=inner+1.;
        config.grid.x2_min=-.5;config.grid.x2_max=.5;
        config.grid.x1l_boundary_type=inner==0.?"reflect":"outflow";
        config.grid.x1r_boundary_type=config.grid.x2l_boundary_type=config.grid.x2r_boundary_type="outflow";
        config.amr.lrefinemax=1;config.amr.refine_on_rho=true;
        config.amr.refine_threshold=.1;config.amr.derefine_threshold=.05;
        config.physics.gravity.boundary="user";
        SpeciesManager species;bool bad_datum=false;
        boundary::ResolvedUserBoundaries callbacks;
        callbacks.gravity=[&](const boundary::GravityBoundaryContext& ctx) {
            require(ctx.point.x==ctx.native_position[0]&&ctx.point.y==0.
                &&ctx.point.z==ctx.native_position[1],"RZ gravity callback lost its meridian chart");
            const double sign=ctx.side==boundary::BoundarySide::Lower?-1.:1.;
            const bool radial=ctx.axis==boundary::BoundaryAxis::X1;
            require(ctx.cartesian_normal==std::array<double,3>{radial?sign:0.,0.,radial?0.:sign},
                "RZ gravity callback used an azimuthal rather than an axial normal");
            const double r=ctx.native_position[0],phi=A*(r*r+ctx.time);
            const double datum=bad_datum?std::numeric_limits<double>::quiet_NaN():phi;
            if(ctx.time<1.)return boundary::GravityBoundaryData::Dirichlet(datum);
            return boundary::GravityBoundaryData::Robin(2.,1.,
                bad_datum?datum:2.*phi+ctx.cartesian_normal[0]*2.*A*r);
        };
        boundary::ScopedUserBoundarySelection selection(callbacks,config,species);
        amr::AMRControl control(8,2);control.tree->InitRootGrid(config,0,rz);
        std::vector<amr::BlockHandle> handles;GravitySolveIdentity identity;
        std::vector<GravityDensityView> views;
        const auto bind_source=[&](std::uint64_t epoch) {
            handles.clear();views.clear();identity={};identity.topology={epoch};
            identity.gravitational_constant=constants::gravity::cgs::gravitational_constant;
            identity.operator_revision=identity.boundary_revision=identity.accuracy_revision=1;
            for(int id:control.tree->GetActiveBlocks()) {
                auto& block=control.pool->GetBlock(id);const auto& grid=block.grid;
                for(auto* fluid:{&block.fluid_state,&block.state_next,&block.state_scratch}) {
                    for(int c=0;c<grid.GetTotalSize();++c)fluid->set(c,{1.,0.,0.,0.,10.});
                    std::fill(fluid->mass_fractions.begin(),fluid->mass_fractions.end(),1.);
                }
                handles.push_back({{static_cast<std::uint64_t>(id+1)},{epoch}});
                const GravityInputIdentity input{handles.back(),state::StateSlot::Current,{1},epoch};
                identity.inputs.push_back(input);
                views.push_back({input,{block.fluid_state.rho.data(),block.fluid_state.rho.size(),
                    amr::native_scalar_layout(grid),grid::FieldMemory::Host,epoch}});
            }
        };
        bind_source(27);SelfGravity gravity(config.physics.gravity);
        gravity.bind(amr::bind_elliptic_mesh(control,config.grid,handles));
        const auto check=[&](double time) {
            identity.input_time=time;
            std::vector<std::vector<double>> before;
            for(int id:control.tree->GetActiveBlocks())before.push_back(control.pool->GetBlock(id).fluid_state.rho);
            const auto token=gravity.prepare({identity,views});
            require(gravity.report().residual<=gravity.report().target,"RZ explicit quadratic residual");
            const auto& phi=gravity.potential();const auto& force=gravity.acceleration();
            double pe=0.,ps=0.,fe=0.,fs=0.;std::size_t cell=0,block_index=0;
            for(int id:control.tree->GetActiveBlocks()) {
                const auto& block=control.pool->GetBlock(id);const auto& grid=block.grid;
                for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i,++cell) {
                    const double r=grid.GetCellCenterX(i),exact=A*(r*r+time),g=-2.*A*r;
                    pe+=(phi.at(cell)-exact)*(phi.at(cell)-exact);ps+=exact*exact;
                    fe+=(force[0].at(cell)-g)*(force[0].at(cell)-g)
                        +force[1].at(cell)*force[1].at(cell)+force[2].at(cell)*force[2].at(cell);
                    fs+=g*g;
                }
                require(block.fluid_state.rho==before[block_index++],"RZ explicit service changed source density");
            }
            require(cell==phi.size(),"RZ explicit field layout changed");
            std::cout<<std::setprecision(17)<<"RZ_USER_SERVICE_ERROR inner="<<inner
                <<" time="<<time<<" leaves="<<control.tree->GetActiveBlocks().size()
                <<" potential="<<std::sqrt(pe/ps)<<" force="<<std::sqrt(fe/fs)
                <<" residual="<<gravity.report().residual<<" target="<<gravity.report().target<<'\n';
            require(std::sqrt(pe/ps)<1e-7,"RZ user quadratic potential");
            require(std::sqrt(fe/fs)<1e-6,"RZ user quadratic force");
            return token;
        };
        const auto first=check(0.),datum=check(.5),rebuilt=check(1.5);
        require(datum.value>first.value&&rebuilt.value>datum.value,
            "RZ datum/operator rebuild restarted publication generation");
        bad_datum=true;
        rejects([&]{gravity.prepare({identity,views});},"nonfinite RZ user datum accepted");
        rejects([&]{gravity.potential();},"bad RZ datum retained old potential publication");
        rejects([&]{gravity.acceleration();},"bad RZ datum retained old force publication");
        bad_datum=false;check(1.5);
        if(inner==1.) {
            const int selected=control.tree->GetActiveBlocks().front();
            auto transaction=control.tree->PrepareRegrid(config,{}, {},[&] {
                for(int id:control.tree->GetActiveBlocks())control.pool->GetBlock(id).refine_flag=id==selected?1:0;
            });
            require(transaction.topology_changed(),"RZ explicit mixed witness did not refine");
            std::vector<amr::BlockHandle> next;
            for(int id:transaction.proposed_active_blocks())next.push_back({{static_cast<std::uint64_t>(id+1)},{28}});
            transaction.BuildMigrationPlans(handles,next,{1,{27},{28}});
            transaction.ExecuteMigration();transaction.ActivateForFinalization();
            transaction.PublishNoexcept();transaction.ReleaseRetired();
            require(control.tree->GetActiveBlocks().size()==5,"RZ explicit mixed leaf count");
            bind_source(28);gravity.bind(amr::bind_elliptic_mesh(control,config.grid,handles),1.5);
            check(1.5);
        }
    }
    std::cout<<"RZ_USER_SERVICE_ANALYTIC_PASS axis=1 annulus=1 mixed=1 datum=1 rebuild=1 stale_retired=1\n";
}
void rz_binding_identity() {
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    std::size_t checked=0;
    for(double inner:{0.,1.}) {
        SimConfig config;
        config.grid.geometry="cylindrical";config.grid.dim=2;
        config.grid.nblockx1=2;config.grid.nblockx2=1;config.grid.nblockx3=0;
        config.grid.x1_min=inner;config.grid.x1_max=inner+1.;
        config.grid.x2_min=-.5;config.grid.x2_max=.5;
        config.amr.lrefinemax=1;config.amr.refine_on_rho=true;
        config.amr.refine_threshold=.1;config.amr.derefine_threshold=.05;
        amr::AMRControl control(8,2);control.tree->InitRootGrid(config,0,rz);
        for(int id:control.tree->GetActiveBlocks()) {
            auto& block=control.pool->GetBlock(id);
            for(auto* fluid:{&block.fluid_state,&block.state_next,&block.state_scratch})
                for(int cell=0;cell<block.grid.GetTotalSize();++cell)
                    fluid->set(cell,{1.,0.,0.,0.,10.});
        }
        std::vector<amr::BlockHandle> handles;
        for(int id:control.tree->GetActiveBlocks())
            handles.push_back({{static_cast<std::uint64_t>(id+1)},{17}});
        const auto verify_binding=[&](const amr::EllipticMeshBinding& binding) {
        require(binding.base.semantics==rz,"RZ adapter lost authoritative tree chart");
        arch::elliptic::CompositePoisson op(binding.base,binding.cells,
            arch::elliptic::BoundaryKind::CurvilinearIsolated);
        require(op.boundary().sides[2]==arch::elliptic::FaceBoundaryKind::Dirichlet
            && op.boundary().sides[3]==arch::elliptic::FaceBoundaryKind::Dirichlet,
            "RZ axial faces were reinterpreted as periodic azimuth");
        for(std::size_t cell=0;cell<binding.storage.size();++cell) {
            const auto storage=binding.storage[cell];
            const auto& grid=*binding.grids[storage.block];
            const auto position=binding.cells[cell].index;
            const int i=grid.Is()+position[0]%amr::BLOCK_NX;
            const int j=grid.Js()+position[1]%amr::BLOCK_NY;
            require(storage.offset==grid.GetIndex(i,j,0),"RZ native offset changed");
            const double native=GridMetrics::CellVolume(
                GridMetrics::make_geometry_view(grid,rz),i,j,0);
            require(op.volumes()[cell]==native,"RZ composite/native full-ring measure differ");
            const auto descriptor=gravity_cell_geometry(storage,op,static_cast<int>(cell));
            require(descriptor.block==static_cast<int>(storage.block)
                && descriptor.offset==storage.offset,"RZ workspace changed native storage identity");
            require(descriptor.width[0]==op.width(cell,0)
                && descriptor.width[1]==op.width(cell,1)
                && descriptor.width[2]==0.,"RZ workspace interpreted dz as r*dphi");
            ++checked;
        }
        // Independent regular manufactured Phi=z: g_r=0, g_z=-1.
        // No ring boundary solve or production bind is used here.
        const auto rows=gravity_face_rows(op);
        std::vector<double> phi(op.size()),boundary(op.faces().size()),gradient(op.faces().size());
        for(int cell=0;cell<op.size();++cell)phi[cell]=op.center(cell)[1];
        for(std::size_t face=0;face<op.faces().size();++face) {
            boundary[face]=op.faces()[face].center[1];
            gradient[face]=op.faces()[face].axis==1?1.:0.;
        }
        const auto apply=[](const arch::multigrid::SparseStorage& csr,int row,
            const std::vector<double>& input) {
            long double value=0.;
            for(int k=csr.offsets[row];k<csr.offsets[row+1];++k)
                value+=static_cast<long double>(csr.values[k])*input[csr.columns[k]];
            return value;
        };
        std::vector<double> side_values(6*op.size()),cell_force(3*op.size()),inverse_dt(op.size());
        std::vector<GravityCell> cell_geometry;
        for(int cell=0;cell<op.size();++cell)
            cell_geometry.push_back(gravity_cell_geometry(binding.storage[cell],op,cell));
        for(int cell=0;cell<op.size();++cell)for(int axis=0;axis<2;++axis)for(int side=0;side<2;++side) {
            const int row=6*cell+2*axis+side;
            const long double acceleration=apply(rows.acceleration,row,gradient);
            side_values[row]=static_cast<double>(acceleration);
            const long double expected=axis==1?-1.:0.;
            require(std::abs(acceleration-expected)<=64.*std::numeric_limits<double>::epsilon(),
                "RZ native face acceleration uses wrong physical axis/fragment measure");
            long double work=apply(rows.potential_work,row,phi)
                +apply(rows.boundary_work,row,boundary),reference=0.,scale=0.;
            // Native signed finite-volume work, independently summed by actual
            // faces (not CSR columns): +/-2*A/V*(z_face-z_cell).
            for(const auto& face:op.faces())if(face.axis==axis
                && (side==0?face.right==cell:face.left==cell)) {
                const long double term=(side==0?2.L:-2.L)*face.area/op.volumes()[cell]
                    *(static_cast<long double>(face.center[1])-op.center(cell)[1]);
                reference+=term;scale+=std::abs(term);
            }
            for(int k=rows.potential_work.offsets[row];k<rows.potential_work.offsets[row+1];++k)
                scale+=std::abs(static_cast<long double>(rows.potential_work.values[k])
                    *phi[rows.potential_work.columns[k]]);
            for(int k=rows.boundary_work.offsets[row];k<rows.boundary_work.offsets[row+1];++k)
                scale+=std::abs(static_cast<long double>(rows.boundary_work.values[k])
                    *boundary[rows.boundary_work.columns[k]]);
            if(std::abs(work-reference)>64.*std::numeric_limits<double>::epsilon()*scale) {
                std::cerr.precision(20);
                std::cerr<<"RZ_WORK_ROW cell="<<cell<<" axis="<<axis<<" side="<<side
                    <<" inner="<<inner<<" level="<<binding.cells[cell].level
                    <<" work="<<work<<" expected="<<reference<<" scale="<<scale<<'\n';
                for(const auto& face:op.faces())if(face.axis==axis
                    && (side==0?face.right==cell:face.left==cell)) {
                    std::cerr<<"face center_z="<<face.center[1]<<" A="<<face.area
                        <<" B="<<face.value_boundary_coefficient<<" samples=";
                    for(std::size_t k=0;k<face.value_samples.size();++k)
                        std::cerr<<face.value_samples[k]<<':'<<face.value_coefficients[k]<<' ';
                    std::cerr<<'\n';
                }
                throw std::runtime_error("RZ native potential work sign/area/volume/interpolation mismatch");
            }
        }
        // Quadratic INPUT, not a Poisson solve or an analytic face-point oracle.
        // The unique Phi_f keeps the original actual value-stencil/Dirichlet
        // semantics. Independently sum +/-2*A_f/V_i*(Phi_f-Phi_i) for each cell
        // side; unlike Phi=z, same-level neighbors generally have unequal work.
        std::vector<double> quadratic_phi(op.size()),quadratic_boundary(op.faces().size());
        for(int cell=0;cell<op.size();++cell) {
            const auto point=op.center(cell);
            quadratic_phi[cell]=point[0]*point[0]+point[1]*point[1];
        }
        for(std::size_t f=0;f<op.faces().size();++f) {
            const auto& point=op.faces()[f].center;
            quadratic_boundary[f]=point[0]*point[0]+point[1]*point[1];
        }
        std::vector<long double> quadratic_reference(6*op.size()),quadratic_scale(6*op.size());
        std::size_t unequal_shared_sides=0,coarse_fine_fragments=0;
        for(std::size_t f=0;f<op.faces().size();++f) {
            const auto& face=op.faces()[f];
            long double face_value=static_cast<long double>(face.value_boundary_coefficient)*quadratic_boundary[f];
            for(std::size_t k=0;k<face.value_samples.size();++k)
                face_value+=static_cast<long double>(face.value_coefficients[k])*quadratic_phi[face.value_samples[k]];
            for(int cell:{face.left,face.right})if(cell>=0) {
                const int side=cell==face.left?1:0,row=6*cell+2*face.axis+side;
                const long double factor=(side==0?2.L:-2.L)*face.area/op.volumes()[cell];
                const long double term=factor*(face_value-quadratic_phi[cell]);
                quadratic_reference[row]+=term;
                quadratic_scale[row]+=std::abs(term)+std::abs(factor*quadratic_phi[cell])
                    +std::abs(factor*face.value_boundary_coefficient*quadratic_boundary[f]);
                for(std::size_t k=0;k<face.value_samples.size();++k)
                    quadratic_scale[row]+=std::abs(factor*face.value_coefficients[k]
                        *quadratic_phi[face.value_samples[k]]);
            }
            if(face.left<0||face.right<0)continue;
            if(binding.cells[face.left].level!=binding.cells[face.right].level) {
                ++coarse_fine_fragments;continue;
            }
            const auto left=binding.storage[face.left],right=binding.storage[face.right];
            const auto& grid=*binding.grids[left.block];
            const int stride=face.axis==0?1:grid.stride_y;
            if(left.block!=right.block||right.offset!=left.offset+stride)continue;
            // The actual same-level value row is the arithmetic average, not
            // analytic Phi(face.center) for this quadratic input. Check that
            // independent value before asserting the strongest alias witness.
            require(face.value_samples.size()==2&&face.value_samples[0]==face.left
                &&face.value_samples[1]==face.right&&face.value_coefficients[0]==.5
                &&face.value_coefficients[1]==.5,
                "same-level quadratic work witness lost the original unique face-value rule");
            const long double average=.5L*(static_cast<long double>(quadratic_phi[face.left])
                +quadratic_phi[face.right]);
            require(face_value==average,"quadratic work face differs from independent same-level average");
            const long double high=-2.L*face.area/op.volumes()[face.left]*(average-quadratic_phi[face.left]);
            const long double low=2.L*face.area/op.volumes()[face.right]*(average-quadratic_phi[face.right]);
            if(std::abs(high-low)>64.*std::numeric_limits<double>::epsilon()*(std::abs(high)+std::abs(low)))
                ++unequal_shared_sides;
        }
        require(unequal_shared_sides>0,"quadratic manufactured work did not distinguish shared-face cell sides");
        if(binding.grids.size()>2)
            require(coarse_fine_fragments>0,"quadratic mixed topology has no genuine coarse-fine work fragments");
        const auto view=[](const multigrid::SparseStorage& csr) {
            return multigrid::SparseView{csr.offsets.data(),csr.columns.data(),csr.values.data()};
        };
        auto host=make_host_gravity_execution()->numeric();
        const auto saved_phi=quadratic_phi,saved_boundary=quadratic_boundary;
        std::vector<double> quadratic_work(6*op.size());
        host->run(multigrid::RowsWork{6*op.size(),view(rows.potential_work),quadratic_phi.data(),quadratic_work.data()});
        host->run(multigrid::RowsWork{6*op.size(),view(rows.boundary_work),quadratic_boundary.data(),quadratic_work.data(),1.,1.});
        std::vector<int> patch_offsets;
        int native_size=0;
        for(const auto* grid:binding.grids) {patch_offsets.push_back(native_size);native_size+=grid->GetTotalSize();}
        const auto patch_rows=gravity_patch_work_rows(binding,patch_offsets,native_size);
        std::vector<double> padded_work(6*native_size,19.);
        host->run(multigrid::RowsWork{6*native_size,view(patch_rows),quadratic_work.data(),padded_work.data()});
        host->fence();
        // These flat views follow the documented SAME-cell-offset contract;
        // no field is published and no native Runtime permission is minted.
        std::vector<GravityPatchView> patch_views(binding.grids.size());
        for(std::size_t b=0;b<binding.grids.size();++b)for(int axis=0;axis<3;++axis) {
            patch_views[b].work_low[axis]=padded_work.data()+2*axis*native_size+patch_offsets[b];
            patch_views[b].work_high[axis]=padded_work.data()+(2*axis+1)*native_size+patch_offsets[b];
        }
        std::vector<unsigned char> occupied(6*native_size);
        for(int cell=0;cell<op.size();++cell) {
            const auto location=binding.storage[cell];const auto& patch=patch_views[location.block];
            for(int axis=0;axis<2;++axis)for(int side=0;side<2;++side) {
                const int row=6*cell+2*axis+side;
                const int padded=(2*axis+side)*native_size+patch_offsets[location.block]+location.offset;
                occupied[padded]=1;
                const double actual=(side?patch.work_high[axis]:patch.work_low[axis])[location.offset];
                require(actual==quadratic_work[row],"quadratic cell-side work selected another real cell or face alias");
                require(std::isfinite(actual)&&std::abs(static_cast<long double>(actual)-quadratic_reference[row])
                    <=64.*std::numeric_limits<double>::epsilon()*quadratic_scale[row],
                    "quadratic padded work differs from independent signed area/own-volume face sum");
            }
        }
        for(const auto& face:op.faces())if(face.left>=0&&face.right>=0) {
            const auto left=binding.storage[face.left],right=binding.storage[face.right];
            const auto& grid=*binding.grids[left.block];const int stride=face.axis==0?1:grid.stride_y;
            if(left.block==right.block&&right.offset==left.offset+stride)
                require(patch_views[left.block].work_high[face.axis]+left.offset
                    !=patch_views[right.block].work_low[face.axis]+right.offset,
                    "adjacent native quadratic work cell sides share one physical-face address");
        }
        for(std::size_t i=0;i<padded_work.size();++i)if(!occupied[i])
            require(padded_work[i]==0.,"patch work invented an inactive/padded cell-side coefficient");
        require(quadratic_phi==saved_phi&&quadratic_boundary==saved_boundary,
            "Host work gather changed immutable actual potential/datum inputs");
        const auto saved_work=padded_work;
        auto wrong_offsets=patch_offsets;++wrong_offsets.back();
        rejects([&]{gravity_patch_work_rows(binding,wrong_offsets,native_size);},
            "patch work builder accepted invalid real padded offsets");
        auto duplicated=binding;duplicated.storage.back()=duplicated.storage.front();
        rejects([&]{gravity_patch_work_rows(duplicated,patch_offsets,native_size);},
            "patch work builder accepted duplicate real cell-storage ownership");
        require(padded_work==saved_work,"rejected patch work metadata changed the existing actual padded values");
        // Execute the original shared CellAcceleration work through its Host
        // executor; axial force must use dz and the inactive phi lane stays zero.
        make_host_gravity_execution()->run(CellAcceleration{op.size(),2,
            cell_geometry.data(),side_values.data(),cell_force.data(),inverse_dt.data()});
        for(int cell=0;cell<op.size();++cell) {
            require(cell_force[cell]==0. && cell_force[2*op.size()+cell]==0.,
                "RZ force mapped to inactive angular component");
            require(std::abs(cell_force[op.size()+cell]+1.)<=64.*std::numeric_limits<double>::epsilon(),
                "RZ actual cell acceleration lost axial mapping");
            const double expected=1./op.width(cell,1);
            require(std::abs(inverse_dt[cell]-expected)<=64.*std::numeric_limits<double>::epsilon()*expected,
                "RZ gravity timestep uses angular metric instead of dz");
        }
        int observers=0;
        for(std::size_t face=0;face<op.faces().size();++face) {
            const auto& native=op.faces()[face];
            if(native.boundary_side<0)continue;
            const auto descriptor=gravity_boundary_point(op,static_cast<int>(face));
            require(descriptor.face==static_cast<int>(face)
                && descriptor.position[0]==native.center[0]
                && descriptor.position[1]==0. && descriptor.position[2]==native.center[1],
                "RZ workspace observer treated physical z as azimuth");
            ++observers;
        }
        require(observers>0,"RZ workspace observer coverage missing");
        rejects([&]{gravity_cell_geometry(binding.storage.front(),op,-1);},"negative cell descriptor accepted");
        rejects([&]{gravity_boundary_point(op,static_cast<int>(op.faces().size()));},"invalid face descriptor accepted");
        auto controls=config.physics.gravity;controls.boundary="isolated";
        SelfGravity gravity(controls);
        bool rejected=false;
        try {gravity.bind(binding);}
        catch(const std::logic_error& error) {
            rejected=std::string_view(error.what())==
                "RZ self-gravity finite-ring runtime consumer is not qualified";
        }
        require(rejected,"unqualified RZ reached legacy gravity runtime");
        rejects([&]{gravity.potential();},"failed RZ bind published potential");
        Fixture legacy;SelfGravity reused(legacy.config.physics.gravity);
        reused.bind(amr::bind_elliptic_mesh(legacy.control,legacy.config.grid,legacy.handles));
        reused.prepare({legacy.identity,legacy.views});
        require(!reused.potential().empty(),"existing Cartesian field not ready");
        rejects([&]{reused.bind(binding);},"live field accepted unqualified RZ chart");
        rejects([&]{reused.potential();},"RZ bind failure retained old field publication");
        reused.bind(amr::bind_elliptic_mesh(legacy.control,legacy.config.grid,legacy.handles));
        reused.prepare({legacy.identity,legacy.views});
        require(!reused.potential().empty(),"supported Cartesian rebind did not recover");
        };
        verify_binding(amr::bind_elliptic_mesh(control,config.grid,handles));
        // Same original tree transfer owner; production Runtime RZ regridding
        // remains gated. Refine one root, retaining a real mixed-level mesh.
        const int selected=control.tree->GetActiveBlocks().front();
        auto transaction=control.tree->PrepareRegrid(config,{}, {}, [&] {
            for(int id:control.tree->GetActiveBlocks())
                control.pool->GetBlock(id).refine_flag=id==selected?1:0;
        });
        require(transaction.topology_changed(),"RZ mixed binding witness did not refine");
        std::vector<amr::BlockHandle> next;
        for(int id:transaction.proposed_active_blocks())
            next.push_back({{static_cast<std::uint64_t>(id+1)},{18}});
        transaction.BuildMigrationPlans(handles,next,{1,{17},{18}});
        transaction.ExecuteMigration();transaction.ActivateForFinalization();
        transaction.PublishNoexcept();transaction.ReleaseRetired();
        require(control.tree->GetActiveBlocks().size()==5,"RZ mixed binding leaf count");
        verify_binding(amr::bind_elliptic_mesh(control,config.grid,next));
    }
    std::cout<<"RZ_ELLIPTIC_BINDING_IDENTITY_PASS cells="<<checked
        <<" axis=1 offaxis=1 mixed=1 workspace_lengths=1 face_acceleration=1 potential_work=1 actual_cell_force=1 physical_timestep=1 meridian_observers=1 axial_dirichlet=1 runtime_gate=1 old_publication_retired=1 recovery=1\n";
}
/** Real native two-block gather and original SelfGravity pipeline.
 * Opt-in expensive verification; no Hydro/time/output and no physical publish.
 */
void native_rz_service_candidate(bool zero_source=false,bool lifecycle=false) {
    SimConfig config;config.grid.geometry="cylindrical";config.grid.dim=2;
    config.grid.nblockx1=2;config.grid.nblockx2=1;config.grid.nblockx3=0;
    config.grid.x1_min=0.;config.grid.x1_max=1.;
    config.grid.x2_min=-.5;config.grid.x2_max=.5;
    config.physics.gravity.boundary="isolated";
    config.physics.gravity.relative_tolerance=1.e-10;
    config.physics.gravity.absolute_tolerance=0.;config.physics.gravity.max_cycles=200;
    amr::AMRControl control(8,2);
    control.tree->InitRootGrid(config,0,GridMetrics::GeometrySemantics::AxisymmetricRz);
    GravitySolveIdentity identity;identity.topology={7};
    identity.gravitational_constant=constants::gravity::cgs::gravitational_constant;
    identity.operator_revision=identity.boundary_revision=identity.accuracy_revision=1;
    std::vector<amr::BlockHandle> handles;std::vector<GravityDensityView> views;
    for(int id:control.tree->GetActiveBlocks()) {
        auto& b=control.pool->GetBlock(id);
        for(int c=0;c<b.grid.GetTotalSize();++c)b.fluid_state.set(c,{zero_source?0.:1.,0.,0.,0.,10.});
        handles.push_back({{static_cast<std::uint64_t>(id+1)},{7}});
        const GravityInputIdentity input{handles.back(),state::StateSlot::Current,{1},1};
        identity.inputs.push_back(input);
        views.push_back({input,{b.fluid_state.rho.data(),b.fluid_state.rho.size(),
            amr::native_scalar_layout(b.grid),grid::FieldMemory::Host,1}});
    }
    SelfGravity gravity(config.physics.gravity);
    auto execution=std::make_shared<IdentityExecution>();
    if(lifecycle)gravity.set_execution(execution);
    auto binding=amr::bind_elliptic_mesh(control,config.grid,handles);
    rejects([&]{gravity.bind(binding);},"public bind enabled RZ");
    // Exercise actual-source auto resource derivation through the existing
    // two-block service owner; all source/config/residual assertions stay fixed.
    gravity.bind_native_rz_candidate(binding,65536,0);
    if(zero_source) {
        bool rejected=false;
        try{gravity.prepare({identity,views});}
        catch(const std::invalid_argument& error) {
            rejected=std::string_view(error.what())=="Self gravity requires finite positive active density";
        }
        require(rejected,"native candidate relaxed the positive-density service contract");
        rejects([&]{gravity.native_rz_potential();},"zero-density rejection published candidate");
        rejects([&]{gravity.potential();},"zero-density rejection published physical field");
        std::cout<<"RZ_NATIVE_SERVICE_ZERO_REJECT_PASS positive_density_contract=1 candidate_unpublished=1\\n";
        return;
    }
    gravity.prepare({identity,views});
    const auto& assessment=gravity.native_rz_assessment();
    require(assessment.source==identity&&assessment.source.inputs.size()==2
        &&assessment.conditional.status==elliptic::BoundaryResidualStatus::Accepted,
        "candidate lost full native source identity or original request");
    require(assessment.physical_status==elliptic::BoundaryResidualStatus::UncertifiedInput,
        "candidate acquired physical qualification");
    std::cout<<"RZ_NATIVE_AUTO_RESOURCE_ENGINEERING_PASS actual_self_gravity=1"
        <<" original_two_block_source=1 original_tolerance=1 physical_qualification=0\n";
    std::cout<<std::setprecision(17)<<"RZ_NATIVE_SERVICE_RESIDUAL total="
        <<assessment.conditional.total_residual_upper<<" safe="
        <<assessment.conditional.tolerance_safe<<" source_generation="
        <<assessment.source_generation<<" input_count="<<assessment.source.inputs.size()<<"\\n";
    const auto& phi=gravity.native_rz_potential();
    const auto& g=gravity.native_rz_acceleration();
    require(phi.size()==512,"candidate active native extent changed");
    double maximum=0.;
    for(int a=0;a<3;++a)for(double value:g[a]) {
        require(std::isfinite(value),"candidate force nonfinite");
        if(a==2)require(value==0.,"axisymmetric candidate has azimuth force");
        maximum=std::max(maximum,std::abs(value));
    }
    if(zero_source) {
        require(maximum==0.,"zero numerical source generated force");
        for(double value:phi)require(value==0.,"zero numerical source generated potential");
    } else require(maximum>0.,"nonzero source candidate skipped force work");
    rejects([&]{gravity.potential();},"candidate passed physical output reader");
    rejects([&]{gravity.report();},"candidate passed physical report reader");
    rejects([&]{gravity.patch_view(0);},"candidate passed physical Hydro patch reader");
    auto stale=identity;stale.inputs.back().version={2};
    rejects([&]{gravity.prepare({stale,views});},"candidate accepted nonfirst stale dependency");
    rejects([&]{gravity.native_rz_potential();},"candidate failure retained old field");
    if(lifecycle) {
        // Borrow each real Block state allocation. Poison inactive buffers so
        // Current-only gathering cannot masquerade as Scratch/Next support.
        std::uint64_t previous_generation=assessment.source_generation;
        const std::array slots{state::StateSlot::Scratch,state::StateSlot::Next,
            state::StateSlot::Current};
        for(std::size_t lane=0;lane<slots.size();++lane) {
            views.clear();identity.inputs.clear();
            std::size_t block_index=0;
            for(int id:control.tree->GetActiveBlocks()) {
                auto& block=control.pool->GetBlock(id);
                for(auto* state:{&block.fluid_state,&block.state_scratch,&block.state_next})
                    for(int c=0;c<block.grid.GetTotalSize();++c)
                        state->rho[c]=std::numeric_limits<double>::quiet_NaN();
                auto& selected=slots[lane]==state::StateSlot::Current?block.fluid_state:
                    slots[lane]==state::StateSlot::Scratch?block.state_scratch:block.state_next;
                for(int c=0;c<block.grid.GetTotalSize();++c)selected.set(c,{1.,0.,0.,0.,10.});
                const auto generation=static_cast<std::uint64_t>(lane+2);
                const GravityInputIdentity input{handles[block_index++],slots[lane],
                    {generation},generation};
                identity.inputs.push_back(input);
                views.push_back({input,{selected.rho.data(),selected.rho.size(),
                    amr::native_scalar_layout(block.grid),grid::FieldMemory::Host,generation}});
            }
            // A mismatched nonfirst slot must retire the publication before
            // delegated gather/ring/solve work; the valid request then recovers.
            auto bad=identity;bad.inputs.back().slot=
                slots[lane]==state::StateSlot::Current?state::StateSlot::Next:state::StateSlot::Current;
            const auto before=execution->work_count;
            rejects([&]{gravity.prepare({bad,views});},"native lifecycle accepted wrong nonfirst slot");
            require(execution->work_count==before,"native invalid slot executed work");
            rejects([&]{gravity.native_rz_assessment();},"native invalid slot retained assessment");
            gravity.prepare({identity,views});
            const auto& next=gravity.native_rz_assessment();
            require(next.source==identity&&next.source_generation>previous_generation,
                "native lifecycle lost selected state identity/generation");
            previous_generation=next.source_generation;
            require(next.conditional.status==elliptic::BoundaryResidualStatus::Accepted
                &&next.physical_status==elliptic::BoundaryResidualStatus::UncertifiedInput,
                "native lifecycle lost original request or acquired physical grant");
            require(gravity.native_rz_potential().size()==512,"native lifecycle changed active extent");
            for(const auto& component:gravity.native_rz_acceleration())
                for(double value:component)require(std::isfinite(value),"native lifecycle force nonfinite");
            rejects([&]{gravity.potential();},"native lifecycle escaped candidate scope");
            rejects([&]{gravity.patch_view(1);},"native nonfirst patch escaped candidate scope");
            std::cout<<std::setprecision(17)<<"RZ_NATIVE_SLOT_PASS lane="<<lane
                <<" slot="<<static_cast<int>(slots[lane])<<" source_generation="<<previous_generation
                <<" total="<<next.conditional.total_residual_upper
                <<" safe="<<next.conditional.tolerance_safe<<" cells=512 time=0 steps=0"<<std::endl;
        }
        std::cout<<"RZ_NATIVE_SERVICE_LIFECYCLE_PASS actual_buffers=3"
            <<" inactive_nan=1 nonfirst_slot_zero_work=1 stale_retired=1 recovery=1"
            <<" physical_readers_rejected=1 time=0 steps=0"<<std::endl;
    }
    std::cout<<"RZ_NATIVE_SERVICE_CANDIDATE_PASS zero_numerical_source="<<zero_source<<" blocks=2 cells=512 source_identity=1"
        <<" original_request=1 finite_force=1 physical_readers_rejected=1 nonfirst_stale=1 time=0 steps=0\\n";
}
void qualification_scope() {
    Fixture f;GravityFieldValidity validity;
    validity.publish({f.identity,1,{1,state::CompletionState::Complete},
        GravityFieldScope::NativeRzCandidate});
    require(!validity.matches(f.identity,1),"native candidate matched physical scope");
    require(validity.matches(f.identity,1,GravityFieldScope::NativeRzCandidate),
        "native candidate not identified explicitly");
    rejects([&]{validity.publish({f.identity,1,{1,state::CompletionState::Complete},
        static_cast<GravityFieldScope>(255)});},"unknown field qualification accepted");
    validity.invalidate();
    require(!validity.matches(f.identity,1,GravityFieldScope::NativeRzCandidate),
        "candidate scope retained retired publication");
}
void native_components() {
    for(int dimension:{2,3}) {
        SimConfig config; config.grid.dim=dimension;
        config.grid.nblockx1=config.grid.nblockx2=2;
        config.grid.nblockx3=dimension==3?2:0;
        amr::AMRControl control(12,dimension);control.tree->InitRootGrid(config,0);
        GravitySolveIdentity identity;identity.topology={1};identity.gravitational_constant=arch::constants::gravity::cgs::gravitational_constant;
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
                    double exact=-.2*arch::constants::gravity::cgs::gravitational_constant/dimension;
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

/** The shared boundary normal helper must be exact for the current cylindrical
 *  and spherical conventions, must not lose precision at a large Cartesian
 *  origin the way a finite difference would, and must reject unusable inputs.
 *  The callback input must also carry the full PointCoords view, not only xyz. */
void boundary_normals() {
    const auto close=[](double a,double b){return std::abs(a-b)<1e-12;};
    const auto check=[&](const Grid& grid,const PointCoords& point,arch::boundary::BoundaryAxis axis,
        arch::boundary::BoundarySide side,const std::array<double,3>& exact,const char* what){
        const auto normal=arch::boundary::BoundaryCartesianNormal(grid,point,axis,side);
        require(close(normal[0],exact[0])&&close(normal[1],exact[1])&&close(normal[2],exact[2]),what);
    };
    Grid cartesian;cartesian.dim=3;cartesian.geometry="cartesian";
    // 1e12 is far beyond the resolution of any fixed finite-difference step.
    const auto far=Grid::PhysicalCoordsFromNative(3,"cartesian",1e12,0.,0.);
    check(cartesian,far,arch::boundary::BoundaryAxis::X1,arch::boundary::BoundarySide::Lower,
        {-1.,0.,0.},"large Cartesian origin lower normal");
    check(cartesian,far,arch::boundary::BoundaryAxis::X1,arch::boundary::BoundarySide::Upper,
        {1.,0.,0.},"large Cartesian origin upper normal");
    Grid line;line.dim=1;line.geometry="cartesian";
    rejects([&]{arch::boundary::BoundaryCartesianNormal(line,far,arch::boundary::BoundaryAxis::X2,
        arch::boundary::BoundarySide::Upper);},"normal on an inactive axis accepted");
    Grid oblique;oblique.dim=2;oblique.geometry="oblique";
    rejects([&]{arch::boundary::BoundaryCartesianNormal(oblique,far,arch::boundary::BoundaryAxis::X1,
        arch::boundary::BoundarySide::Lower);},"normal from an unsupported geometry accepted");
    const double sixth=constants::math::pi/6.;
    Grid cylindrical2;cylindrical2.dim=2;cylindrical2.geometry="cylindrical";
    const auto polar=Grid::PhysicalCoordsFromNative(2,"cylindrical",2.,sixth,0.);
    check(cylindrical2,polar,arch::boundary::BoundaryAxis::X1,arch::boundary::BoundarySide::Upper,
        {std::cos(sixth),std::sin(sixth),0.},"cylindrical radial normal");
    check(cylindrical2,polar,arch::boundary::BoundaryAxis::X2,arch::boundary::BoundarySide::Upper,
        {-std::sin(sixth),std::cos(sixth),0.},"cylindrical azimuthal normal");
    Grid cylindrical3;cylindrical3.dim=3;cylindrical3.geometry="cylindrical";
    const auto tube=Grid::PhysicalCoordsFromNative(3,"cylindrical",1.5,0.25,0.4);
    check(cylindrical3,tube,arch::boundary::BoundaryAxis::X2,arch::boundary::BoundarySide::Upper,
        {0.,0.,1.},"cylindrical axial normal");
    check(cylindrical3,tube,arch::boundary::BoundaryAxis::X3,arch::boundary::BoundarySide::Upper,
        {-std::sin(0.4),std::cos(0.4),0.},"cylindrical three-dimensional azimuthal normal");
    // The current two-dimensional spherical plane is a polar section: axis 2 is
    // azimuth, so at phi=0 the outward azimuthal direction is +y.
    Grid spherical2;spherical2.dim=2;spherical2.geometry="spherical";
    const auto wedge=Grid::PhysicalCoordsFromNative(2,"spherical",3.,0.,0.);
    check(spherical2,wedge,arch::boundary::BoundaryAxis::X1,arch::boundary::BoundarySide::Upper,
        {1.,0.,0.},"two-dimensional spherical radial normal at phi=0");
    check(spherical2,wedge,arch::boundary::BoundaryAxis::X2,arch::boundary::BoundarySide::Upper,
        {0.,1.,0.},"two-dimensional spherical azimuth normal at phi=0");
    Grid spherical3;spherical3.dim=3;spherical3.geometry="spherical";
    const double theta=1.1,phi=0.7;
    const auto ball=Grid::PhysicalCoordsFromNative(3,"spherical",2.,theta,phi);
    check(spherical3,ball,arch::boundary::BoundaryAxis::X1,arch::boundary::BoundarySide::Upper,
        {std::sin(theta)*std::cos(phi),std::sin(theta)*std::sin(phi),std::cos(theta)},
        "three-dimensional spherical radial normal");
    check(spherical3,ball,arch::boundary::BoundaryAxis::X2,arch::boundary::BoundarySide::Upper,
        {std::cos(theta)*std::cos(phi),std::cos(theta)*std::sin(phi),-std::sin(theta)},
        "three-dimensional spherical polar normal");
    check(spherical3,ball,arch::boundary::BoundaryAxis::X3,arch::boundary::BoundarySide::Upper,
        {-std::sin(phi),std::cos(phi),0.},"three-dimensional spherical azimuthal normal");
    // The real gravity face path must publish the same view to a callback.
    elliptic::EllipticMesh mesh;
    mesh.dimension=3;mesh.geometry=elliptic::Geometry::Spherical;
    mesh.cells={8,4,4};mesh.spacing={.1,.05,.05};mesh.origin={.5,.2,.3};
    SimConfig config;SpeciesManager species;
    const std::array<double,3> native{.9,1.1,.7};
    bool sampled=false;
    arch::boundary::GravityBoundaryFunction callback=
        [&](const arch::boundary::GravityBoundaryContext& ctx){
        sampled=true;
        require(std::abs(ctx.point.r-native[0])<1e-12,"native radius missing from PointCoords");
        require(std::abs(ctx.point.theta-native[1])<1e-12,"native polar angle missing from PointCoords");
        require(std::abs(ctx.point.phi-native[2])<1e-12,"native azimuth missing from PointCoords");
        require(std::abs(ctx.point.x-native[0]*std::sin(native[1])*std::cos(native[2]))<1e-12,
            "expanded Cartesian x mismatch");
        require(std::abs(ctx.point.z-native[0]*std::cos(native[1]))<1e-12,"expanded Cartesian z mismatch");
        require(std::abs(ctx.point.r_cy-native[0]*std::sin(native[1]))<1e-12,"cylindrical radius mismatch");
        require(std::abs(ctx.cartesian_normal[0]+std::sin(native[2]))<1e-12,"azimuthal normal x mismatch");
        require(std::abs(ctx.cartesian_normal[1]-std::cos(native[2]))<1e-12,"azimuthal normal y mismatch");
        require(ctx.cartesian_normal[2]==0.,"azimuthal normal z mismatch");
        return arch::boundary::GravityBoundaryData::Dirichlet(0.);
    };
    gravity_user_sample(callback,config,species,mesh,5,native,1.25);
    require(sampled,"user gravity face was never sampled");
    std::cout<<"boundary normals passed\n";
}

/** A callback may not poison a stage with an unknown kind, nonfinite
 *  coefficient, datum or time; reject before any boundary vector upload. */
void user_nonfinite() {
    SimConfig config;config.grid.dim=1;config.grid.nblockx1=4;config.grid.nblockx2=config.grid.nblockx3=0;
    config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="outflow";
    config.grid.x1_min=0.;config.grid.x1_max=1.;
    config.physics.gravity.boundary="user";
    SpeciesManager species;
    const double inf=std::numeric_limits<double>::infinity();
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const auto reject=[&](arch::boundary::GravityBoundaryFunction function,const char* what,double time=0.){
        arch::boundary::ResolvedUserBoundaries resolved;resolved.gravity=std::move(function);
        arch::boundary::ScopedUserBoundarySelection scope(resolved,config,species);
        Line f("outflow","user");
        SelfGravity gravity(config.physics.gravity);
        rejects([&]{gravity.bind(amr::bind_elliptic_mesh(f.control,f.config.grid,f.handles),time);},what);
    };
    reject([&](const arch::boundary::GravityBoundaryContext&){
        return arch::boundary::GravityBoundaryData::Robin(1.,1.,nan);},"nonfinite Robin datum accepted");
    reject([&](const arch::boundary::GravityBoundaryContext&){
        return arch::boundary::GravityBoundaryData::Robin(inf,1.,0.);},"nonfinite Robin coefficient accepted");
    reject([&](const arch::boundary::GravityBoundaryContext&){
        return arch::boundary::GravityBoundaryData::Neumann(nan);},"nonfinite Neumann datum accepted");
    reject([](const arch::boundary::GravityBoundaryContext&){
        return arch::boundary::GravityBoundaryData{
            static_cast<arch::boundary::GravityBoundaryCondition>(255),0.,1.,0.};
        },"unknown gravity boundary kind silently treated as Neumann");
    reject([](const arch::boundary::GravityBoundaryContext&){
        return arch::boundary::GravityBoundaryData::Dirichlet(0.);},"nonfinite boundary time accepted",nan);
    std::cout<<"user nonfinite rejections passed\n";
}

/** A changed Robin a/b must rebuild the actual operator on the same topology,
 *  and that rebuild must not restart the publication counter. */
void user_structure_rebuild() {
    SimConfig config;config.grid.dim=1;config.grid.nblockx1=4;config.grid.nblockx2=config.grid.nblockx3=0;
    config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="outflow";
    config.grid.x1_min=0.;config.grid.x1_max=1.;
    config.physics.gravity.boundary="user";
    SpeciesManager species;
    const double G=constants::gravity::cgs::gravitational_constant,L=1.,A=2.*constants::math::pi*G,B=-2.*constants::math::pi*G*L;
    const auto exact=[&](double x){return A*x*x+B*x;};
    const auto slope=[&](double x){return 2.*A*x+B;};
    arch::boundary::ResolvedUserBoundaries resolved;
    resolved.gravity=[&](const arch::boundary::GravityBoundaryContext& ctx){
        // Same exact solution, different side operator: a*Phi+b*dPhi/dn=c.
        const double a=ctx.time<1.?1.:2.;
        return arch::boundary::GravityBoundaryData::Robin(a,1.,
            a*exact(ctx.point.x)+ctx.cartesian_normal[0]*slope(ctx.point.x));
    };
    arch::boundary::ScopedUserBoundarySelection scope(resolved,config,species);
    Line f("outflow","user");
    SelfGravity gravity(config.physics.gravity);
    f.identity.input_time=0.5;
    gravity.bind(amr::bind_elliptic_mesh(f.control,f.config.grid,f.handles),0.5);
    const auto first=gravity.prepare({f.identity,f.views});
    line_error(f,gravity,exact,[&](double x){return -slope(x);},
        "first Robin structure potential","first Robin structure force");
    // The a=2 side is only reproduced if the operator itself was rebuilt; a
    // retained a=1 operator would solve a visibly different problem.
    f.identity.input_time=1.5;
    const auto second=gravity.prepare({f.identity,f.views});
    require(second.value>first.value,"structure rebuild restarted the publication counter");
    line_error(f,gravity,exact,[&](double x){return -slope(x);},
        "rebuilt Robin structure potential","rebuilt Robin structure force");
    std::cout<<"user structure rebuild passed\n";
}

/** A restart begins at a nonzero stage time: the initial side structure must be
 *  sampled there, and an execution swap must retain that time. */
void user_restart_time() {
    SimConfig config;config.grid.dim=1;config.grid.nblockx1=4;config.grid.nblockx2=config.grid.nblockx3=0;
    config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="outflow";
    config.grid.x1_min=0.;config.grid.x1_max=1.;
    config.physics.gravity.boundary="user";
    SpeciesManager species;
    arch::boundary::ResolvedUserBoundaries resolved;
    resolved.gravity=[](const arch::boundary::GravityBoundaryContext& ctx){
        if(!(ctx.time>0.)) throw std::invalid_argument("restart boundary time must be positive");
        return arch::boundary::GravityBoundaryData::Robin(1.,1.,0.);
    };
    arch::boundary::ScopedUserBoundarySelection scope(resolved,config,species);
    Line f("outflow","user");
    f.identity.input_time=2.5;
    SelfGravity gravity(config.physics.gravity);
    rejects([&]{gravity.bind(amr::bind_elliptic_mesh(f.control,f.config.grid,f.handles));},
        "initial side structure sampled at the rejected zero time");
    gravity.bind(amr::bind_elliptic_mesh(f.control,f.config.grid,f.handles),2.5);
    gravity.prepare({f.identity,f.views});
    require(gravity.report().residual<=gravity.report().target,"restart stage residual");
    gravity.set_execution(make_host_gravity_execution());
    gravity.prepare({f.identity,f.views});
    require(gravity.report().residual<=gravity.report().target,
        "execution swap lost the restart boundary time");
    std::cout<<"user restart time passed\n";
}

/** A periodic side is a topology statement: a nonzero payload is rejected while
 *  the canonical zero payload keeps working. */
void user_periodic_payload() {
    SimConfig config;config.grid.dim=1;config.grid.nblockx1=4;config.grid.nblockx2=config.grid.nblockx3=0;
    config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="periodic";
    config.physics.gravity.boundary="user";
    SpeciesManager species;
    {
        arch::boundary::ResolvedUserBoundaries invalid;
        invalid.gravity=[](const arch::boundary::GravityBoundaryContext&){
            return arch::boundary::GravityBoundaryData{
                arch::boundary::GravityBoundaryCondition::Periodic,0.,0.,1.};};
        arch::boundary::ScopedUserBoundarySelection scope(invalid,config,species);
        Line f("periodic","user");
        SelfGravity gravity(config.physics.gravity);
        rejects([&]{gravity.bind(amr::bind_elliptic_mesh(f.control,f.config.grid,f.handles));},
            "nonzero periodic payload accepted");
    }
    {
        arch::boundary::ResolvedUserBoundaries valid;
        valid.gravity=[](const arch::boundary::GravityBoundaryContext&){
            return arch::boundary::GravityBoundaryData::Periodic();};
        arch::boundary::ScopedUserBoundarySelection scope(valid,config,species);
        Line f("periodic","user");
        SelfGravity gravity(config.physics.gravity);
        gravity.bind(amr::bind_elliptic_mesh(f.control,f.config.grid,f.handles));
        gravity.prepare({f.identity,f.views});
        require(gravity.report().residual<=gravity.report().target,"periodic user boundary residual");
    }
    std::cout<<"user periodic payload passed\n";
}

}
/** Actual field energy and outward Green exchange for a changing Dirichlet gauge. */
void user_green_accounting() {
    SimConfig config; config.grid.dim=1;
    config.physics.gravity.boundary="user";
    SpeciesManager species;
    const double step=1.e-7;
    arch::boundary::ResolvedUserBoundaries callbacks;
    callbacks.gravity=[&](const arch::boundary::GravityBoundaryContext& context) {
        return arch::boundary::GravityBoundaryData::Dirichlet(step*context.time);
    };
    arch::boundary::ScopedUserBoundarySelection selected(callbacks,config,species);
    Line fixture("outflow","user"); SelfGravity gravity(config.physics.gravity);
    gravity.bind(amr::bind_elliptic_mesh(fixture.control,fixture.config.grid,fixture.handles));
    gravity.prepare({fixture.identity,fixture.views});
    const auto before=gravity.boundary_snapshot();
    fixture.identity.input_time=1.; gravity.prepare({fixture.identity,fixture.views});
    const auto after=gravity.boundary_snapshot();
    // rho=1 and V=1: d(1/2 integral rho*Phi)=1/2*step. Density and
    // normal forces remain unchanged, so the same change is boundary exchange.
    require(std::abs(after.potential_energy-before.potential_energy-.5*step)<2.e-9*step,
        "changing Dirichlet gauge lost field energy");
    require(std::abs(gravity_boundary_exchange(before,after)-.5*step)<2.e-9*step,
        "outward Green boundary work differs from the independent mass integral");
    require(before.faces.size()==2 && after.faces.size()==2,"surface sampling included internal faces");

    // Regrid overlap: a coarse face is exactly covered by four dyadic children.
    GravityBoundarySnapshot coarse,fine;
    coarse.mesh.dimension=fine.mesh.dimension=3;
    coarse.G=fine.G=1.;
    coarse.faces.push_back({{1,0,0,0},1.,2.,3.});
    for(int j=0;j<2;++j) for(int k=0;k<2;++k)
        fine.faces.push_back({{1,1,j,k},.25,4.,5.});
    const double expected=2./(8.*constants::math::pi);
    require(std::abs(gravity_boundary_exchange(coarse,fine)-expected)<1.e-15,
        "refined surface partition lost Green exchange");
    require(std::abs(gravity_boundary_exchange(fine,coarse)+expected)<1.e-15,
        "coarsened surface partition duplicated Green exchange");
    fine.faces.pop_back();
    rejects([&]{(void)gravity_boundary_exchange(coarse,fine);},"missing surface child silently dropped work");
}

int main(int argc,char** argv) { try {
    if(argc>1&&std::string_view(argv[1])=="native-rz-zero-scope"){native_rz_service_candidate(true);return 0;}
    if(argc>1&&std::string_view(argv[1])=="native-rz-service-candidate"){native_rz_service_candidate();return 0;}
    if(argc>1&&std::string_view(argv[1])=="native-rz-service-lifecycle"){native_rz_service_candidate(false,true);return 0;}
    dirichlet_quadratic();neumann_compatibility();mixed_periodic();
    user_robin_time();user_rejections();user_periodic_mismatch();boundary_normals();user_nonfinite();
    user_structure_rebuild();user_restart_time();user_periodic_payload();user_green_accounting();
    qualification_scope();request_identity_preflight();host_consumption_receipt();host_stage_diagnostic_journal();lifecycle();native_components();rz_binding_identity();rz_user_analytic_service();ring_execution_identity();std::cout<<"Self-gravity lifecycle validation passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
