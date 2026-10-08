/** Pure formal thermal evolution through the existing ordinary Host RKL2 lane.
 * Workflow: initialize exact cosine cell means; fill real periodic ghosts;
 * bind the actual storage ledger; call the production integrator; compare
 * every macrostep against independent long-double s=3 stability polynomials
 * and the semidiscrete heat eigenmode. This is no coupled Hydro/AMR/device claim.
 */
#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "amr/storage/Block.h"
#include "driver/schedule/StageScheduler.h"
#include "numerics/diffusion/RKL2TimeIntegrator.h"
#include "physics/eos/IdealGas.h"

namespace ThermalFiveDecayCases {
inline void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
/** Copy every halo from its own real periodic active cell. */
struct PeriodicBoundary {
    void apply(FluidState& state,const Grid& grid) const {
        const int count=grid.Ie()-grid.Is();
        for(int i=0;i<grid.GetTotalSize();++i) {
            if(i>=grid.Is()&&i<grid.Ie())continue;
            const int source=grid.Is()+((i-grid.Is())%count+count)%count;
            state.set(i,state.get(source));state.X(0,i)=state.X(0,source);
        }
    }
};
struct Result {double endpoint_error=0.,maximum_error=0.,maximum_heat_drift=0.;};
/** Run one fresh trajectory; producer owns all copies, rotations and versions. */
inline Result trajectory(int macrosteps) {
    using namespace arch::scheduler;
    using namespace arch::state;
    Grid grid(amr::MAX_NG,0.,1.);grid.dim=1;grid.InitializeTopology();
    const int count=grid.Ie()-grid.Is();require(count==16,"thermal fixture requires sixteen physical cells");
    SpeciesManager species;species.add_species("thermal",1.,1.,1.4,1.);
    IdealGas eos(species);
    SimConfig config;
    auto& diffusion=config.physics.diffusion;
    diffusion.use_diffusion=true;diffusion.use_thermal_diffusion=true;
    diffusion.use_species_diffusion=false;diffusion.use_viscous_diffusion=false;
    diffusion.alpha_therm=.01;diffusion.nu_visc=0.;diffusion.D_spec=0.;
    diffusion.diff_cfl=.45;diffusion.max_stages=31;
    config.numerics.sml_rho=1e-30;config.numerics.min_eint=1e-12;config.numerics.max_eint=1e12;
    const long double pi=std::acos(-1.L),half_phase=pi/count;
    const long double amplitude=.4L*std::sin(half_phase)/half_phase;
    const long double h=1.L/count;
    const long double lambda=-4.L*static_cast<long double>(diffusion.alpha_therm)
        /(h*h)*std::sin(half_phase)*std::sin(half_phase);
    const double end=static_cast<double>(-5.L/lambda),dt=end/macrosteps;
    const double dt_fe=static_cast<double>(h*h/(2.L*diffusion.alpha_therm));
    require(DiffFunction::compute_stages(DiffFunction::RKLOrder::Second,dt,dt_fe,
        diffusion.diff_cfl,diffusion.max_stages)==3,"thermal fixture changed the frozen RKL stage count");
    require(dt<=DiffFunction::stable_step(DiffFunction::RKLOrder::Second,dt_fe,
        diffusion.diff_cfl,3),"thermal fixture exceeds its actual stability interval");
    amr::Block block;block.Reset();block.grid=grid;
    block.fluid_state.Preallocate(grid.GetTotalSize());block.fluid_state.InitSpecies(1);
    for(int i=0;i<grid.GetTotalSize();++i) {
        const long double cosine=std::cos(2.L*pi*grid.GetCellCenterX(i));
        block.fluid_state.set(i,{1.,0.,0.,0.,static_cast<double>(2.L+amplitude*cosine)});
        block.fluid_state.X(0,i)=1.;
    }
    const PeriodicBoundary boundary;boundary.apply(block.fluid_state,grid);
    block.state_next=block.fluid_state;block.state_scratch=block.fluid_state;
    long double initial_heat=0.;
    for(int i=grid.Is();i<grid.Ie();++i)initial_heat+=block.fluid_state.eng[i];
    const amr::BlockHandle handle{{1},{1}};
    StateResidencyLedger ledger{{1}};MonotonicSchedulerClock clock{2,1};
    ledger.register_block(handle,{1},{1,CompletionState::Complete});
    ledger.publish_ghost({handle,StateSlot::Current},ExecutionSide::Host,{1},
        {2,CompletionState::Complete});
    StageExecutionContext context{ExecutionSide::Host,ledger,clock};
    const std::vector handles{handle};ScopedStageBinding binding(context,handles);
    int accepted_stages=0;
    // Inspect the actual descriptor output before publication; use the same
    // storage-role resolver as the producer rather than inventing a slot map.
    context.rkl_acceptance=[&](const RklStageDescriptor& descriptor) {
        const auto& output=Numerics::Diffusion::detail::state_for(block,descriptor.output_slot);
        for(int i=grid.Is();i<grid.Ie();++i) {
            const double temperature=eos.get_temperature(output.rho[i],output.eng[i]/output.rho[i],
                &output.mass_fractions[i]);
            require(std::isfinite(temperature)&&temperature>0.,"thermal intermediate stage has invalid temperature");
        }
        require(std::all_of(output.stage_repairs.values.begin(),output.stage_repairs.values.end(),
            [](double value){return value==0.;}),"smooth thermal intermediate stage needed a repair");
        ++accepted_stages;
    };
    const long double z=lambda*dt;
    // Independent s=3 RKL2 polynomial, not the production coefficient routine.
    const long double polynomial=1.L+z+z*z/2.L+z*z*z/15.L;
    Result result;
    for(int step=1;step<=macrosteps;++step) {
        context.step_start_time=(step-1)*dt;context.step_dt=dt;
        context.boundary_start_time=context.step_start_time;context.boundary_step_dt=dt;
        RKL2TimeIntegrator::integrate(block,eos,grid,config,dt,dt_fe,boundary);
        const auto& state=block.fluid_state;
        const long double amplification=std::pow(polynomial,step);
        const long double continuous=std::exp(lambda*(step*dt));
        long double heat=0.,error=0.;
        for(int i=grid.Is();i<grid.Ie();++i) {
            const double* fraction=&state.mass_fractions[i];
            const double temperature=eos.get_temperature(state.rho[i],state.eng[i]/state.rho[i],fraction);
            require(std::isfinite(temperature)&&temperature>0.,"thermal evolution produced invalid temperature");
            const long double cosine=std::cos(2.L*pi*grid.GetCellCenterX(i));
            const long double exact_polynomial=2.L+amplitude*amplification*cosine;
            require(std::abs(static_cast<long double>(temperature)-exact_polynomial)/2.L<=1e-11L,
                "production thermal trajectory differs from independent stability polynomial");
            require(std::bit_cast<std::uint64_t>(state.rho[i])==std::bit_cast<std::uint64_t>(1.)
                &&std::bit_cast<std::uint64_t>(state.mom_u[i])==std::bit_cast<std::uint64_t>(0.)
                &&std::bit_cast<std::uint64_t>(state.mom_v[i])==std::bit_cast<std::uint64_t>(0.)
                &&std::bit_cast<std::uint64_t>(state.mom_w[i])==std::bit_cast<std::uint64_t>(0.)
                &&std::bit_cast<std::uint64_t>(state.X(0,i))==std::bit_cast<std::uint64_t>(1.),
                "pure thermal transport changed stationary conserved fields");
            heat+=state.eng[i];error+=std::abs(static_cast<long double>(temperature)
                -(2.L+amplitude*continuous*cosine));
        }
        const double heat_drift=static_cast<double>(std::abs(heat-initial_heat)/std::abs(initial_heat));
        require(heat_drift<=1e-12,"closed thermal evolution lost total heat");
        require(std::all_of(state.stage_repairs.values.begin(),state.stage_repairs.values.end(),
            [](double value){return value==0.;}),"smooth thermal evolution needed a state repair");
        result.endpoint_error=static_cast<double>(error/(2.L*count));
        result.maximum_error=std::max(result.maximum_error,result.endpoint_error);
        result.maximum_heat_drift=std::max(result.maximum_heat_drift,heat_drift);
    }
    require(accepted_stages==3*macrosteps,"thermal production stage acceptance hook was not executed");
    std::cout<<"THERMAL_FIVE_DECAY_FORMAL steps="<<macrosteps<<" stages=3 time="<<end
        <<" maximum_relative_L1="<<result.maximum_error<<" final_relative_L1="<<result.endpoint_error
        <<" maximum_heat_drift="<<result.maximum_heat_drift<<" coupled_hydro=0\n";
    return result;
}
/** Constant-stage time refinement and finest all-time semidiscrete heat budget. */
inline void run() {
    const std::array counts{64,96,128};std::array<Result,3> results{};
    for(std::size_t i=0;i<counts.size();++i)results[i]=trajectory(counts[i]);
    for(std::size_t i=0;i+1<counts.size();++i) {
        const double order=std::log(results[i].endpoint_error/results[i+1].endpoint_error)
            /std::log(static_cast<double>(counts[i+1])/counts[i]);
        require(std::isfinite(order)&&order>=1.8,"constant-stage thermal RKL2 lost second-order time convergence");
    }
    require(results.back().maximum_error<=1e-5,"thermal finest all-time accuracy budget exceeded");
}
} // namespace ThermalFiveDecayCases
