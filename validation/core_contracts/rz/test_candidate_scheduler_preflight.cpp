#include <bit>
#include <iostream>
#include "physics/gravity/ExternalGravity.h"
#include "driver/schedule/StageScheduler.h"
using namespace arch::state;
using namespace arch::scheduler;
using namespace Physical::Gravity;
static auto snapshot(const FluidState& state) {
    std::vector<std::uint64_t> result;
    for(const auto* values:{&state.rho,&state.mom_u,&state.mom_v,&state.mom_w,&state.eng,&state.enuc_rate,&state.mass_fractions})
        for(double value:*values)result.push_back(std::bit_cast<std::uint64_t>(value));
    return result;
}
struct Preparation final:HydroStagePreparation {
    const ExternalGravity& source;
    const Grid& grid;
    const std::array<FluidState,2>& inputs;
    int completed_patches=0;
    Preparation(const ExternalGravity& s,const Grid& g,const std::array<FluidState,2>& i):source(s),grid(g),inputs(i){}
    CompletionToken prepare(const HydroStagePreparationRequest& request) override {
        // Test adapter only: production config/storage owner binding is NOT claimed.
        std::vector<std::vector<FluidVector>> prepared;
        for(std::size_t i=0;i<request.handles.size();++i) {
            const auto h=request.handles[i];
            const auto version=request.ledger.inspect({h,request.descriptor.input_slot}).interior.version;
            ExternalGravity::RzStageIdentity frame{
                GravitySourceOrigin::ExternalNativeOrthonormal,
                GridMetrics::GeometrySemantics::AxisymmetricRz,
                h,request.descriptor.input_slot,version,3,2,0,request.descriptor.stage,
                {.3,-.35,-.025},&inputs[i]};
            prepared.push_back(source.produce_rz_stage_patch<MinMod>(
                inputs[i],grid,request.step_dt,frame,frame,request.ledger));
            ++completed_patches;
        }
        return {1,CompletionState::Complete};
    }
};
int main() {
    Grid grid(2,1.,3.,-1.,1.,0.,0.,1,1,0);
    grid.dim=2;grid.geometry="cylindrical";
    grid.InitializeTopology(GridMetrics::GeometrySemantics::AxisymmetricRz);
    ExternalGravity source(.3,-.35,-.025,GridMetrics::GeometrySemantics::AxisymmetricRz);
    int rejected=0;
    for(auto method:{HydroMethod::Euler,HydroMethod::RK2,HydroMethod::RK3}) {
        std::array<FluidState,2> current,next,scratch;
        for(auto* slots:{&current,&next,&scratch})for(auto& s:*slots) {
            s.Preallocate(grid.GetTotalSize());
            std::fill(s.rho.begin(),s.rho.end(),2.);
            std::fill(s.eng.begin(),s.eng.end(),100.);
        }
        current[1].eng[grid.GetIndex(grid.Ie()-1,grid.Je()-1,0)]=std::numeric_limits<double>::quiet_NaN();
        const std::array<amr::BlockHandle,2> handles{{{{901},{17}},{{902},{17}}}};
        StateResidencyLedger ledger({17});
        for(auto h:handles) {
            ledger.register_block(h,{1},{1,CompletionState::Complete});
            ledger.publish_ghost({h,StateSlot::Current},ExecutionSide::Host,{1},{2,CompletionState::Complete});
        }
        std::vector<std::vector<std::uint64_t>> before;
        for(const auto* slots:{&current,&next,&scratch})for(const auto& s:*slots)before.push_back(snapshot(s));
        auto publication=[&]() {
            std::vector<std::uint64_t> result;
            for(auto h:handles)for(auto slot:{StateSlot::Current,StateSlot::Next,StateSlot::Scratch}) {
                const auto c=ledger.inspect({h,slot});
                for(auto part:{c.interior,c.ghost}) {
                    result.push_back(static_cast<unsigned>(part.residency));
                    result.push_back(part.version.value);result.push_back(part.completion.value);
                    result.push_back(static_cast<unsigned>(part.completion.state));
                    result.push_back(static_cast<unsigned>(part.pending_transfer));
                }
                result.push_back(c.ghost_source_version.value);
            }
            return result;
        };
        const auto publication_before=publication();
        MonotonicSchedulerClock clock(2,1);
        Preparation preparation(source,grid,current);
        StageExecutionContext context{ExecutionSide::Host,ledger,clock,&preparation,{},0.,1e-4};
        int writes=0,boundaries=0,rotations=0,refluxes=0,acceptances=0;
        context.hydro_acceptance=[&](const auto&){++acceptances;};
        bool failed=false;
        try {
            execute_hydro_plan(context,handles,make_hydro_plan(method),
                [&](const auto&,CompletionToken t){++writes;return t;},
                [&](StateSlot,StateVersion,CompletionToken t){++boundaries;return t;},
                [&]{++rotations;},
                [&](const auto&,StateSlot,CompletionToken t){++refluxes;return t;});
        } catch(const std::invalid_argument&) {failed=true;}
          catch(const std::runtime_error&) {failed=true;}
        std::vector<std::vector<std::uint64_t>> after;
        for(const auto* slots:{&current,&next,&scratch})for(const auto& s:*slots)after.push_back(snapshot(s));
        if(!failed||preparation.completed_patches!=1||writes||boundaries||rotations||refluxes||acceptances
            ||before!=after||publication_before!=publication()||clock.last_token()!=2||clock.last_version()!=1)
            throw std::runtime_error("late domain rejection failed to preserve pre-execution state");
        ++rejected;
    }
    std::cout<<"RZ_SCHEDULER_PREFLIGHT_PASS plans="<<rejected
             <<" blocks=2 late_failure=PASS arrays=unchanged ledger=unchanged clock=unchanged callbacks=0 numerical_RK=NOT_RUN\n";
}
