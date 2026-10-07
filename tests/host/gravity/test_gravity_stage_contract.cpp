/** @file test_gravity_stage_contract.cpp
 * @brief Whole-domain preparation ordering and gravity publication validity.
 */
#include "driver/schedule/StageScheduler.h"
#include "physics/gravity/GravitySolveTypes.h"
#include "amr/AMRControl.h"
#include "physics/eos/IdealGas.h"
#include "physics/gravity/GravitySource.h"
#include "numerics/state/RzCellAverage.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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
} // namespace
int main() {
    try { test_native_external_source_mean(); test_preparation(); test_failures(); test_field_identity(); test_host_hydro_transaction(); run_native_rz_runtime_boundary_contract(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    std::cout << "Gravity stage preparation and field identity contracts passed\n";
}
