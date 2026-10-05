#include "physics/gravity/GravitySolveTypes.h"
#include "physics/gravity/GravityExecution.h"
#include "amr/elliptic/EllipticMeshAdapter.h"
#include "numerics/multigrid/CompositeMultigrid.h"
#include "physics/gravity/self/SelfGravity.h"
#include "physics/gravity/self/GravityWorkspace.h"
#include "amr/AMRControl.h"
#include "physics/constant/PhysicalConstants.h"
#include "core/config/ControlRelations.h"
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
    GravityBoundary tree(op,{9});GravitySolveIdentity identity;
    identity.topology={9};identity.gravitational_constant=constants::gravity::cgs::gravitational_constant;
    identity.operator_revision=identity.boundary_revision=identity.accuracy_revision=1;
    identity.inputs.push_back({{{1},{9}},state::StateSlot::Current,{1},1});
    std::vector<double> density(op.size(),1.);tree.update(density,identity);
    RingBoundaryControl control;control.face_absolute_target=1.e-18;
    control.maximum_boxes_per_leaf=65536;
    RingBoundaryEvaluation result;auto execution=make_host_gravity_execution();
    execution->run(EvaluateRingBoundary{&tree,&op,&identity,&control,&result});
    require(result.status==RingBoundaryStatus::Bounded && result.source==identity
        && result.source_generation>0 && result.values.size()==op.faces().size(),
        "typed ring executor lost bounded source identity");
    tree.require_current_ring(op,result);
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

}
int main() { try {request_identity_preflight();lifecycle();native_components();rz_binding_identity();ring_execution_identity();std::cout<<"Self-gravity lifecycle validation passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
