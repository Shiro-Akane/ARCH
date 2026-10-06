// Actual CPU RZ Runtime ledger -> Stage -> native candidate; no timestep.
// Scientific/physical consumers and regrid/Device production gates stay held.
#include "amr/AMRControl.h"
#include "driver/DriverUtils.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "driver/stages/GravityStage.h"
#include "physics/eos/IdealGas.h"
#include "physics/gravity/self/SelfGravity.h"
#include "physics/gravity/GravityExecution.h"
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
static void require(bool x,const char* m){if(!x)throw std::runtime_error(m);}
template<class F> static void rejects(F f,const char* m){
    bool failed=false;try{f();}catch(const std::exception&){failed=true;}
    require(failed,m);
}
class Capture final:public Physical::Gravity::GravityExecution {
    std::shared_ptr<Physical::Gravity::GravityExecution> host=
        Physical::Gravity::make_host_gravity_execution();
public:
    int gathers=0;std::vector<double> density;
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
    require(argc==2,"new persistent output directory required");
    const std::filesystem::path out(argv[1]);require(!std::filesystem::exists(out),"new directory required");
    SimConfig config;config.grid.geometry="cylindrical";config.grid.dim=2;
    config.grid.nblockx1=2;config.grid.nblockx2=1;config.grid.nblockx3=0;
    config.grid.x1_min=0.;config.grid.x1_max=1.;config.grid.x2_min=-.5;config.grid.x2_max=.5;
    config.grid.amr_max_blocks=8;config.amr.lrefinemax=1;
    config.physics.gravity.boundary="isolated";
    config.physics.gravity.relative_tolerance=1.e-10;config.physics.gravity.absolute_tolerance=0.;
    config.physics.gravity.max_cycles=200;config.io.out_dir=out.string();
    SpeciesManager species;species.add_species("fixture",1.,1.,1.4,1.);
    IdealGas eos(1.4,species);
    amr::AMRControl control(8,2);
    control.tree->InitRootGrid(config,1,GridMetrics::GeometrySemantics::AxisymmetricRz);
    for(int id:control.tree->GetActiveBlocks()){
        auto& b=control.pool->GetBlock(id);
        for(auto* state:{&b.fluid_state,&b.state_next,&b.state_scratch}){
            state->InitSpecies(1);
            for(int c=0;c<b.grid.GetTotalSize();++c){state->set(c,{1.,0.,0.,0.,10.});state->X(0,c)=1.;}
        }
    }
    RunState start{};SimulationController counters(config,start);
    BCHandler boundary(config,GridMetrics::GeometrySemantics::AxisymmetricRz);
    arch::driver::DriverRuntime runtime(control,boundary,config,species,counters);
    runtime.bind_native_rz_eos(eos);
    runtime.initialize_topology();
    Physical::Gravity::SelfGravity gravity(config.physics.gravity);
    auto capture=std::make_shared<Capture>();gravity.set_execution(capture);
    using Stage=arch::driver::GravityStage;using Slot=arch::state::StateSlot;
    Stage production(runtime,&gravity);
    rejects([&]{production.prepare_current(0.,true);},"default stage enabled RZ");
    require(capture->gathers==0,"default RZ gate delegated work");
    Stage stage(runtime,&gravity,Stage::Qualification::NativeRzCandidate);
    auto verify=[&](Slot slot){
        const auto& result=gravity.native_rz_assessment();
        require(result.source.inputs.size()==runtime.handles().size(),"Runtime inputs incomplete");
        require(result.source.topology==runtime.handles().front().epoch,"Runtime topology identity lost");
        require(result.source.input_time==0.,"t=0 Runtime request changed");
        auto context=runtime.stage_context();
        std::vector<bool> seen(runtime.handles().size(),false);
        for(std::size_t n=0;n<runtime.handles().size();++n){
            const auto& input=result.source.inputs[n];
            const auto interior=context.ledger.inspect({runtime.handles()[n],slot}).interior;
            require(input.block==runtime.handles()[n]&&input.slot==slot
                &&input.version==interior.version,"actual Runtime lease/version mismatch");
            require(input.storage_generation>0,"Runtime lease generation missing");
        }
        for(std::size_t n=0;n<capture->cells.size();++n){
            const auto cell=capture->cells[n];
            require(cell.block>=0&&std::size_t(cell.block)<seen.size(),"native block map mismatch");
            const auto& b=control.pool->GetBlock(control.tree->GetActiveBlocks()[cell.block]);
            const auto& state=slot==Slot::Current?b.fluid_state:slot==Slot::Scratch?b.state_scratch:b.state_next;
            require(capture->density[n]==state.rho[cell.offset],"Runtime selected buffer not gathered");
            seen[cell.block]=true;
        }
        for(bool found:seen)require(found,"nonfirst native block omitted");
        require(gravity.native_rz_potential().size()==512,"Runtime native extent changed");
        rejects([&]{gravity.potential();},"Runtime candidate escaped output scope");
        rejects([&]{stage.plot_fields();},"candidate allowed physical plot output");
        rejects([&]{stage.timestep();},"candidate allowed physical CFL");
        rejects([&]{gravity.patch_view(1);},"candidate allowed nonfirst Hydro patch");
        std::cout<<"ACTUAL_RZ_RUNTIME_SLOT_PASS slot="<<static_cast<int>(slot)
            <<" lease="<<result.source.inputs.front().storage_generation
            <<" source_generation="<<result.source_generation<<" cells=512 time=0 steps=0"<<std::endl;
    };
    stage.prepare_current(0.,true);verify(Slot::Current);
    const auto plan=arch::scheduler::make_hydro_plan(arch::scheduler::HydroMethod::RK3);
    auto context=runtime.stage_context();
    auto invoke=[&](const arch::scheduler::StageDescriptor& descriptor){
        return stage.prepare({plan.method,descriptor,runtime.handles(),arch::state::ExecutionSide::Host,
            context.ledger,0.,0.});
    };
    rejects([&]{invoke(plan.stages[1]);},"unpublished Scratch accepted");
    require(capture->gathers==1,"unpublished Scratch reached gather");
    rejects([&]{gravity.native_rz_assessment();},"invalid lease retained field");
    const auto prefix=std::span<const amr::BlockHandle>(runtime.handles()).first(runtime.handles().size()-1);
    arch::scheduler::publish_completed_interior(context,prefix,Slot::Scratch);
    rejects([&]{invoke(plan.stages[1]);},"unpublished nonfirst Scratch accepted");
    require(capture->gathers==1,"unpublished nonfirst reached gather");
    for(auto slot:{Slot::Scratch,Slot::Next}){
        for(int id:control.tree->GetActiveBlocks()){
            auto& b=control.pool->GetBlock(id);
            for(auto* state:{&b.fluid_state,&b.state_scratch,&b.state_next})
                for(double& rho:state->rho)rho=std::numeric_limits<double>::quiet_NaN();
            auto& selected=slot==Slot::Scratch?b.state_scratch:b.state_next;
            for(double& rho:selected.rho)rho=1.;
        }
        arch::scheduler::publish_completed_interior(context,runtime.handles(),slot);
        invoke(plan.stages[slot==Slot::Scratch?1:2]);verify(slot);
    }
    require(capture->gathers==3,"actual RZ Runtime gather count");
    stage.invalidate();rejects([&]{gravity.native_rz_potential();},"invalidation retained candidate");
    rejects([&]{runtime.perform_regrid(0,0.);},"RZ production regrid gate lifted");
    require(capture->gathers==3,"regrid gate executed gravity");
    std::cout<<"ACTUAL_RZ_RUNTIME_CONTRACT_PASS blocks=2 gathers=3 slots=3"
        <<" actual_ledger=1 nonfirst_unpublished=1 invalidation=1"
        <<" physical_consumers_rejected=1 regrid_gate_held=1 time=0 steps=0"<<std::endl;
    return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
