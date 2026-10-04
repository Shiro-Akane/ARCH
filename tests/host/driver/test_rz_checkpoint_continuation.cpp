// Internal RZ Host scheduler -> checkpoint -> reconstructed Runtime continuation.
// Existing constant-axial-translation fixture, not a scientific trajectory.
#include "driver/runtime/DriverRuntime.h"
#include "driver/DriverUtils.h"
#include "driver/schedule/DriverControl.h"
#include "driver/stages/DriverStages.h"
#include "driver/io/DriverIO.h"
#include "numerics/integrator/TimeIntegratorRK2.h"
#include "numerics/integrator/HydroSolverImpl.h"
#include "numerics/flux/FluxHLLC.h"
#include "physics/eos/IdealGas.h"
#include "io/chk/CheckpointCompatibility.h"
#include "core/files/FileFingerprint.h"
#include <bit>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
static void require(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
static constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
static constexpr double dt=.001; // Existing scheduled-Hydro engineering step.
static void initialize(amr::AMRControl& control,const SimConfig& config,int direction) {
    control.tree->LoadLeafGrid(config,2,{1,1,1,1,0},
        {0,1,0,1,static_cast<std::uint32_t>(direction==0?1:0)},
        {0,0,1,1,static_cast<std::uint32_t>(direction==0?0:1)},{0,0,0,0,0},rz);
    for(int id:control.tree->GetActiveBlocks()) {
        auto& b=control.pool->GetBlock(id);
        for(auto* state:{&b.fluid_state,&b.state_next,&b.state_scratch}) {
            state->InitSpecies(2);
            for(int cell=0;cell<b.grid.GetTotalSize();++cell) {
                state->set(cell,{2.,0.,6.,0.,21.5});
                state->X(0,cell)=.6;state->X(1,cell)=.4;
                state->enuc_rate[cell]=0.;
            }
        }
    }
}
static void advance(arch::driver::DriverRuntime& runtime,SimulationController& counters,
                    const Numerics::IHydroSolver& hydro) {
    arch::driver::DriverStageWorkspace workspace;
    arch::dispatch::ResolvedExecutionPlan plan{};
    plan.time_integrator=arch::dispatch::TimeIntegratorId::Rk2;
    auto context=runtime.stage_context();
    context.step_start_time=counters.t_current;
    {
        arch::scheduler::ScopedStageBinding scope(context,runtime.handles());
        arch::driver::advance_hydro(runtime,workspace,context,&plan,dt,
            &SolverRK2::solve<BCHandler>,nullptr,&hydro);
    }
    counters.dt_old=dt;
    counters.advance(dt);
    runtime.materialize_current_for_host();
    for(std::size_t n=0;n<runtime.handles().size();++n) {
        const auto coherence=context.ledger.inspect(
            {runtime.handles()[n],arch::state::StateSlot::Current});
        context.ledger.require_readable(
            {runtime.handles()[n],arch::state::StateSlot::Current},
            {arch::state::ExecutionSide::Host,coherence.interior.version,true,true});
    }
}
static std::vector<std::uint64_t> snapshot(const amr::AMRControl& control) {
    std::vector<std::uint64_t> values;
    for(int id:control.tree->GetActiveBlocks()) {
        const auto& b=control.pool->GetBlock(id);const auto& g=b.grid;
        values.push_back(b.level);values.push_back(b.logical_x1);
        values.push_back(b.logical_x2);values.push_back(b.logical_x3);
        for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
            const int cell=g.GetIndex(i,j,0);const auto f=b.fluid_state.get(cell);
            for(double v:{f.rho,f.mom_u,f.mom_v,f.mom_w,f.eng,b.fluid_state.enuc_rate[cell],
                b.fluid_state.X(0,cell),b.fluid_state.X(1,cell)})
                values.push_back(std::bit_cast<std::uint64_t>(v));
        }
    }
    return values;
}
int main(int argc,char** argv) {
 try {
    require(argc==2 || (argc==3 && std::string(argv[2])=="--repair-position"),
            "new local output root and optional --repair-position required");
    const bool repair_probe=argc==3;
    const std::filesystem::path root(argv[1]);require(!std::filesystem::exists(root),"output root exists");
    SpeciesManager species;species.add_species("gas0",1.,1.,1.4,3.);
    species.add_species("gas1",2.,1.,1.4,3.);IdealGas eos(1.4,species);
    Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> hydro(eos,rz);
    for(int direction:{0,1})for(double inner:{0.,1.}) {
        SimConfig config{};
        config.grid.dim=2;config.grid.geometry="cylindrical";
        config.grid.nblockx1=direction==0?2:1;
        config.grid.nblockx2=direction==0?1:2;config.grid.nblockx3=0;
        config.grid.x1_min=inner;config.grid.x1_max=inner+2.;
        config.grid.x2_min=-1.;config.grid.x2_max=1.;
        config.grid.amr_max_blocks=32;config.amr.lrefinemin=0;config.amr.lrefinemax=1;
        config.grid.x1l_boundary_type="outflow";config.grid.x1r_boundary_type="outflow";
        config.grid.x2l_boundary_type="outflow";config.grid.x2r_boundary_type="outflow";
        config.physics.gravity.type="none";
        config.numerics.entropy_fix_coeff=0.;config.numerics.hll_roe_wave_speed=true;
        config.numerics.sml_rho=1e-14;config.numerics.min_eint=1e-14;config.numerics.max_eint=1e10;
        // Deliberately trigger the existing floor machinery only in the
        // diagnostic fixture. This is not tuning a scientific run to pass.
        if (repair_probe) config.numerics.min_eint=100.;
        config.io.tmax=2*dt;
        config.io.out_dir=(root/(std::to_string(direction)+"-"+std::to_string(inner))).string();
        config.io.base_name="internal-rz";
        amr::AMRControl control(32,2);initialize(control,config,direction);
        RunState initial;SimulationController counters(config,initial);
        counters.repairs.reset(species.count());
        BCHandler boundary(config,rz);
        arch::driver::DriverRuntime runtime(control,boundary,config,species,counters);
        runtime.initialize_topology();
        advance(runtime,counters,hydro);
        if (repair_probe) {
            const auto& repairs=counters.repairs;
            require(repairs.values[0]>0.,"repair-position fixture did not generate real repairs");
            bool found=false;
            for(std::size_t n=0;n<runtime.handles().size();++n) {
                if(runtime.handles()[n].uid.value!=repairs.block_uid) continue;
                const auto& g=control.pool->GetBlock(control.tree->GetActiveBlocks()[n]).grid;
                const int cell=static_cast<int>(repairs.values[9]);
                const int k=cell/g.stride_z,j=(cell-k*g.stride_z)/g.stride_y;
                const int i=cell-k*g.stride_z-j*g.stride_y;
                const double expected_r=g.x1_min+(i-g.ng+.5)*g.dx1;
                const double expected_z=g.x2_min+(j-g.ng+.5)*g.dx2;
                std::cout<<std::setprecision(17)<<"RZ_REPAIR_POSITION direction="<<direction
                    <<" inner="<<inner<<" events="<<repairs.values[0]
                    <<" actual="<<repairs.position[0]<<","<<repairs.position[1]<<","<<repairs.position[2]
                    <<" expected="<<expected_r<<",0,"<<expected_z<<"\n";
                require(repairs.position[0]==expected_r && repairs.position[1]==0.
                    &&repairs.position[2]==expected_z,"Hydro repair position used old polar chart");
                require(repairs.stage==1 && repairs.time==0.,"repair representative stage/time changed");
                found=true;
            }
            require(found,"repair representative block identity missing");
        }
        const auto split=snapshot(control);
        const auto provenance=io::inspect_checkpoint_provenance(config,species,
            arch::dispatch::EosId::Ideal,false,"none",false);
        auto pressure=+[](const FluidVector&,const double*,const void*){return 5.;};
        auto temperature=+[](const FluidVector&,const double*,const void*){return 25./12.;};
        auto gamma=+[](const FluidVector&,const double*,const void*){return 1.4;};
        arch::driver::DriverIO output(runtime,counters,provenance,pressure,temperature,gamma,nullptr);
        output.write_checkpoint(1e99,false);
        const auto checkpoint=std::filesystem::path(config.io.out_dir)/"internal-rz_chk_0000.h5";
        const auto checkpoint_sha=arch::core::file_sha256(checkpoint.string());
        amr::AMRControl restored(32,2);RunState restart;
        read_chk(checkpoint.string(),restored,restart,config,species,provenance,io::current_rz_checkpoint_geometry());
        require(snapshot(restored)==split,"checkpoint did not preserve accepted interior bits");
        require(restart.time==dt&&restart.step==1&&restart.dt_old==dt,
            "split checkpoint controller not preserved");
        SimConfig resumed_config=config;resumed_config.io.restart=true;
        SimulationController resumed_counters(resumed_config,restart);
        BCHandler resumed_boundary(resumed_config,rz);
        arch::driver::DriverRuntime resumed(restored,resumed_boundary,resumed_config,species,resumed_counters);
        resumed.initialize_topology();
        advance(runtime,counters,hydro);advance(resumed,resumed_counters,hydro);
        require(snapshot(control)==snapshot(restored),"continued and restarted scheduler states differ");
        require(counters.t_current==2*dt&&resumed_counters.t_current==counters.t_current
            &&counters.step_count==2&&resumed_counters.step_count==2,"continued controller differs");
        require(counters.repairs.values==resumed_counters.repairs.values
            &&(repair_probe ? counters.repairs.values[0]>0. : counters.repairs.values[0]==0.),
            "continuation repair ledger differs");
        for(int axis=0;axis<3;++axis)
            require(counters.repairs.position[axis]==resumed_counters.repairs.position[axis],
                    "checkpoint repair position changed on continuation");
        require(counters.repairs.block_uid==resumed_counters.repairs.block_uid &&
                counters.repairs.stage==resumed_counters.repairs.stage &&
                counters.repairs.time==resumed_counters.repairs.time,
                "checkpoint repair representative identity changed");
        require(arch::core::file_sha256(checkpoint.string())==checkpoint_sha,
            "continuation modified source checkpoint");
        std::cout<<std::setprecision(17)<<"PASS RZ_CHECKPOINT_CONTINUATION direction="<<direction
            <<" inner="<<inner<<" leaves="<<restored.tree->GetActiveBlocks().size()
            <<" split_time="<<restart.time<<" final_time="<<resumed_counters.t_current
            <<" final_step="<<resumed_counters.step_count<<" bit_words="<<split.size()
            <<" checkpoint_sha="<<checkpoint_sha<<"\n";
    }
    return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
