#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include "physics/gravity/ExternalGravity.h"
using namespace Physical::Gravity;
using namespace arch::state;
static std::vector<std::uint64_t> bits(const FluidState& s) {
    std::vector<std::uint64_t> out;
    for(const auto* v:{&s.rho,&s.mom_u,&s.mom_v,&s.mom_w,&s.eng,&s.enuc_rate,&s.mass_fractions})
        for(double q:*v)out.push_back(std::bit_cast<std::uint64_t>(q));
    return out;
}
static std::string stamp(const StateResidencyLedger& ledger,amr::BlockHandle handle) {
    std::ostringstream out;
    for(auto slot:{StateSlot::Current,StateSlot::Next,StateSlot::Scratch}) {
        const auto s=ledger.inspect({handle,slot});
        for(auto c:{s.interior,s.ghost})
            out<<int(c.residency)<<','<<c.version.value<<','<<c.completion.value<<','
               <<int(c.completion.state)<<','<<int(c.pending_transfer)<<';';
        out<<s.ghost_source_version.value<<';';
    }
    return out.str();
}
static void close(long double a,long double b) {
    if(!std::isfinite(a)||!std::isfinite(b)||std::abs(a-b)>1e-12L*std::abs(b))
        throw std::runtime_error("independent integrated physical-stage mismatch");
}
int main() {
    const auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    Grid grid(2,1.,3.,-1.,1.,0.,0.,1,1,0);
    grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(rz);
    FluidState current,next,scratch;
    for(auto* s:{&current,&next,&scratch}) {
        s->Preallocate(grid.GetTotalSize());
        for(int c=0;c<grid.GetTotalSize();++c) {
            const int i=c%grid.stride_y;
            const long double l=grid.GetFacePosL(i),h=grid.GetFacePosR(i);
            // Independent polynomial W average, not production centroid.
            const long double rw=.75L*(h*h*h*h-l*l*l*l)/(h*h*h-l*l*l);
            s->rho[c]=2;s->mom_w[c]=double(2*rw);s->eng[c]=100;
        }
    }
    const amr::BlockHandle handle{{901},{17}};
    StateResidencyLedger ledger({17});
    ledger.register_block(handle,{1},{1,CompletionState::Complete});
    ledger.publish_ghost({handle,StateSlot::Current},ExecutionSide::Host,{1},{2,CompletionState::Complete});
    ExternalGravity source(.3,-.35,-.025,rz);
    ExternalGravity::RzStageIdentity expected{
        GravitySourceOrigin::ExternalNativeOrthonormal,rz,handle,StateSlot::Current,{1},
        3,2,0,1,{.3,-.35,-.025},&current};
    for(double& q:next.mom_w)q*=1.5;
    for(double& q:scratch.mom_w)q*=2.;
    const auto state0=bits(current),next0=bits(next),scratch0=bits(scratch);
    const auto ledger0=stamp(ledger,handle);
    auto increments=source.produce_rz_stage_patch<MinMod>(current,grid,1e-4,expected,expected,ledger);
    long double J=0,E=0,Mr=0,Mz=0;
    const long double pi=std::acos(-1.L);
    for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
        const long double l=grid.GetFacePosL(i),h=grid.GetFacePosR(i);
        const long double V=pi*(h*h-l*l)*grid.dx2;
        const long double W=2*pi*(h*h*h-l*l*l)*grid.dx2/3;
        const auto d=increments[grid.GetIndex(i,j,0)];
        J+=d.mom_w*W;E+=d.eng*V;Mr+=d.mom_u*V;Mz+=d.mom_v*V;
    }
    close(J,-13.L/150000*2*pi);
    close(E,-13.L/150000*2*pi);
    close(Mr,3.L/50000*8*2*pi);
    close(Mz,-7.L/100000*8*2*pi);
    if(bits(current)!=state0||bits(next)!=next0||bits(scratch)!=scratch0||stamp(ledger,handle)!=ledger0)
        throw std::runtime_error("producer mutated state or publication ledger");
    int rejected=0;
    auto reject=[&](auto identity,auto expected_identity) {
        const auto before=bits(current),bn=bits(next),bs=bits(scratch);
        const auto publication=stamp(ledger,handle);
        bool failed=false;
        try {(void)source.produce_rz_stage_patch<MinMod>(current,grid,1e-4,identity,expected_identity,ledger);}
        catch(const std::exception&) {failed=true;}
        if(!failed||bits(current)!=before||bits(next)!=bn||bits(scratch)!=bs
            ||stamp(ledger,handle)!=publication)throw std::runtime_error("rejection or preservation failed");
        ++rejected;
    };
    auto bad=expected;bad.origin=GravitySourceOrigin::Unknown;reject(bad,expected);
    bad=expected;bad.chart=GridMetrics::GeometrySemantics::Existing;reject(bad,expected);
    bad=expected;bad.block.epoch={18};reject(bad,bad); // actual ledger rejects stale topology
    bad=expected;bad.version={2};reject(bad,bad); // actual ledger rejects stale version
    bad=expected;bad.slot=StateSlot::Next;reject(bad,bad); // unpublished work slot
    bad=expected;bad.storage_generation=4;reject(bad,expected);
    bad=expected;bad.config_revision=3;reject(bad,expected);
    bad=expected;bad.stage=2;reject(bad,expected);
    bad=expected;bad.macro_step=1;reject(bad,expected);
    bad=expected;bad.acceleration[2]=.05;reject(bad,bad); // actual policy mismatch
    bad=expected;bad.state=&next;reject(bad,bad);
    const int last=grid.GetIndex(grid.Ie()-1,grid.Je()-1,0);
    const double saved=current.mom_w[last];
    current.mom_w[last]=std::numeric_limits<double>::quiet_NaN();
    reject(expected,expected);current.mom_w[last]=saved; // late-cell failure discards private prefix
    // Actual ledger-published work slots, each with a different physical input.
    ledger.publish_interior({handle,StateSlot::Next},ExecutionSide::Host,{2},{3,CompletionState::Complete});
    ledger.publish_ghost({handle,StateSlot::Next},ExecutionSide::Host,{2},{4,CompletionState::Complete});
    ledger.publish_interior({handle,StateSlot::Scratch},ExecutionSide::Host,{3},{5,CompletionState::Complete});
    ledger.publish_ghost({handle,StateSlot::Scratch},ExecutionSide::Host,{3},{6,CompletionState::Complete});
    for(auto slot:{StateSlot::Next,StateSlot::Scratch}) {
        auto* input=slot==StateSlot::Next?&next:&scratch;
        auto frame=expected;frame.slot=slot;frame.state=input;
        frame.version={slot==StateSlot::Next?2u:3u};frame.stage=slot==StateSlot::Next?3:2;
        const auto before=bits(current),bn=bits(next),bs=bits(scratch);
        const auto publication=stamp(ledger,handle);
        auto delta=source.produce_rz_stage_patch<MinMod>(*input,grid,1e-4,frame,frame,ledger);
        long double work=0;
        for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
            const long double l=grid.GetFacePosL(i),h=grid.GetFacePosR(i);
            work+=delta[grid.GetIndex(i,j,0)].eng*pi*(h*h-l*l)*grid.dx2;
        }
        close(work,(-13.L/150000*2*pi)*(slot==StateSlot::Next?1.5L:2.L));
        if(bits(current)!=before||bits(next)!=bn||bits(scratch)!=bs||stamp(ledger,handle)!=publication)
            throw std::runtime_error("work-slot producer mutated state or ledger");
    }
    // Additional polynomial source check: rho(r)=2+r, physical m_phi(r)=2r.
    // Density is V averaged independently; angular torque needs its W average.
    for(int c=0;c<grid.GetTotalSize();++c) {
        const int i=c%grid.stride_y;
        const long double l=grid.GetFacePosL(i),h=grid.GetFacePosR(i);
        current.rho[c]=double(2+(2.L/3)*(h*h*h-l*l*l)/(h*h-l*l));
    }
    ledger.publish_interior({handle,StateSlot::Current},ExecutionSide::Host,{2},{7,CompletionState::Complete});
    ledger.publish_ghost({handle,StateSlot::Current},ExecutionSide::Host,{2},{8,CompletionState::Complete});
    auto variable=expected;variable.version={2};variable.stage=2;
    const auto variable_before=bits(current),variable_next=bits(next),variable_scratch=bits(scratch);
    const auto variable_publication=stamp(ledger,handle);
    auto variable_delta=source.produce_rz_stage_patch<MinMod>(current,grid,1e-4,variable,variable,ledger);
    long double variable_J=0,variable_E=0,variable_Mr=0,variable_Mz=0;
    for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
        const long double l=grid.GetFacePosL(i),h=grid.GetFacePosR(i);
        const long double V=pi*(h*h-l*l)*grid.dx2;
        const long double W=2*pi*(h*h*h-l*l*l)*grid.dx2/3;
        const auto d=variable_delta[grid.GetIndex(i,j,0)];
        variable_J+=d.mom_w*W;variable_E+=d.eng*V;
        variable_Mr+=d.mom_u*V;variable_Mz+=d.mom_v*V;
    }
    close(variable_J,-7.L/37500*2*pi);
    close(variable_E,-13.L/150000*2*pi);
    close(variable_Mr,1.L/1000*2*pi);
    close(variable_Mz,-7.L/6000*2*pi);
    if(bits(current)!=variable_before||bits(next)!=variable_next||bits(scratch)!=variable_scratch
       ||stamp(ledger,handle)!=variable_publication)throw std::runtime_error("variable source publication changed");
    // A fresh interior invalidates its ghosts: no stale halo may be consumed.
    ledger.publish_interior({handle,StateSlot::Current},ExecutionSide::Host,{3},{9,CompletionState::Complete});
    bad=expected;bad.version={3};reject(bad,bad);
    std::cout<<"RZ_REAL_PATCH_PRODUCER_PASS cells="<<(grid.Ie()-grid.Is())*(grid.Je()-grid.Js())
             <<" positive_slots=3 variable_density=PASS rejection_cases="<<rejected
             <<" states=Current+Next+Scratch ledger=unchanged scheduler=NOT_BOUND RK=NOT_RUN\n";
}
