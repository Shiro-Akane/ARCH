/** @file test_gravity_stage_contract.cpp
 * @brief Whole-domain preparation ordering and gravity publication validity.
 */
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "host/driver/RzRuntimeWitness.h"
#include "host/gravity/NativeSelfEnergyWitness.h"

#include "amr/AMRControl.h"
#include "amr/elliptic/EllipticMeshAdapter.h"
#include "driver/DriverUtils.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "driver/schedule/StageScheduler.h"
#include "driver/stages/DriverMacroStep.h"
#include "driver/stages/DriverStages.h"
#include "driver/stages/GravityStage.h"
#include "numerics/flux/FluxHLLC.h"
#include "numerics/integrator/HydroSolverImpl.h"
#include "numerics/integrator/TimeIntegratorEuler.h"
#include "numerics/integrator/TimeIntegratorRK2.h"
#include "numerics/integrator/TimeIntegratorRK3.h"
#include "numerics/reconstruction/Reconstruction.h"
#include "numerics/state/RzCellAverage.h"
#include "physics/eos/IdealGas.h"
#include "physics/gravity/GravityExecution.h"
#include "physics/gravity/GravitySolveTypes.h"
#include "physics/gravity/GravitySource.h"
#include "physics/gravity/NativeSelfStage.h"
#include "physics/gravity/self/GravityWorkspace.h"
#include "physics/gravity/self/SelfGravity.h"

void test_host_hydro_transaction();
void run_native_rz_runtime_boundary_contract();

namespace {
using namespace arch::scheduler;
using namespace arch::state;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class Action> void rejects(Action&& action, const char* message) {
    try { action(); } catch (const std::logic_error&) { return; }
    throw std::runtime_error(message);
}
struct Fixture {
    std::array<amr::BlockHandle, 2> handles{{{{1}, {1}}, {{2}, {1}}}};
    StateResidencyLedger ledger{amr::TopologyEpoch{1}};
    MonotonicSchedulerClock clock;
    StageExecutionContext context{ExecutionSide::Host, ledger, clock};
    Fixture(bool ghosts = true) {
        const auto witness = clock.next_publication();
        for (const auto handle : handles) {
            ledger.register_block(handle, witness.version, witness.completion);
            if (ghosts)
                ledger.publish_ghost({handle, StateSlot::Current}, ExecutionSide::Host,
                                     witness.version, witness.completion);
        }
        context.step_start_time = 2.0;
        context.step_dt = 0.25;
    }
};
struct Probe : HydroStagePreparation {
    std::vector<StateSlot> slots;
    std::vector<double> times;
    std::vector<std::vector<StateVersion>> versions;
    int executions = 0;
    bool pending = false, fail = false;
    CompletionToken prepare(const HydroStagePreparationRequest& request) override {
        require(request.handles.size() == 2, "preparation is domain-wide, not per patch");
        require(executions == static_cast<int>(slots.size()), "prepare precedes each executor");
        require(request.step_dt == 0.25, "preparation receives the Hydro step size");
        slots.push_back(request.descriptor.input_slot);
        times.push_back(request.input_time);
        versions.emplace_back();
        for (auto handle : request.handles) {
            const StateKey key{handle, request.descriptor.input_slot};
            const auto current = request.ledger.inspect(key).interior.version;
            request.ledger.require_readable(key, {request.side, current, true, true});
            versions.back().push_back(current);
        }
        if (fail) throw std::logic_error("controlled field preparation failure");
        return {123, pending ? CompletionState::Pending : CompletionState::Complete};
    }
};
void run_hydro(Fixture& f, Probe& p, HydroMethod method) {
    f.context.hydro_preparation = &p;
    execute_hydro_lane(f.context, f.handles, method,
        [&](const StageDescriptor&, CompletionToken token) {
            require(p.slots.size() == static_cast<std::size_t>(p.executions + 1),
                    "completed preparation must precede block execution");
            ++p.executions;
            return token;
        },
        [](StateSlot, StateVersion, CompletionToken token) { return token; },
        [](SlotRotation) {},
        [](const HydroPlan&, StateSlot, CompletionToken token) { return token; });
}
/** Native external source integration only, not a field/frame or stage grant.
 * Workflow: obtain one positive cell from the real bound Tree; furnish and
 * actual-EOS-check immutable physical Gauss states; compare signed V/W source
 * means with independent antiderivatives; reject arithmetic/invalid inputs.
 * Physical rho is positive, but a source's mass component may be zero or signed.
 */
void test_native_external_source_mean()
{
    namespace gravity=Physical::Gravity;
    using MeanStatus=RzCellAverage::Status;
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    const auto relative=[](double actual,long double expected,const char* message) {
        require(std::isfinite(actual)&&std::isfinite(expected)
            &&std::abs(static_cast<long double>(actual)-expected)
                <=64.L*std::numeric_limits<double>::epsilon()*std::abs(expected),message);
    };
    SimConfig config{};config.grid.dim=2;config.grid.geometry="cylindrical";
    config.grid.nblockx1=config.grid.nblockx2=1;config.grid.nblockx3=0;
    config.grid.x1_min=1.;config.grid.x1_max=33.;
    config.grid.x2_min=0.;config.grid.x2_max=32.;
    config.grid.amr_max_blocks=8;config.amr.lrefinemin=config.amr.lrefinemax=0;
    amr::AMRControl control{8,2};control.tree->InitRootGrid(config,1,rz);
    const auto& active=control.tree->GetActiveBlocks();
    require(active.size()==1,"external source math lacks one actual Tree cell owner");
    const auto& block=control.pool->GetBlock(active.front());
    block.RequireNativeGeometryIdentity();const auto& grid=block.grid;
    const auto view=GridMetrics::make_geometry_view(grid,rz);
    const int i=grid.Is(),j=grid.Js();
    const double lower=view.GetFacePosL(i),upper=view.GetFacePosR(i);
    const double zl=view.GetAxialFacePosL(j),zh=view.GetAxialFacePosR(j);
    require(grid.dyadic_identity.bound&&lower==1.&&upper==3.&&zl==0.&&zh==2.,
        "external source math lost its actual positive [1,3]x[0,2] cell");
    const auto samples=GridMetrics::Rz::CellAverageSamples(lower,upper,zl,zh);
    const auto sample_before=samples;
    SpeciesManager material;material.add_species("external-source-gas",1.,1.,1.4,2.);
    IdealGas eos(1.4,material);const double xi[1]{1.};
    const arch::state::Bounds bounds{config.numerics.sml_rho,
        config.numerics.min_eint,config.numerics.max_eint};
    std::array<FluidVector,8> points;
    // Independent true field: rho=2, ur=1, uz=-2, uphi=r, e=3.
    // E=2e+rho*(ur^2+uz^2+uphi^2)/2=11+r^2, not a native J/W point.
    for(std::size_t n=0;n<samples.size();++n) {
        const double r=samples[n].radius;points[n]={2.,2.,-4.,2.*r,11.+r*r};
        require(arch::state::validate_eos(points[n],xi,1,bounds,eos)==Status::valid,
            "external source mathematical input failed its real selected EOS");
    }
    const auto physical_before=points;const auto physical_address=points.data();
    const auto same_points=[](const auto& current,const auto& before) {
        for(std::size_t n=0;n<current.size();++n)
            for(const auto component:std::array<double FluidVector::*,5>{
                &FluidVector::rho,&FluidVector::mom_u,&FluidVector::mom_v,
                &FluidVector::mom_w,&FluidVector::eng})
                require(std::bit_cast<std::uint64_t>(current[n].*component)
                    ==std::bit_cast<std::uint64_t>(before[n].*component),
                    "external source integration modified immutable point bits");
    };
    const auto sample_unchanged=[&] {
        for(std::size_t n=0;n<samples.size();++n)
            for(const auto component:std::array<double GridMetrics::Rz::CellAverageSample::*,4>{
                &GridMetrics::Rz::CellAverageSample::radius,&GridMetrics::Rz::CellAverageSample::axial,
                &GridMetrics::Rz::CellAverageSample::volume_weight,
                &GridMetrics::Rz::CellAverageSample::angular_weight})
                require(std::bit_cast<std::uint64_t>(samples[n].*component)
                    ==std::bit_cast<std::uint64_t>(sample_before[n].*component),
                    "external source integration changed actual quadrature metadata");
    };
    const auto reader=[&](std::size_t n){return points[n];};
    const auto physical_mean=RzCellAverage::conserved_mean(samples,reader);
    require(physical_mean.valid(),"positive physical conserved mean no longer accepted");
    // True antiderivatives V=int_1^3 r dr=4, W=int_1^3 r^2 dr=26/3,
    // C=int_1^3 r^3 dr=20. Work uses 2*W/V=13/3; stored mphi is 2*C/W=60/13.
    relative(physical_mean.value.rho,2.L,"physical state V density mean");
    relative(physical_mean.value.mom_w,60.L/13.L,"physical state W angular mean");
    relative(physical_mean.value.eng,16.L,"physical state V energy mean");
    constexpr double dt=.25;
    const auto check_force=[&](gravity::ExternalGravityView force) {
        const auto result=gravity::native_external_source_mean(samples,reader,force,dt);
        require(result.valid()&&result.value.rho==0.,"native external source has nonzero mass or failed");
        // g_x/g_y/g_z are local g_r/g_z/g_phi in this explicit native math API.
        relative(result.value.mom_u,2.L*dt*force.g_x,"native external radial V source");
        relative(result.value.mom_v,2.L*dt*force.g_y,"native external axial V source");
        relative(result.value.mom_w,2.L*dt*force.g_z,"native external azimuthal W source");
        relative(result.value.eng,dt*(2.L*force.g_x-4.L*force.g_y+(13.L/3.L)*force.g_z),
            "native external physical V work used stored J/W or an extra torque lever");
        same_points(points,physical_before);sample_unchanged();
    };
    for(double sign:{-1.,1.})check_force({0.,0.,sign,true});
    for(double gr:{-.75,.75})for(double gz:{-1.25,1.25})for(double gp:{-.875,.875})
        check_force({gr,gz,gp,true});
    const auto disabled=gravity::native_external_source_mean(samples,reader,{3.,-4.,5.,false},dt);
    require(disabled.valid()&&disabled.value.rho==0.&&disabled.value.mom_u==0.
        &&disabled.value.mom_v==0.&&disabled.value.mom_w==0.&&disabled.value.eng==0.,
        "disabled finite external view did not return valid zero source");

    const auto failed=[](const auto& result,MeanStatus status,const char* message) {
        require(!result.valid()&&result.status==status,message);
        for(const auto component:std::array<double FluidVector::*,5>{
            &FluidVector::rho,&FluidVector::mom_u,&FluidVector::mom_v,
            &FluidVector::mom_w,&FluidVector::eng})
            require(std::isnan(result.value.*component),"failed external/source mean returned a usable fallback component");
    };
    for(double mass_source:{-2.,0.,2.}) {
        const FluidVector source{mass_source,3.,-4.,5.,-6.};
        const auto signed_reader=[&](std::size_t){return source;};
        const auto source_mean=RzCellAverage::source_components_mean(samples,signed_reader);
        require(source_mean.valid(),"signed or zero source mass was subjected to physical rho validation");
        relative(source_mean.value.rho,mass_source,"signed source V mass mean");
        relative(source_mean.value.mom_u,3.L,"signed source V radial mean");
        relative(source_mean.value.mom_v,-4.L,"signed source V axial mean");
        relative(source_mean.value.mom_w,5.L,"signed source W angular mean");
        relative(source_mean.value.eng,-6.L,"signed source V energy mean");
        const auto state_mean=RzCellAverage::conserved_mean(samples,signed_reader);
        if(mass_source<=0.)failed(state_mean,MeanStatus::invalid_density,
            "physical conserved mean silently relaxed its positive density contract");
        else require(state_mean.valid(),"positive conserved mean was rejected by source factoring");
    }
    const gravity::ExternalGravityView force{.75,-1.25,.875,true};
    for(double step:{0.,-0.,-1.})failed(gravity::native_external_source_mean(samples,reader,force,step),
        MeanStatus::invalid_weight,"nonpositive external source dt accepted");
    for(double step:{std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        failed(gravity::native_external_source_mean(samples,reader,force,step),
            MeanStatus::nonfinite_state,"nonfinite external source dt accepted");
    for(int axis=0;axis<3;++axis)for(bool enabled:{false,true}) {
        auto invalid=force;invalid.enabled=enabled;
        if(axis==0)invalid.g_x=std::numeric_limits<double>::quiet_NaN();
        if(axis==1)invalid.g_y=std::numeric_limits<double>::infinity();
        if(axis==2)invalid.g_z=-std::numeric_limits<double>::infinity();
        failed(gravity::native_external_source_mean(samples,reader,invalid,dt),
            MeanStatus::nonfinite_state,"invalid external view bypassed finite validation");
    }
    for(double rho:{0.,-2.}) {
        auto bad=points;bad[0].rho=rho;const auto bad_before=bad;
        failed(gravity::native_external_source_mean(samples,[&](std::size_t n){return bad[n];},force,dt),
            MeanStatus::invalid_density,"nonpositive physical external input density accepted");
        same_points(bad,bad_before);
    }
    for(const auto component:std::array<double FluidVector::*,5>{
        &FluidVector::rho,&FluidVector::mom_u,&FluidVector::mom_v,&FluidVector::mom_w,&FluidVector::eng}) {
        auto bad=points;bad[0].*component=std::numeric_limits<double>::quiet_NaN();const auto bad_before=bad;
        failed(gravity::native_external_source_mean(samples,[&](std::size_t n){return bad[n];},force,dt),
            MeanStatus::nonfinite_state,"nonfinite physical external input accepted");
        same_points(bad,bad_before);
    }
    // Finite genuine EOS points and a finite force can require nonrepresentable
    // energy. Mean work is (13/12)*max_double here; no physical floor fixes it.
    failed(gravity::native_external_source_mean(samples,reader,
        {0.,0.,std::numeric_limits<double>::max(),true},dt),MeanStatus::nonfinite_state,
        "overflowing finite-force physical energy source was accepted");

    // Every nonzero tiny source component loses an actual Gauss contribution.
    // This is a mathematical representation refusal, not a dimensional cutoff.
    const double tiny=std::numeric_limits<double>::denorm_min();
    for(const auto component:std::array<double FluidVector::*,5>{
        &FluidVector::rho,&FluidVector::mom_u,&FluidVector::mom_v,&FluidVector::mom_w,&FluidVector::eng})
        for(double sign:{-1.,1.}) {
            FluidVector source{};source.*component=sign*tiny;
            failed(RzCellAverage::source_components_mean(samples,[&](std::size_t){return source;}),
                MeanStatus::unrepresentable,"signed source weighted contribution silently disappeared");
        }
    const double tiny_gp=8.*tiny,point_source_phi=(dt*2.)*tiny_gp;
    require(point_source_phi>0.&&samples[0].angular_weight*point_source_phi==0.,
        "external weighted-loss input has no genuine nonzero lost contribution");
    failed(gravity::native_external_source_mean(samples,reader,{0.,0.,tiny_gp,true},dt),
        MeanStatus::unrepresentable,"native external angular weighted loss silently returned zero");
    for(int fault=0;fault<6;++fault) {
        auto bad_samples=samples;
        if(fault==0)bad_samples[0].radius=-1.;
        if(fault==1)bad_samples[0].axial=std::numeric_limits<double>::quiet_NaN();
        if(fault==2)bad_samples[0].volume_weight=0.;
        if(fault==3)bad_samples[0].angular_weight=-1.;
        if(fault==4)bad_samples[0].volume_weight=std::numeric_limits<double>::infinity();
        if(fault==5)bad_samples[0].angular_weight=std::numeric_limits<double>::quiet_NaN();
        failed(gravity::native_external_source_mean(bad_samples,reader,force,dt),
            fault<2?MeanStatus::invalid_coordinate:MeanStatus::invalid_weight,
            "invalid actual-rule metadata was integrated as external source");
    }
    auto huge_weights=samples;huge_weights[0].volume_weight=std::numeric_limits<double>::max();
    const FluidVector huge_source{0.,0.,0.,0.,std::numeric_limits<double>::max()};
    failed(RzCellAverage::source_components_mean(huge_weights,[&](std::size_t){return huge_source;}),
        MeanStatus::unrepresentable,"finite weighted source overflow produced a fallback mean");
    require(points.data()==physical_address,"external source point ownership changed");
    same_points(points,physical_before);sample_unchanged();
    std::cout<<"NATIVE_EXTERNAL_SOURCE_MATH_PASS actual_grid_samples=true physical_EOS=true V_W_work=true source_signed_rho=true Runtime_or_field_qualified=false\n";
}


namespace reflux_row_checks {
constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
/** This owner really initializes Runtime identity, BC, actual EOS and ledger.
 * Fluid is a warm nonrotating constant; arbitrary stage Phi below is only a
 * row-algebra stimulus, not a solved or physically qualified gravity field. */
struct NativeOwner {
    SimConfig config{};
    SpeciesManager species;
    amr::AMRControl control{16,2};
    std::unique_ptr<IdealGas> eos;
    RunState start{};
    std::unique_ptr<SimulationController> clock;
    std::unique_ptr<BCHandler> boundary;
    std::unique_ptr<arch::driver::DriverRuntime> runtime;
    NativeOwner(int axis,double radial_lower,bool mixed,bool periodic) {
        config.grid.dim=2;config.grid.geometry="cylindrical";
        config.grid.nblockx1=axis==0?2:1;config.grid.nblockx2=axis==0?1:2;
        config.grid.nblockx3=0;config.grid.amr_max_blocks=16;
        config.grid.x1_min=radial_lower;config.grid.x1_max=1.3;
        config.grid.x2_min=-.3;config.grid.x2_max=.7;
        config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="outflow";
        config.grid.x2l_boundary_type=config.grid.x2r_boundary_type=periodic?"periodic":"outflow";
        config.amr.lrefinemin=0;config.amr.lrefinemax=mixed?1:0;
        config.physics.gravity.type="none";config.physics.burn.use_burn=false;
        config.physics.diffusion.use_diffusion=false;
        config.numerics.sml_rho=1.e-14;config.numerics.min_eint=1.e-14;config.numerics.max_eint=1.e6;
        config.io.tmax=.1;config.io.plt_dt=config.io.chk_dt=-1.;
        species.add_species("work-row-gas",1.,1.,1.4,2.);
        eos=std::make_unique<IdealGas>(1.4,species);
        if(mixed)control.tree->LoadLeafGrid(config,1,{1,1,1,1,0},
            {0,1,0,1,axis==0?1u:0u},{0,0,1,1,axis==0?0u:1u},{0,0,0,0,0},rz);
        else control.tree->InitRootGrid(config,1,rz);
        control.flux_register.EnsureSpecies(1);
        for(int id:control.tree->GetActiveBlocks()) {
            auto& b=control.pool->GetBlock(id);b.RequireNativeGeometryIdentity();
            for(auto member:{&amr::Block::fluid_state,&amr::Block::state_scratch,&amr::Block::state_next}) {
                auto& s=b.*member;s.stage_repairs.reset(1,RepairSemantics::RzVolumeAngular);
                for(int c=0;c<b.grid.GetTotalSize();++c){s.set(c,{2.,0.,0.,0.,100.});s.X(0,c)=1.;s.enuc_rate[c]=0.;}
            }
        }
        start.repairs.reset(1,RepairSemantics::RzVolumeAngular);
        clock=std::make_unique<SimulationController>(config,start);
        boundary=std::make_unique<BCHandler>(config,rz);boundary->bind(*eos,species);
        boundary->configure_stage(0.,arch::boundary::BoundaryPurpose::Hydro);
        runtime=std::make_unique<arch::driver::DriverRuntime>(control,*boundary,config,species,*clock);
        runtime->bind_native_rz_eos(*eos);runtime->initialize_topology();
    }
};
/** Evaluate already-built scalar rows in long double independently of backend
 * sparse gather. This is a local64eps rounding comparison, never a PDE oracle. */
long double row(const arch::multigrid::SparseStorage& rows,std::size_t index,const std::vector<double>& x) {
    long double value=0.;
    for(int k=rows.offsets.at(index);k<rows.offsets.at(index+1);++k)
        value+=static_cast<long double>(rows.values.at(k))*x.at(rows.columns.at(k));
    return value;
}
void near(long double actual,long double expected,const char* text) {
    require(std::isfinite(actual)&&std::isfinite(expected)
        &&std::abs(actual-expected)<=64.L*std::numeric_limits<double>::epsilon()*std::max(1.L,std::abs(expected)),text);
}
/** Reject before caller publication: assignment happens only after complete
 * builder return; the real register and real source/ledger remain unchanged. */
template<class F> void refused(F&& call,const amr::FluxRegister& reg,const amr::FluxRegister::HostSnapshot& before) {
    bool rejected=false,published=false;
    try {auto unusable=call();(void)unusable;published=true;}catch(const std::exception&){rejected=true;}
    require(rejected&&!published&&reg.host_snapshot_matches(before),"invalid reflux geometry published rows or changed actual register");
}
/** Use true op face incidences as the independent registration oracle. It
 * directly multiplies each actual fragment Phi by its actual donor mass flux,
 * and uses the original destination box/active storage to find the coarse Phi.
 * No builder coefficients or final-Phi-times-total-mass shortcut is used. */
void run_case(int axis,double radial_lower,bool mixed,bool periodic) {
    std::cout<<"NATIVE_REFLUX_ROW_CASE_BEGIN axis="<<axis<<" radial_lower="<<radial_lower
        <<" mixed="<<mixed<<" periodic_z="<<periodic<<std::endl;
    NativeOwner owner(axis,radial_lower,mixed,periodic);
    if(radial_lower>0.)for(int id:owner.control.tree->GetActiveBlocks()) {
        const auto& grid=owner.control.pool->GetBlock(id).grid;
        require(grid.GetFacePosL(0)>=0.,
            "new off-axis work-row fixture has an invalid actual physical halo");
    }
    auto& control=owner.control;
    const auto binding=amr::bind_elliptic_mesh(control,owner.config.grid,owner.runtime->handles());
    arch::elliptic::CompositeBoundary policy{};
    policy.sides.fill(arch::elliptic::FaceBoundaryKind::Neumann);
    for(int side=0;side<6;++side)policy.conditions[side]={arch::elliptic::FaceBoundaryKind::Neumann,0.,1.};
    if(periodic)for(int side:{2,3}){policy.sides[side]=arch::elliptic::FaceBoundaryKind::Periodic;
        policy.conditions[side]={arch::elliptic::FaceBoundaryKind::Periodic,0.,1.};}
    policy.constant_nullspace=true;
    arch::elliptic::CompositePoisson op(binding.base,binding.cells,policy);
    const auto& topology=control.RequireFluxTopologyPlan(1,rz,-1,true);
    const auto rows=Physical::Gravity::gravity_reflux_rows(binding,op,topology);
    const auto register_before=control.flux_register.snapshot_host();
    require(control.flux_register.host_snapshot_matches(register_before),"builder changed actual register arena");
    const auto ledger_before=owner.runtime->stage_context().ledger.snapshot_host();
    require(rows.potential.offsets.size()==rows.identity.size()+1&&rows.boundary.offsets.size()==rows.identity.size()+1,
        "reflux rows lost independent sparse row alignment");
    if(!mixed) {require(rows.identity.empty()&&topology.routes.empty(),"uniform actual Runtime fabricated CF rows");return;}
    require(!rows.identity.empty(),"actual mixed Runtime produced no Energy work rows");
    const auto cell=[&](const amr::AmrEndpoint& endpoint,const amr::LogicalAmrBox& box) {
        const int pool_id=topology.pool_lowering.at(endpoint);const auto& g=control.pool->GetBlock(pool_id).grid;
        const int offset=g.GetIndex(g.Is()+box.first[0],g.Js()+box.first[1],g.Ks()+box.first[2]);
        for(int c=0;c<op.size();++c)if(binding.grids[binding.storage[c].block]==&g&&binding.storage[c].offset==offset)return c;
        throw std::runtime_error("independent destination box has no actual elliptic cell");
    };
    struct Key {int block,face,cell;auto operator<=>(const Key&) const=default;};
    bool covariance=false,fine_counterterm_distinct=false,periodic_seam=false;
    for(const auto method:{HydroMethod::Euler,HydroMethod::RK2,HydroMethod::RK3}) {
        control.flux_register.Clear();std::map<Key,long double> expected_mass,expected_energy;
        const auto plan=make_hydro_plan(method);
        for(const auto& stage:plan.stages) {
            std::vector<double> phi(op.size()),datum(op.faces().size());
            for(int c=0;c<op.size();++c){const auto p=op.center(c);phi[c]=1.+.2*stage.stage+(.7+.1*stage.stage)*p[0]+(1.3+.2*stage.stage)*p[1];}
            for(std::size_t f=0;f<datum.size();++f)datum[f]=2.+.01*f;
            std::vector<std::vector<double>> values(topology.routes.size());
            for(std::size_t route=0;route<topology.routes.size();++route)values[route].assign(topology.routes[route].plan.operations.size(),0.);
            std::map<Key,std::array<long double,4>> fine_products;
            for(std::size_t ri=0;ri<rows.identity.size();++ri) {
                const auto& identity=rows.identity[ri];const auto route_it=topology.route_index.at(identity.route);
                const auto& route=topology.routes[route_it];const auto& operation=route.plan.operations[identity.operation_index];
                require(identity.operation==operation&&identity.epoch==topology.epoch
                    &&identity.topology_fingerprint==topology.fingerprint&&identity.route_fingerprint==route.plan.fingerprint,
                    "work row lost original Energy route/fingerprint/epoch identity");
                const bool fine=operation.rule==amr::RefinementRule::FineFluxContribution;
                const bool high=operation.side==amr::AmrSide::Upper;
                const int direction=amr::axis_value(operation.axis),tangent=1-direction;
                const int dc=cell(operation.destination,operation.destination_box);
                auto source_box=operation.source_box;const int n=direction==0?amr::BLOCK_NX:amr::BLOCK_NY;
                const bool source_high=source_box.first[direction]==n;
                if(source_high)--source_box.first[direction];const int sc=cell(operation.source,source_box);
                const auto& sg=control.pool->GetBlock(identity.route.source_block).grid;
                require(identity.coarse_cell_index==dc&&identity.source_flux_offset==sg.GetIndex(
                    sg.Is()+operation.source_box.first[0],sg.Js()+operation.source_box.first[1],sg.Ks()),
                    "work row used fine Phi or changed original padded face offset");
                const double source_area=GridMetrics::FaceArea(GridMetrics::make_geometry_view(sg,rz),direction,
                    sg.Is()+source_box.first[0],sg.Js()+source_box.first[1],sg.Ks(),source_high);
                long double weighted_phi=0.,area_sum=0.;int count=0;
                for(std::size_t fi=0;fi<op.faces().size();++fi) {
                    const auto& face=op.faces()[fi];if(face.axis!=direction||face.boundary_side>=0)continue;
                    if(source_high?face.left!=sc:face.right!=sc)continue;
                    const int other=source_high?face.right:face.left;
                    if(other<0||(fine?other!=dc:op.cells()[other].level!=op.cells()[dc].level+1))continue;
                    long double face_phi=face.value_boundary_coefficient*datum[fi];
                    for(std::size_t k=0;k<face.value_samples.size();++k)face_phi+=static_cast<long double>(face.value_coefficients[k])*phi[face.value_samples[k]];
                    weighted_phi+=face.area*face_phi;area_sum+=face.area;++count;
                    if(direction==1&&std::abs(op.cells()[sc].index[1]-(fine?2*op.cells()[dc].index[1]:op.cells()[other].index[1]/2))>2)periodic_seam=true;
                }
                require(count==(fine?1:2),"independent actual CF incidence coverage is incomplete");
                near(area_sum,source_area,"original actual source-face area coverage");
                const long double direct=weighted_phi/source_area-phi[dc];
                const long double built=row(rows.potential,ri,phi)+row(rows.boundary,ri,datum);
                near(built,direct,"stage work row differs from fragment Phi minus true coarse target");
                if(fine&&std::abs(phi[sc]-phi[dc])>1.e-5)fine_counterterm_distinct=true;
                const auto p=op.center(sc);const double mass=(fine?2.:.7)+.4*p[tangent]+.1*stage.stage;
                const double energy=1.+.3*p[0]+.2*p[1]+.01*stage.stage;
                const double gather=energy+double(built)*mass;
                values[route_it][identity.operation_index]=gather;
                // The real registration plan contains Rho/Species siblings of
                // this SAME face; fill them without replacing their coefficients.
                for(std::size_t oi=0;oi<route.plan.operations.size();++oi) {
                    const auto& sibling=route.plan.operations[oi];
                    if(sibling.source_box==operation.source_box&&sibling.destination_box==operation.destination_box
                        &&sibling.destination==operation.destination&&(sibling.field==amr::AmrField::Rho||sibling.field==amr::AmrField::Species))values[route_it][oi]=mass;
                }
                const int face=2*direction+int(high);
                const int destination_face_cell=direction==0?operation.destination_box.first[1]:operation.destination_box.first[0];
                const Key key{topology.pool_lowering.at(operation.destination),face,destination_face_cell};
                const long double signed_weight=(fine?1.L:-1.L)*operation.weight*stage.flux_register_weight;
                expected_mass[key]+=signed_weight*mass;expected_energy[key]+=signed_weight*(energy+direct*mass);
                if(fine){auto& products=fine_products[key];products[0]+=operation.weight;
                    products[1]+=operation.weight*(direct+phi[dc]);products[2]+=operation.weight*mass;
                    products[3]+=operation.weight*(direct+phi[dc])*mass;}
            }
            for(const auto& [key,products]:fine_products) {
                const long double lost=products[3]-products[1]*products[2]/products[0];
                if(std::abs(lost)>1.e-8)covariance=true;
                (void)key;
            }
            for(std::size_t route=0;route<topology.routes.size();++route)
                control.flux_register.ApplyRegistrationPlan(topology.routes[route].plan,values[route],topology.pool_lowering,
                    stage.flux_register_weight,topology.fingerprint);
        }
        for(const auto& [key,energy]:expected_energy) {
            const auto actual=control.flux_register.GetSummedFlux(key.block,key.face,key.cell);
            near(actual.rho,expected_mass.at(key),"real register repeated/lost route-area or RK mass factor");
            near(actual.eng,energy,"real register repeated/lost route-area or RK work factor");
        }
        // Existing ExecuteRefluxPlan alone supplies signed dt*A_coarse/V once.
        const auto reflux=amr::build_amr_reflux_topology_plan(*control.pool,topology);
        std::map<std::pair<int,int>,double> before_energy,before_mass;
        for(const auto& operation:reflux.operations)if(operation.field==amr::AmrField::Energy) {
            const int id=topology.pool_lowering.at(operation.destination);const auto& b=control.pool->GetBlock(id);
            const int c=b.grid.GetIndex(b.grid.Is()+operation.destination_box.first[0],b.grid.Js()+operation.destination_box.first[1],0);
            before_energy[{id,c}]=b.fluid_state.eng[c];before_mass[{id,c}]=b.fluid_state.rho[c];
        }
        constexpr double dt=0x1p-16;
        control.flux_register.ExecuteRefluxPlan(reflux,control.pool,control.tree->GetActiveBlocks(),owner.runtime->handles(),
            &amr::Block::fluid_state,dt,&topology);
        for(const auto& operation:reflux.operations)if(operation.field==amr::AmrField::Energy) {
            const int id=topology.pool_lowering.at(operation.destination);const auto& b=control.pool->GetBlock(id);
            const int c=b.grid.GetIndex(b.grid.Is()+operation.destination_box.first[0],b.grid.Js()+operation.destination_box.first[1],0);
            const int dir=amr::axis_value(operation.axis),face=2*dir+int(operation.side==amr::AmrSide::Upper);
            const int fc=dir==0?operation.destination_box.first[1]:operation.destination_box.first[0];const Key key{id,face,fc};
            near(b.fluid_state.eng[c],before_energy.at({id,c})+dt*operation.sign*operation.weight*expected_energy.at(key),
                "real reflux repeated/lost dt or coarse A/V energy factor");
            near(b.fluid_state.rho[c],before_mass.at({id,c})+dt*operation.sign*operation.weight*expected_mass.at(key),
                "real reflux repeated/lost dt or coarse A/V mass factor");
        }
    }
    require(covariance&&fine_counterterm_distinct,"work-row stimulus cannot detect product-of-means or wrong fine counterterm");
    if(periodic&&axis==1)require(periodic_seam,"actual paired-z seam was not exercised");
    // Restore only explicitly injected metadata, never invent replacement UIDs.
    control.flux_register.Clear();const auto no_registration=control.flux_register.snapshot_host();
    auto bad=binding;bad.handles.front().uid.value+=1000;
    refused([&]{return Physical::Gravity::gravity_reflux_rows(bad,op,topology);},control.flux_register,no_registration);
    bad=binding;++bad.handles.front().epoch.value;
    refused([&]{return Physical::Gravity::gravity_reflux_rows(bad,op,topology);},control.flux_register,no_registration);
    bad=binding;bad.storage.pop_back();
    refused([&]{return Physical::Gravity::gravity_reflux_rows(bad,op,topology);},control.flux_register,no_registration);
    bad=binding;bad.storage.back()=bad.storage.front();
    refused([&]{return Physical::Gravity::gravity_reflux_rows(bad,op,topology);},control.flux_register,no_registration);
    auto& grid=control.pool->GetBlock(control.tree->GetActiveBlocks().front()).grid;
    const auto provenance=grid.dyadic_identity;
    grid.dyadic_identity.root_upper[0]=std::nextafter(provenance.root_upper[0],std::numeric_limits<double>::infinity());
    refused([&]{return Physical::Gravity::gravity_reflux_rows(binding,op,topology);},control.flux_register,no_registration);
    grid.dyadic_identity=provenance;
    require(owner.runtime->stage_context().ledger.host_snapshot_matches(ledger_before),"topology row checks published real fluid ledger state");
}
void run() {
    for(int axis:{0,1})for(double radial_lower:{0.,.4})run_case(axis,radial_lower,true,false);
    run_case(1,.4,true,true);run_case(0,.4,false,false);
    std::cout<<"NATIVE_GRAVITY_REFLUX_ROW_PASS actual_Runtime=true source_geometry=true covariance=true original_RK_weights=true real_register_reflux=true field_or_production_qualification=false\n";
}
} // namespace reflux_row_checks

void test_preparation() {
    for (const auto method : {HydroMethod::Euler, HydroMethod::RK2, HydroMethod::RK3}) {
        Fixture f;
        Probe p;
        run_hydro(f, p, method);
        const int count = method == HydroMethod::Euler ? 1 : method == HydroMethod::RK2 ? 2 : 3;
        require(p.executions == count, "one preparation per actual RK stage");
        require(p.slots.front() == StateSlot::Current && p.times.front() == 2.0,
                "first stage uses accepted input");
        if (count >= 2) {
            require(p.slots[1] == StateSlot::Scratch && p.times[1] == 2.25,
                    "second stage uses its evolved scratch state and time");
            require(p.versions[1][0] != p.versions[0][0], "stage input version advances");
        }
        if (count == 3)
            require(p.slots[2] == StateSlot::Next && p.times[2] == 2.125,
                    "SSPRK3 third stage samples the half-time input");
    }
    // A domain request exposes every block's authoritative version, even when
    // the accepted input versions differ; no first-block shortcut is valid.
    Fixture mixed;
    const auto later = mixed.clock.next_publication();
    mixed.ledger.publish_interior({mixed.handles[1], StateSlot::Current}, ExecutionSide::Host,
                                   later.version, later.completion);
    mixed.ledger.publish_ghost({mixed.handles[1], StateSlot::Current}, ExecutionSide::Host,
                                later.version, later.completion);
    Probe p;
    run_hydro(mixed, p, HydroMethod::Euler);
    require(p.versions[0][0] != p.versions[0][1], "all block input dependencies remain observable");
}
void test_failures() {
    for (int mode = 0; mode < 3; ++mode) {
        Fixture f(mode != 0);
        Probe p;
        p.pending = mode == 1;
        p.fail = mode == 2;
        const auto version = f.clock.last_version(), token = f.clock.last_token();
        rejects([&] { run_hydro(f, p, HydroMethod::RK3); }, "invalid preparation accepted");
        require(p.executions == 0, "failed preparation must not execute patches");
        require(f.clock.last_version() == version && f.clock.last_token() == token,
                "failed preparation must not issue a publication witness");
        require(f.ledger.inspect({f.handles[0], StateSlot::Next}).interior.residency
                    == StateResidency::Invalid, "failed prepare cannot publish output");
        if (mode == 0) require(p.slots.empty(), "input validation must precede the service");
    }
    // No field service is the production none/external route in P1.
    Fixture f;
    int executed = 0;
    execute_euler_lane(f.context, f.handles,
        [&](const StageDescriptor&, CompletionToken t) { ++executed; return t; },
        [](StateSlot, StateVersion, CompletionToken t) { return t; },
        [](SlotRotation) {},
        [](const HydroPlan&, StateSlot, CompletionToken t) { return t; });
    require(executed == 1, "unbound preparation preserves existing execution");
}
void test_field_identity() {
    using namespace Physical::Gravity;
    GravitySolveIdentity identity;
    identity.topology = {1};
    identity.inputs = {{{{1}, {1}}, StateSlot::Current, {1}, 1},
                       {{{2}, {1}}, StateSlot::Current, {2}, 2}};
    identity.gravitational_constant = 6.67430e-8;
    identity.operator_revision = identity.boundary_revision = identity.accuracy_revision = 1;
    GravityFieldValidity field;
    require(!field.matches(identity, 3), "unpublished gravity cannot be consumed");
    field.publish({identity, 3, {9, CompletionState::Complete}});
    require(field.matches(identity, 3), "complete matching publication is usable");
    for (int change = 0; change < 9; ++change) {
        auto changed = identity;
        switch (change) {
        case 0: changed.inputs.back().version = {3}; break;
        case 1: changed.inputs.back().storage_generation = 4; break;
        case 2: changed.inputs.back().slot = StateSlot::Scratch; break;
        case 3: changed.topology = {2}; break;
        case 4: ++changed.operator_revision; break;
        case 5: ++changed.boundary_revision; break;
        case 6: ++changed.accuracy_revision; break;
        case 7: changed.input_time = 0.25; break;
        case 8: changed.gravitational_constant *= 2.0; break;
        }
        require(!field.matches(changed, 3), "stale field dependency accepted");
    }
    require(!field.matches(identity, 4), "retired field storage accepted");
    rejects([&] { field.publish({identity, 3, {10, CompletionState::Pending}}); },
            "pending field published");
    require(field.matches(identity, 3), "failed publication changed the accepted metadata");
    auto invalid = identity;
    invalid.inputs.back().block.epoch = {2};
    rejects([&] { field.publish({invalid, 3, {10, CompletionState::Complete}}); },
            "mixed topology input published");
    field.invalidate();
    require(!field.matches(identity, 3), "explicit regrid/restart invalidation failed");
}
namespace native_self_hydro_owner_checks {
using namespace arch;
using rz_runtime_witness::bits;
constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
constexpr double interval=1.e-3;
using ActualHydro=Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>>;
using Stage=driver::GravityStage;
/** Delegate every actual gravity operation; count only genuine density gathers. */
class ActualExecution final:public Physical::Gravity::GravityExecution {
    std::shared_ptr<Physical::Gravity::GravityExecution> host_=Physical::Gravity::make_host_gravity_execution();
public:
    int gathers=0;
    std::shared_ptr<multigrid::CompositeExecution> numeric() const override{return host_->numeric();}
    void run(const Physical::Gravity::GravityWork& work) override {
        host_->run(work);if(std::holds_alternative<Physical::Gravity::GatherDensity>(work))++gathers;
    }
};
struct Owner;
/** Observe a genuine prepared field, then delegate exactly one selected Hydro.
 * The wrong-dt claim is an engineering negative before any patch scratch write;
 * no affine field, substituted Riemann flux or synthetic source receipt exists.
 */
class ObservedHydro final:public Numerics::IHydroSolver {
    Owner& owner_;const ActualHydro& actual_;
public:
    mutable std::mutex mutex;
    mutable std::map<std::uint64_t,int> visits;
    mutable bool nonzero_field=false,flow=false,dt_fault_seen=false,cache_fault_seen=false;
    mutable std::shared_ptr<const amr::AmrFluxTopologyPlan> retained_topology;
    // Explicit maintenance diagnostics borrow only this actual Hydro call.
    // Defaults are empty: ordinary CI/private matrices perform no extra copies.
    using EnergyField=Physical::Gravity::NativeRzFieldInspection;
    using EnergyFieldCapture=void(*)(void*,Owner&,EnergyField&&,const scheduler::StageDescriptor&);
    using EnergyDeltaCapture=void(*)(void*,Owner&,int,const FluidState&,const Grid&,const std::vector<FluidVector>&);
    void* energy_payload=nullptr;
    EnergyFieldCapture energy_field_capture=nullptr;
    EnergyDeltaCapture energy_delta_capture=nullptr;
    explicit ObservedHydro(Owner& owner,const ActualHydro& actual):owner_(owner),actual_(actual){}
    GridMetrics::GeometrySemantics geometry_semantics() const noexcept override{return actual_.geometry_semantics();}
    Numerics::HostHydroStorageContract host_storage_contract() const noexcept override{return actual_.host_storage_contract();}
    void evaluate_patch(amr::AMRControl*,int,const FluidState&,const Grid&,double,
        std::vector<FluidVector>&,std::vector<double>&,const Physical::Gravity::IGravityPolicy*,
        const NumericsConfig&,double=1.,void* =nullptr,const boundary::HostHydroBoundaryAuthority* =nullptr) const override;
    void update_patch(const FluidState& old,const FluidState& current,FluidState& next,
        const std::vector<FluidVector>& du,const std::vector<double>& dx,const Grid& grid,
        double old_weight,double flux_weight,const NumericsConfig& config,void* stream=nullptr) const override {
        actual_.update_patch(old,current,next,du,dx,grid,old_weight,flux_weight,config,stream);
    }
};
/** Authentic annular CGS root with positive real radial halos, strict actual EOS,
 * real mixed 2:1 topology when requested, and the original shared G in SelfGravity.
 * Warm physical input rho=1, ur=.01, uz=.02+.002*r/10000, e=100, Omega=0.
 * Native energy/momentum use independent V antiderivatives, not point guesses.
 */
struct Owner {
    SimConfig config{};SpeciesManager species;amr::AMRControl control{16,2};RunState start{};
    std::unique_ptr<IdealGas> eos;std::unique_ptr<SimulationController> counters;
    std::unique_ptr<BCHandler> bc;std::unique_ptr<driver::DriverRuntime> runtime;
    std::unique_ptr<Physical::Gravity::SelfGravity> gravity;
    std::shared_ptr<ActualExecution> execution;
    std::unique_ptr<Stage> stage;std::unique_ptr<ActualHydro> hydro;
    std::unique_ptr<ObservedHydro> observer;
    std::optional<scheduler::StageExecutionContext> context;
    driver::DriverStageWorkspace workspace;dispatch::ResolvedExecutionPlan resolved{};
    scheduler::HydroMethod method;bool mixed,wrong_dt=false,drop_cache=false;
    Owner(scheduler::HydroMethod selected,bool refined,const std::string& label)
        :method(selected),mixed(refined) {
        config.grid.dim=2;config.grid.geometry="cylindrical";
        config.grid.nblockx1=mixed?2:1;config.grid.nblockx2=1;config.grid.nblockx3=0;
        config.grid.x1_min=10000.;config.grid.x1_max=30000.;
        config.grid.x2_min=-10000.;config.grid.x2_max=10000.;config.grid.amr_max_blocks=16;
        config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="outflow";
        config.grid.x2l_boundary_type=config.grid.x2r_boundary_type="outflow";
        config.amr.lrefinemin=0;config.amr.lrefinemax=mixed?1:0;
        config.numerics.solver_name="HLLC";config.numerics.reconstruction="pcm";
        config.numerics.time_integrator=method==scheduler::HydroMethod::Euler?"euler":
            method==scheduler::HydroMethod::RK2?"rk2":"rk3";
        config.physics.gravity.type="self";config.physics.gravity.boundary="isolated";
        // Existing real Runtime/isolated numerical-candidate budget, unchanged.
        config.physics.gravity.relative_tolerance=1.e-10;config.physics.gravity.absolute_tolerance=0.;
        config.physics.gravity.max_cycles=200;
        config.physics.burn.use_burn=false;config.physics.diffusion.use_diffusion=false;
        config.io.tmax=1.;config.io.plt_dt=config.io.chk_dt=0.;
        config.io.out_dir="native-self-hydro-owner/"+label;
        species.add_species("self-hydro-gas",1.,1.,1.4,2.);eos=std::make_unique<IdealGas>(1.4,species);
        if(mixed)control.tree->LoadLeafGrid(config,1,{1,1,1,1,0},
            {0,1,0,1,1},{0,0,1,1,0},{0,0,0,0,0},rz);
        else control.tree->InitRootGrid(config,1,rz);
        control.flux_register.EnsureSpecies(1);
        for(int id:control.tree->GetActiveBlocks()) {
            auto& block=control.pool->GetBlock(id);block.RequireNativeGeometryIdentity();const auto& grid=block.grid;
            require(grid.GetFacePosL(0)>0.&&grid.Ie()-grid.Is()>=4&&grid.Je()-grid.Js()>=4,
                "PrivateSelf fixture has invalid actual annular support");
            for(auto* fluid:rz_runtime_witness::slots(block)) {
                fluid->stage_repairs.reset(1,state::RepairSemantics::RzVolumeAngular);
                for(int c=0;c<grid.GetTotalSize();++c) {
                    fluid->set(c,{1.,.01,.02,0.,100.00025});fluid->X(0,c)=1.;fluid->enuc_rate[c]=3.+c;
                }
                for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
                    const long double l=grid.GetFacePosL(i),h=grid.GetFacePosR(i);
                    const long double V=(h*h-l*l)/2.,rmean=(h*h*h-l*l*l)/(3.*V),
                        r2mean=(h*h*h*h-l*l*l*l)/(4.*V),a=.02L,b=.002L/10000.L;
                    const double uz=double(a+b*rmean);
                    const double energy=double(100.L+.5L*.01L*.01L+.5L*(a*a+2.*a*b*rmean+b*b*r2mean));
                    fluid->set(grid.GetIndex(i,j,0),{1.,.01,uz,0.,energy});
                }
            }
        }
        start.time=.375;start.step=1;start.repairs.reset(1,state::RepairSemantics::RzVolumeAngular);
        counters=std::make_unique<SimulationController>(config,start);
        bc=std::make_unique<BCHandler>(config,rz);bc->bind(*eos,species);
        bc->configure_stage(start.time,boundary::BoundaryPurpose::Hydro);
        runtime=std::make_unique<driver::DriverRuntime>(control,*bc,config,species,*counters);
        runtime->bind_native_rz_eos(*eos);runtime->initialize_topology();
        gravity=std::make_unique<Physical::Gravity::SelfGravity>(config.physics.gravity);
        execution=std::make_shared<ActualExecution>();gravity->set_execution(execution);
        stage=std::make_unique<Stage>(*runtime,gravity.get(),Stage::Qualification::NativeRzSelfHydroCandidate);
        require(stage->supports_host_macro_step_journal(),"PrivateSelf did not acquire genuine Host journal capability");
        // The actual schema must already be readable while its writer lives,
        // including a later zero-row macro rejection. No solve is needed here.
        {std::ifstream journal(config.io.out_dir+"/native_rz_candidates.tsv");std::string header;
            require(bool(std::getline(journal,header))&&header==
                "time\tstage\tepoch\tlease\tcells\tsource_generation\tresidual_upper\ttolerance_safe\tphysical_qualified",
                "PrivateSelf live writer has not published the actual journal schema");}
        hydro=std::make_unique<ActualHydro>(*eos,rz);observer=std::make_unique<ObservedHydro>(*this,*hydro);
        resolved.time_integrator=method==scheduler::HydroMethod::Euler?dispatch::TimeIntegratorId::Euler:
            method==scheduler::HydroMethod::RK2?dispatch::TimeIntegratorId::Rk2:dispatch::TimeIntegratorId::Rk3;
        context.emplace(runtime->stage_context());context->step_start_time=start.time;context->step_dt=interval;
        context->configure_boundary_context=[this](double time,boundary::BoundaryPurpose purpose) {
            bc->configure_stage(time,purpose);runtime->bind_native_boundary_acceptance(*context,runtime->handles());
        };
        context->physical_boundary_preparation=[this](state::StateSlot slot,double time,boundary::BoundaryPurpose purpose) {
            context->configure_boundary_context(time,purpose);runtime->ensure_fluid_ghosts(slot);
        };
        context->hydro_preparation=stage.get();
        context->configure_boundary_context(start.time,boundary::BoundaryPurpose::Hydro);
        runtime->bind_boundary_accounting(*context);
    }
    /** One real macro transaction surrounds selected real Hydro/final reflux/EOS.
     * Burn/diffusion are disabled for this independent source-consumer owner;
     * disabled diffusion still goes through its existing production entry.
     */
    void advance() {
        scheduler::ScopedStageBinding binding(*context,runtime->handles());
        const driver::IntegratorSolve integrator=method==scheduler::HydroMethod::Euler?&SolverEuler::solve<BCHandler>:
            method==scheduler::HydroMethod::RK2?&SolverRK2::solve<BCHandler>:&SolverRK3::solve<BCHandler>;
        driver::execute_driver_macro_step(*runtime,*context,observer.get(),false,
            [](driver::BurnHalf,double,state::CompletionToken){throw std::logic_error("disabled Burn was executed");return state::CompletionToken{};},
            [&](double half){driver::advance_diffusion(*runtime,workspace,*context,*eos,&resolved,counters->step_count,half,1.);},
            [&](double full){driver::advance_hydro(*runtime,workspace,*context,&resolved,full,integrator,gravity.get(),observer.get());},
            [](driver::CpuStage,auto&& call){call();});
    }
};

void ObservedHydro::evaluate_patch(amr::AMRControl* control,int id,const FluidState& input,const Grid& grid,
    double dt,std::vector<FluidVector>& du,std::vector<double>& dx,
    const Physical::Gravity::IGravityPolicy* policy,const NumericsConfig& config,double weight,void* stream,
    const boundary::HostHydroBoundaryAuthority* walls) const {
    require(control==&owner_.control&&policy==owner_.gravity.get()&&walls
        &&owner_.runtime->active_host_hydro_transaction()&&bits(dt,interval),
        "PrivateSelf consumer lost actual Runtime/policy/transaction/wall identity");
    const auto* frame=policy->prepared_native_self();
    require(frame&&!policy->prepared_native_external(),"PrivateSelf consumer has no uniquely prepared real field frame");
    const auto& assessment=owner_.gravity->native_rz_assessment();
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto generation=assessment.source_generation;
        const bool first=!visits.contains(generation);
        if(first) {
            const auto stage_index=visits.size();const auto plan=scheduler::make_hydro_plan(owner_.method);
            require(stage_index<plan.stages.size(),"PrivateSelf produced extra solved stages");
            const auto& descriptor=plan.stages[stage_index];
            auto field=owner_.gravity->native_rz_field_inspection();
            require(field.source_generation==generation&&field.field_generation>0
                &&field.source.topology==owner_.runtime->handles().front().epoch
                &&field.source.inputs.size()==owner_.runtime->handles().size()
                &&bits(field.source.input_time,owner_.context->step_start_time+descriptor.input_time_fraction*dt),
                "PrivateSelf solved field lost actual same-stage source/time/epoch identity");
            for(const auto& source:field.source.inputs)
                require(source.slot==descriptor.input_slot&&source.version.value>0&&source.storage_generation>0,
                    "PrivateSelf source did not use its actual selected publication");
            require(!field.potential.empty()&&field.acceleration[0].size()==field.potential.size()
                &&field.acceleration[1].size()==field.potential.size(),"PrivateSelf actual workspace extent is missing");
            for(std::size_t n=0;n<field.potential.size();++n) {
                require(std::isfinite(field.potential[n])&&std::isfinite(field.acceleration[0][n])
                    &&std::isfinite(field.acceleration[1][n]),"PrivateSelf field contains nonfinite actual values");
                nonzero_field|=field.potential[n]!=0.&&(field.acceleration[0][n]!=0.||field.acceleration[1][n]!=0.);
            }
            rejects([&]{owner_.gravity->potential();},"ready candidate granted public Phi");
            rejects([&]{owner_.gravity->patch_view(0);},"ready candidate granted public patch source");
            rejects([&]{owner_.gravity->timestep(owner_.config.numerics.cfl);},"ready candidate granted public CFL");
            rejects([&]{owner_.stage->plot_fields();},"ready candidate granted public plot fields");
            if(energy_field_capture)energy_field_capture(energy_payload,owner_,std::move(field),descriptor);
        }
        ++visits[generation];
        const int cell=grid.GetIndex(grid.Is(),grid.Js(),0);flow|=input.mom_u[cell]!=0.||input.mom_v[cell]!=0.;
    }
    if(owner_.drop_cache) {
        // Republish the exact actual span after real field preparation. Only the
        // ephemeral cache owner is dropped; topology/configuration/U stay fixed.
        const auto before_du=du;const auto before_dx=dx;
        const auto lease=control->FluxTopologyPlanLease();
        require(bool(lease),"PrivateSelf cache fault did not have an actual original lease");
        const auto fingerprint=lease->fingerprint;const auto epoch=lease->epoch;
        const auto route_count=lease->routes.size();const auto handles=control->ActiveHandles();
        control->PublishActiveHandlesNoexcept(handles);
        require(!control->FluxTopologyPlanLease()&&control->ActiveHandles().data()==handles.data()
            &&control->ActiveHandles().size()==handles.size(),
            "PrivateSelf exact handle republication did not drop only its cache");
        bool rejected=false;
        try{(void)frame->boundary_domain();}
        catch(const std::logic_error& error) {
            rejected=std::string(error.what())=="Native self topology cache owner was dropped or replaced";
            if(!rejected)throw;
        }
        require(rejected&&!control->FluxTopologyPlanLease()&&lease->fingerprint==fingerprint
            &&lease->epoch==epoch&&lease->routes.size()==route_count&&du.size()==before_du.size()
            &&rz_runtime_witness::bits(dx,before_dx),
            "PrivateSelf dropped lease revived a cache, invalidated its owner, or wrote scratch");
        for(std::size_t n=0;n<du.size();++n)for(auto member:{&FluidVector::rho,&FluidVector::mom_u,
            &FluidVector::mom_v,&FluidVector::mom_w,&FluidVector::eng})
            require(bits(du[n].*member,before_du[n].*member),"PrivateSelf rejected lease wrote actual dU");
        {std::lock_guard<std::mutex> lock(mutex);retained_topology=lease;cache_fault_seen=true;}
        throw std::logic_error("PRIVATE_SELF_REAL_CACHE_LEASE_REFUSED");
    }
    if(owner_.wrong_dt) {
        const auto before_du=du;const auto before_dx=dx;bool rejected=false;
        try{auto impossible=frame->claim_patch(control,id,input,grid,
            std::nextafter(dt,std::numeric_limits<double>::infinity()),*policy);(void)impossible;}
        catch(const std::logic_error&) {rejected=true;}
        require(rejected&&du.size()==before_du.size()&&rz_runtime_witness::bits(dx,before_dx),
            "PrivateSelf wrong-dt claim changed scratch or did not reject");
        for(std::size_t n=0;n<du.size();++n)for(auto member:{&FluidVector::rho,&FluidVector::mom_u,
            &FluidVector::mom_v,&FluidVector::mom_w,&FluidVector::eng})
            require(bits(du[n].*member,before_du[n].*member),"PrivateSelf rejected claim wrote actual dU");
        {std::lock_guard<std::mutex> lock(mutex);dt_fault_seen=true;}
        throw std::logic_error("PRIVATE_SELF_REAL_DT_CLAIM_REFUSED");
    }
    actual_.evaluate_patch(control,id,input,grid,dt,du,dx,policy,config,weight,stream,walls);
    if(energy_delta_capture)energy_delta_capture(energy_payload,owner_,id,input,grid,du);
}

/** Count actual accepted journal rows after commit/flush, never numerical receipts guessed from dU. */
void journal_rows(const Owner& owner,std::size_t expected) {
    std::ifstream file(owner.config.io.out_dir+"/native_rz_candidates.tsv");std::string line;
    require(bool(std::getline(file,line)),"PrivateSelf actual journal header is missing");
    std::size_t count=0;
    while(std::getline(file,line))if(!line.empty()) {
        std::istringstream row(line);double time=0.,residual=0.,tolerance=0.;int stage=0,physical=-1;
        std::uint64_t epoch=0,lease=0,source=0;std::size_t cells=0;
        require(bool(row>>time>>stage>>epoch>>lease>>cells>>source>>residual>>tolerance>>physical)
            &&stage==static_cast<int>(++count)&&std::isfinite(time)&&epoch>0&&lease>0&&cells>0&&source>0
            &&std::isfinite(residual)&&residual>=0.&&std::isfinite(tolerance)&&tolerance>0.
            &&residual<=tolerance&&physical==0,
            "PrivateSelf committed row lost stage/source/candidate-only semantics");
    }
    require(count==expected,"PrivateSelf journal omitted or published extra stages");
}
/** Exercise the final genuine cache refusal independently of accepted solves.
 * The full maintenance matrix below invokes this same unchanged check. */
void cache_refusal() {
    {
        // A separate fresh actual macro/frame proves cache lease invalidation;
        // never reuse the earlier wrong-dt frame after it has been poisoned.
        Owner owner(scheduler::HydroMethod::RK3,false,"rk3-cache-lease-refusal");owner.drop_cache=true;
        auto& block=owner.control.pool->GetBlock(owner.control.tree->GetActiveBlocks().front());
        const rz_runtime_witness::FieldsWitness fields(block);
        const auto saved=driver::HostHydroTransaction::snapshot_owner(*owner.runtime,*owner.context);
        bool refused=false;try{owner.advance();}catch(const std::logic_error& error) {
            refused=std::string(error.what())=="PRIVATE_SELF_REAL_CACHE_LEASE_REFUSED";if(!refused)throw;
        }
        fields.matches(block);const auto lease=owner.observer->retained_topology;
        require(refused&&owner.observer->cache_fault_seen&&owner.execution->gathers==1&&lease
            &&lease->fingerprint>0&&lease->epoch==owner.runtime->handles().front().epoch
            &&!owner.control.FluxTopologyPlanLease()
            &&driver::HostHydroTransaction::owner_matches(*owner.runtime,*owner.context,saved)
            &&!owner.runtime->active_host_hydro_transaction()&&!owner.gravity->prepared_native_self()
            &&bits(owner.counters->t_current,.375)&&owner.counters->step_count==1,
            "PrivateSelf dropped actual cache lease did not preserve owner/field rollback or resurrected cache");
        owner.stage->flush_committed_diagnostics();journal_rows(owner,0);
        rejects([&]{owner.gravity->native_rz_potential();},"PrivateSelf rejected cache macro retained usable candidate field");
    }
}
void run() {
    for(auto method:{scheduler::HydroMethod::Euler,scheduler::HydroMethod::RK2,scheduler::HydroMethod::RK3}) {
        const auto name=method==scheduler::HydroMethod::Euler?"euler":method==scheduler::HydroMethod::RK2?"rk2":"rk3";
        Owner owner(method,false,std::string(name)+"-single");owner.advance();
        const auto stages=scheduler::make_hydro_plan(method).stages.size();
        require(owner.execution->gathers==static_cast<int>(stages)&&owner.observer->visits.size()==stages
            &&owner.observer->nonzero_field&&owner.observer->flow&&!owner.gravity->prepared_native_self(),
            "PrivateSelf single owner skipped a real solved-field/source stage or retained a live frame");
        for(auto [generation,visits]:owner.observer->visits){(void)generation;require(visits==1,"PrivateSelf repeated/omitted a real patch");}
        for(auto handle:owner.runtime->handles()) {
            const auto coherence=owner.context->ledger.inspect({handle,state::StateSlot::Current});
            owner.context->ledger.require_readable({handle,state::StateSlot::Current},
                {state::ExecutionSide::Host,coherence.interior.version,true,true});
        }
        owner.stage->flush_committed_diagnostics();journal_rows(owner,stages);
    }
    {
        Owner owner(scheduler::HydroMethod::Euler,true,"euler-mixed");
        const auto& topology=owner.control.RequireFluxTopologyPlan(1,rz,-1,true);
        require(!topology.routes.empty(),"PrivateSelf mixed fixture has no actual CF routes");owner.advance();
        require(owner.execution->gathers==1&&owner.observer->visits.size()==1
            &&owner.observer->visits.begin()->second==static_cast<int>(owner.runtime->handles().size())
            &&owner.observer->nonzero_field&&owner.observer->flow&&!owner.gravity->prepared_native_self(),
            "PrivateSelf mixed actual field/patch/registration consumption did not complete");
        owner.stage->flush_committed_diagnostics();journal_rows(owner,1);
    }
    {
        Owner owner(scheduler::HydroMethod::RK3,false,"rk3-dt-refusal");owner.wrong_dt=true;
        auto& block=owner.control.pool->GetBlock(owner.control.tree->GetActiveBlocks().front());
        const rz_runtime_witness::FieldsWitness fields(block);
        const auto saved=driver::HostHydroTransaction::snapshot_owner(*owner.runtime,*owner.context);
        bool refused=false;try{owner.advance();}catch(const std::logic_error& error) {
            refused=std::string(error.what())=="PRIVATE_SELF_REAL_DT_CLAIM_REFUSED";if(!refused)throw;
        }
        fields.matches(block);require(refused&&owner.observer->dt_fault_seen&&owner.execution->gathers==1
            &&driver::HostHydroTransaction::owner_matches(*owner.runtime,*owner.context,saved)
            &&!owner.runtime->active_host_hydro_transaction()&&!owner.gravity->prepared_native_self()
            &&bits(owner.counters->t_current,.375)&&owner.counters->step_count==1,
            "PrivateSelf actual dt rejection did not restore fields/leases/ledger/flux/clock/BC/source owners");
        owner.stage->flush_committed_diagnostics();journal_rows(owner,0);
        rejects([&]{owner.gravity->native_rz_potential();},"PrivateSelf rejected macro retained usable candidate field");
    }
    cache_refusal();
    std::cout<<"PRIVATE_NATIVE_SELF_HYDRO_OWNER candidate_only=1 actual_Euler_RK2_RK3=1 mixed_Euler=1 real_dt_refusal=1 real_cache_lease_refusal=1 public_gates_held=1 physical_grant=0\n";
}
} // namespace native_self_hydro_owner_checks

} // namespace
int main(int argc,char** argv) {
    try {
        if(argc==2&&std::string(argv[1])=="private-native-self") {
            native_self_hydro_owner_checks::run();return 0;
        }
        if(argc==2&&std::string(argv[1])=="private-native-self-cache-refusal") {
            native_self_hydro_owner_checks::cache_refusal();
            std::cout<<"PRIVATE_NATIVE_SELF_CACHE_REFUSAL actual_field=1 full_rollback=1 physical_grant=0\n";
            return 0;
        }
        if(argc==2&&std::string(argv[1])=="private-native-self-energy") {
            arch::test::run_native_self_energy<native_self_hydro_owner_checks::Owner>(native_self_hydro_owner_checks::interval);
            std::cout<<"PRIVATE_NATIVE_SELF_ENERGY accounting_checked=1 four_actual_fields=1 fault_rollback=1 total_energy_science=UNVERIFIED physical_grant=0\n";
            return 0;
        }
        if(argc==2&&std::string(argv[1])=="private-native-self-green-pair") {
            arch::test::run_native_self_green_pair<native_self_hydro_owner_checks::Owner>(native_self_hydro_owner_checks::interval);
            std::cout<<"PRIVATE_NATIVE_SELF_GREEN_PAIR algebra_checked=1 two_actual_fields=1 continuous_green_science=UNVERIFIED physical_grant=0\n";
            return 0;
        }
        if(argc!=1)throw std::invalid_argument("expected no arguments, private-native-self, private-native-self-cache-refusal, private-native-self-energy or private-native-self-green-pair");
        reflux_row_checks::run(); test_native_external_source_mean(); test_preparation(); test_failures(); test_field_identity(); test_host_hydro_transaction(); run_native_rz_runtime_boundary_contract();
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    std::cout << "Gravity stage preparation and field identity contracts passed\n";
}
