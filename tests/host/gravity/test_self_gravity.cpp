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
#include <bit>
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
    require(stage.supports_macro_step_journal(arch::state::ExecutionSide::Host),"real Host journal capability absent");
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

/** Borrow actual raw scratch for the original controller, without granting a
 * field/typed consumer. Dirty unused siblings and a non-power-of-two maximum
 * must not change the original Host interval or its exact work diagnostics.
 */
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
    Grid meridional;meridional.dim=2;meridional.geometry="cylindrical";
    const auto meridional_point=Grid::PhysicalCoordsFromNative(2,"cylindrical",2.,sixth,0.);
    require(meridional_point.x==2.&&meridional_point.y==0.&&meridional_point.z==sixth,
        "axisymmetric axial length was interpreted as an azimuth");
    check(meridional,meridional_point,arch::boundary::BoundaryAxis::X1,arch::boundary::BoundarySide::Upper,
        {1.,0.,0.},"axisymmetric radial normal");
    check(meridional,meridional_point,arch::boundary::BoundaryAxis::X2,arch::boundary::BoundarySide::Upper,
        {0.,0.,1.},"axisymmetric axial normal");
    rejects([&]{arch::boundary::BoundaryCartesianNormal(meridional,meridional_point,
        arch::boundary::BoundaryAxis::X3,arch::boundary::BoundarySide::Upper);},
        "axisymmetric inactive phi normal accepted");
    // The spherical polar section supplies independent oblique normals.
    Grid spherical_section;spherical_section.dim=2;spherical_section.geometry="spherical";
    const auto oblique_point=Grid::PhysicalCoordsFromNative(2,"spherical",2.,sixth,0.);
    check(spherical_section,oblique_point,arch::boundary::BoundaryAxis::X1,arch::boundary::BoundarySide::Upper,
        {std::cos(sixth),std::sin(sixth),0.},"oblique radial normal");
    check(spherical_section,oblique_point,arch::boundary::BoundaryAxis::X2,arch::boundary::BoundarySide::Upper,
        {-std::sin(sixth),std::cos(sixth),0.},"oblique azimuthal normal");
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

/** Actual native binding: CGS density creates the independent quadratic
 * potential a(r^2+z^2). Check its force, field energy and original face-flux
 * work, plus isolated periodic-z rejection before a workspace can exist.
 */
void axisymmetric_field_and_work() {
    constexpr double a=.01,dt=.125,flux_density=.37;
    SimConfig config;config.grid.dim=2;config.grid.geometry="cylindrical";
    config.grid.nblockx1=config.grid.nblockx2=2;config.grid.nblockx3=0;
    config.grid.x1_min=0.;config.grid.x1_max=1.;config.grid.x2_min=-.5;config.grid.x2_max=.5;
    config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="reflecting";
    config.grid.x2l_boundary_type=config.grid.x2r_boundary_type="reflecting";
    config.physics.gravity.boundary="user";config.physics.gravity.relative_tolerance=1e-12;
    config.physics.gravity.absolute_tolerance=0.;config.physics.gravity.max_cycles=300;
    SpeciesManager species;
    boundary::ResolvedUserBoundaries callbacks;
    callbacks.gravity=[](const boundary::GravityBoundaryContext& context) {
        const auto& x=context.native_position;
        return boundary::GravityBoundaryData::Dirichlet(a*(x[0]*x[0]+x[1]*x[1]));
    };
    boundary::ScopedUserBoundarySelection selection(std::move(callbacks),config,species);
    amr::AMRControl control(16,2);control.tree->InitRootGrid(config,0);
    std::vector<amr::BlockHandle> handles;std::vector<GravityDensityView> views;
    GravitySolveIdentity identity;identity.topology={1};identity.gravitational_constant=constants::gravity::cgs::gravitational_constant;
    identity.operator_revision=identity.boundary_revision=identity.accuracy_revision=1;
    const double rho=6.*a/(4.*constants::math::pi*constants::gravity::cgs::gravitational_constant);
    for(int id:control.tree->GetActiveBlocks()) {
        auto& block=control.pool->GetBlock(id);handles.push_back({{static_cast<std::uint64_t>(id+1)},{1}});
        const GravityInputIdentity input{handles.back(),state::StateSlot::Current,{1},1};identity.inputs.push_back(input);
        views.push_back({input,{block.fluid_state.rho.data(),block.fluid_state.rho.size(),amr::native_scalar_layout(block.grid),grid::FieldMemory::Host,1}});
        for(int c=0;c<block.grid.GetTotalSize();++c)block.fluid_state.set(c,{rho,.1*rho,-.2*rho,.3*rho,20.*rho});
    }
    auto binding=amr::bind_elliptic_mesh(control,config.grid,handles);
    auto isolated_config=config.physics.gravity;isolated_config.boundary="isolated";
    SelfGravity rejected(isolated_config);auto periodic=binding;periodic.periodic[1]=true;
    rejects([&]{rejected.bind(periodic);},"isolated native periodic z accepted");
    rejects([&]{(void)rejected.cell_count();},"rejected isolated binding allocated a field workspace");
    SelfGravity gravity(config.physics.gravity);gravity.bind(binding);gravity.prepare({identity,views});
    const auto& phi=gravity.potential();const auto& g=gravity.acceleration();
    double phi_error=0.,force_error=0.,work_error=0.;long double expected_energy=0.;
    std::size_t cell=0;
    for(int id:control.tree->GetActiveBlocks()) {
        const auto& block=control.pool->GetBlock(id);const auto& grid=block.grid;
        std::vector<FluidVector> delta(grid.GetTotalSize()),flux(grid.GetTotalSize());
        for(auto& face:flux)face.rho=flux_density;
        gravity.add_sources_on_patch(delta,block.fluid_state,grid,dt);
        for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i,++cell) {
            const int c=grid.GetIndex(i,j,0);const double r=grid.GetCellCenterX(i),z=grid.GetCellCenterY(j),exact=a*(r*r+z*z);
            phi_error=std::max(phi_error,std::abs(phi[cell]-exact));
            force_error=std::max({force_error,std::abs(g[0][cell]+2.*a*r),std::abs(g[1][cell]+2.*a*z)});
            require(g[2][cell]==0.&&delta[c].mom_w==0.&&delta[c].eng==0.,"self gravity introduced phi torque or duplicate energy work");
            require(std::abs(delta[c].mom_u-dt*rho*g[0][cell])<1e-12*rho&&std::abs(delta[c].mom_v-dt*rho*g[1][cell])<1e-12*rho,"native force used wrong momentum slots");
            expected_energy+=.5L*rho*exact*GridMetrics::CellVolume(grid,i,j,0);
        }
        for(int axis=0;axis<2;++axis) {
            for(auto& value:delta)value={};gravity.add_flux_work_on_patch(delta,flux,block.fluid_state,grid,dt,axis);
            for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
                const int c=grid.GetIndex(i,j,0);const double r=grid.GetCellCenterX(i),z=grid.GetCellCenterY(j);
                const double h=axis==0?grid.dx1:grid.dx2,coordinate=axis==0?r:z;
                // Existing multidimensional gravity work shares a LINEAR
                // interior face potential: averaging this quadratic adds
                // a*h^2/4 to its point-face value. Physical Dirichlet faces
                // use the prescribed point datum. Keep that original scheme.
                const double domain_lo=axis==0?config.grid.x1_min:config.grid.x2_min;
                const double domain_hi=axis==0?config.grid.x1_max:config.grid.x2_max;
                const bool physical_lo=coordinate-.5*h==domain_lo,physical_hi=coordinate+.5*h==domain_hi;
                const double low=a*(-h*coordinate+(physical_lo?.25:.5)*h*h),
                    high=a*(h*coordinate+(physical_hi?.25:.5)*h*h);
                const double exact=dt*flux_density*(GridMetrics::FaceArea(grid,axis,i,j,0,false)*low-GridMetrics::FaceArea(grid,axis,i,j,0,true)*high)/GridMetrics::CellVolume(grid,i,j,0);
                work_error=std::max(work_error,std::abs(delta[c].eng-exact));
            }
        }
    }
    std::cout<<"AXISYMMETRIC_FIELD_WORK_ERRORS phi="<<phi_error<<" force="<<force_error<<" work="<<work_error<<'\n';
    require(phi_error<1e-8&&force_error<1e-7&&work_error<1e-8,"native quadratic field/force/work budget");
    const auto snapshot=gravity.boundary_snapshot();
    const double energy_error=static_cast<double>(std::abs(snapshot.potential_energy-expected_energy)/std::abs(expected_energy));
    require(energy_error<1e-8,"native field energy lost per-radian normalization");
    gravity.invalidate();rejects([&]{gravity.potential();},"native invalidated field remained readable");
    gravity.prepare({identity,views});auto bad=identity;bad.inputs.back().version={0};
    rejects([&]{gravity.prepare({bad,views});},"native invalid density identity accepted");
    rejects([&]{gravity.potential();},"native failed solve retained publication");
    std::cout<<"AXISYMMETRIC_FIELD_WORK_PASS cells="<<cell<<" phi_error="<<phi_error<<" force_error="<<force_error
        <<" work_error="<<work_error<<" field_energy_relative="<<energy_error<<" phi_torque=0 periodic_z_rejected=1 invalidation=1\n";
}

int main(int argc,char** argv) { try {
    axisymmetric_field_and_work();
    dirichlet_quadratic();neumann_compatibility();mixed_periodic();
    user_robin_time();user_rejections();user_periodic_mismatch();boundary_normals();user_nonfinite();
    user_structure_rebuild();user_restart_time();user_periodic_payload();user_green_accounting();
    request_identity_preflight();host_consumption_receipt();host_stage_diagnostic_journal();lifecycle();native_components();std::cout<<"Self-gravity lifecycle validation passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
