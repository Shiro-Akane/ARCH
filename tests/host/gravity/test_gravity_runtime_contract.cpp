// Real DriverRuntime -> GravityStage -> SelfGravity, host publication contract.
// Cartesian supported identity path only; no RZ capability or evolution claim.
#include "amr/AMRControl.h"
#include "driver/DriverUtils.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "driver/stages/GravityStage.h"
#include "physics/gravity/self/SelfGravity.h"
#include "physics/gravity/GravityExecution.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

static void require(bool x,const char* m){if(!x)throw std::runtime_error(m);}
template<class F> static void rejects(F f,const char* m) {
    try{f();}catch(const std::logic_error&){return;}throw std::runtime_error(m);
}
class Capture final:public Physical::Gravity::GravityExecution {
    std::shared_ptr<Physical::Gravity::GravityExecution> host=Physical::Gravity::make_host_gravity_execution();
public:
    int gathers=0;
    std::vector<double> density;
    std::vector<Physical::Gravity::GravityCell> cells;
    std::shared_ptr<arch::multigrid::CompositeExecution> numeric() const override{return host->numeric();}
    void run(const Physical::Gravity::GravityWork& work) override {
        host->run(work);
        if(const auto* input=std::get_if<Physical::Gravity::GatherDensity>(&work)){
            host->numeric()->fence();++gathers;
            density.assign(input->out,input->out+input->size);
            cells.assign(input->cells,input->cells+input->size);
        }
    }
};
int main(int argc,char** argv){
 try {
    require(argc==2,"new persistent fixture directory required");
    const std::filesystem::path out(argv[1]);
    require(!std::filesystem::exists(out),"output directory must be new");
    SimConfig config;
    config.grid.dim=1;config.grid.nblockx1=4;config.grid.nblockx2=config.grid.nblockx3=0;
    config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="periodic";
    config.grid.x1_min=0.;config.grid.x1_max=1.;
    config.amr.lrefinemin=0;config.amr.lrefinemax=1;config.grid.amr_max_blocks=16;
    config.amr.refine_on_rho=true;config.amr.refine_threshold=.001;
    config.amr.regrid_interval=1;
    config.io.out_dir=out.string();
    SpeciesManager species;species.add_species("fixture",1.,1.,1.4,1.);
    amr::AMRControl control(16,1);control.tree->InitRootGrid(config,1);
    for(int id:control.tree->GetActiveBlocks()){
        auto& b=control.pool->GetBlock(id);
        for(auto* state:{&b.fluid_state,&b.state_next,&b.state_scratch}){
            state->InitSpecies(1);
            for(int i=0;i<b.grid.GetTotalSize();++i){
                const double x=b.grid.GetPhysicalCoords(i,0,0).x;
                state->set(i,{1.+.1*std::cos(2.*arch::constants::math::pi*x),0.,0.,0.,10.});
                state->X(0,i)=1.;
            }
        }
    }
    RunState start{};SimulationController counters(config,start);BCHandler boundary(config);
    arch::driver::DriverRuntime runtime(control,boundary,config,species,counters);
    runtime.initialize_topology();
    Physical::Gravity::SelfGravity gravity(config.physics.gravity);
    auto capture=std::make_shared<Capture>();gravity.set_execution(capture);
    arch::driver::GravityStage stage(runtime,&gravity);
    auto verify=[&](arch::state::StateSlot slot){
        const auto& active=control.tree->GetActiveBlocks();
        std::vector<bool> seen(active.size(),false);
        for(std::size_t i=0;i<capture->cells.size();++i){
            const auto c=capture->cells[i];require(c.block>=0&&std::size_t(c.block)<active.size(),"captured block map");
            const auto& b=control.pool->GetBlock(active[c.block]);
            const auto& state=slot==arch::state::StateSlot::Current?b.fluid_state
                :slot==arch::state::StateSlot::Next?b.state_next:b.state_scratch;
            require(capture->density[i]==state.rho[c.offset],"actual gathered density differs from requested slot/block");
            seen[c.block]=true;
        }
        for(bool x:seen)require(x,"active nonfirst block not gathered");
        require(gravity.potential().size()==capture->density.size(),"published extent");
    };
    stage.prepare_current(0.,true);verify(arch::state::StateSlot::Current);
    require(capture->gathers==1,"first actual prepare count");
    // Change only a nonfirst block/version. First-block shortcut cannot observe it.
    auto context=runtime.stage_context();
    const auto first=context.ledger.inspect({runtime.handles().front(),arch::state::StateSlot::Current}).interior.version;
    const auto changed=runtime.handles().back();
    auto& last=control.pool->GetBlock(control.tree->GetActiveBlocks().back());
    for(double& rho:last.fluid_state.rho)rho*=1.01;
    auto witness=context.clock.next_publication();
    context.ledger.publish_interior({changed,arch::state::StateSlot::Current},
        arch::state::ExecutionSide::Host,witness.version,witness.completion);
    require(context.ledger.inspect({runtime.handles().front(),arch::state::StateSlot::Current}).interior.version==first,
        "nonfirst publication changed first version");
    stage.prepare_current(.125,true);verify(arch::state::StateSlot::Current);
    require(capture->gathers==2,"nonfirst update failed to issue new gather");
    const auto plan=arch::scheduler::make_hydro_plan(arch::scheduler::HydroMethod::RK3);
    // Scratch has never been published: fail before any new gather and retire old field.
    const auto invoke=[&](const arch::scheduler::StageDescriptor& descriptor,double time){
        return stage.prepare({plan.method,descriptor,runtime.handles(),
            arch::state::ExecutionSide::Host,context.ledger,time,.25});
    };
    rejects([&]{invoke(plan.stages[1],.25);},"unpublished Scratch accepted");
    require(capture->gathers==2,"invalid slot reached actual gather");
    rejects([&]{gravity.potential();},"failed prepare retained consumable publication");
    // Only the nonfirst block remains unpublished: full-domain readability must fail.
    const auto prefix=std::span<const amr::BlockHandle>(runtime.handles()).first(runtime.handles().size()-1);
    arch::scheduler::publish_completed_interior(context,prefix,arch::state::StateSlot::Scratch);
    rejects([&]{invoke(plan.stages[1],.25);},"unpublished nonfirst Scratch accepted");
    require(capture->gathers==2,"nonfirst invalid dependency reached actual gather");
    for(auto slot:{arch::state::StateSlot::Scratch,arch::state::StateSlot::Next}){
        for(int id:control.tree->GetActiveBlocks()){
            auto& b=control.pool->GetBlock(id);
            auto& state=slot==arch::state::StateSlot::Scratch?b.state_scratch:b.state_next;
            for(double& rho:state.rho)rho+=slot==arch::state::StateSlot::Scratch?.2:.4;
        }
        arch::scheduler::publish_completed_interior(context,runtime.handles(),slot);
        const auto& descriptor=plan.stages[slot==arch::state::StateSlot::Scratch?1:2];
        invoke(descriptor,slot==arch::state::StateSlot::Scratch?.25:.125);verify(slot);
    }
    require(capture->gathers==4,"actual stage slots not gathered once each");
    stage.invalidate();rejects([&]{gravity.potential();},"explicit invalidation retained publication");
    // Restore common Current version before real Runtime transaction/halo refresh.
    arch::scheduler::publish_completed_interior(context,runtime.handles(),arch::state::StateSlot::Current);
    runtime.ensure_fluid_ghosts();
    const auto old_epoch=runtime.handles().front().epoch;
    const auto old_blocks=runtime.handles().size();
    require(runtime.perform_regrid(0,0.),"actual Runtime refine witness missing");
    require(runtime.handles().front().epoch!=old_epoch,"regrid did not turn topology identity");
    require(runtime.handles().size()>old_blocks,"actual refine did not increase active leaves");
    stage.prepare_current(.5,true);verify(arch::state::StateSlot::Current);
    require(capture->gathers==5,"regrid did not rebind/gather current domain");
    require(counters.t_current==0.&&counters.step_count==0,"fixture advanced simulation controller");
    std::cout<<"ACTUAL_GRAVITY_RUNTIME_CONTRACT_PASS initial_blocks="<<old_blocks
        <<" refined_blocks="<<runtime.handles().size()<<" gathers="<<capture->gathers
        <<" cells="<<capture->cells.size()<<" time=0 steps=0\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
