// Internal RZ Host scheduler -> checkpoint -> reconstructed Runtime continuation.
// Existing constant-axial-translation fixture, not a scientific trajectory.
#include "host/gravity/NativeActiveAmrWitness.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/DriverUtils.h"
#include "driver/schedule/DriverControl.h"
#include "driver/stages/DriverStages.h"
#include "driver/io/DriverIO.h"
#include "math/io/PlotIdentityFixture.h"
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
    context.step_dt=dt; // Freeze the same step used by integrator, source and boundary receipts.
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
/** Canonical active-state words sorted by actual logical key; new runtime
 * UIDs may differ, while original conserved/X/ENUC bits must be identical.
 */
static std::vector<std::uint64_t> warm_words(native_active_four_module::Owner& owner) {
    auto cells=owner.freeze_active();
    std::sort(cells.begin(),cells.end(),[](const auto& a,const auto& b) {
        return std::tuple{a.level,a.logical,a.index}<std::tuple{b.level,b.logical,b.index};
    });
    std::vector<std::uint64_t> words;
    for(const auto& c:cells) {
        words.push_back(c.level);for(auto v:c.logical)words.push_back(v);words.push_back(c.index);
        for(double v:{c.fluid.rho,c.fluid.mom_u,c.fluid.mom_v,c.fluid.mom_w,c.fluid.eng,c.enuc})
            words.push_back(std::bit_cast<std::uint64_t>(v));
        for(double v:c.fractions)words.push_back(std::bit_cast<std::uint64_t>(v));
    }
    return words;
}
/** Exact existing repair record equality includes the representative identity. */
static void warm_repairs(const arch::state::RepairBudget& a,const arch::state::RepairBudget& b) {
    ::require(a.semantics==b.semantics&&a.values.size()==b.values.size()
        &&a.block_uid==b.block_uid&&a.stage==b.stage
        &&native_active_four_module::bits(a.time,b.time),"Warm continuation repair identity differs");
    for(std::size_t i=0;i<a.values.size();++i)
        ::require(native_active_four_module::bits(a.values[i],b.values[i]),"Warm repair value bits differ");
    for(int axis=0;axis<3;++axis)
        ::require(native_active_four_module::bits(a.position[axis],b.position[axis]),"Warm repair position bits differ");
}
/** Immutable local IO input, owned for the synchronous EMPTY construction. */
struct WarmCheckpointInput {
    std::string checkpoint,table,table_sha;
};
/** Approved IO TU loader: genuine EMPTY read_chk, never Init then overwrite.
 * Workflow: inspect the actual table/scientific identity after the fresh Helm
 * owner loads, reconstruct native means into EMPTY storage, verify the saved
 * table against that same file, then return for actual Runtime ghost/EOS bind.
 * Helm table stream bytes are pinned before/after construction by this caller
 * and the strict runner; this callback does not claim ghost or source readiness.
 */
static void load_warm_checkpoint(void* payload,amr::AMRControl& control,RunState& state,
    const SimConfig& config,const SpeciesManager& species,HelmEos& eos) {
    const auto& input=*static_cast<const WarmCheckpointInput*>(payload);
    ::require(control.tree->GetActiveBlocks().empty(),"Warm IO loader requires EMPTY genuine storage");
    const auto actual_table_sha=arch::core::file_sha256(input.table);
    ::require(actual_table_sha==input.table_sha,"Actual fresh Helm table changed during construction");
    const auto provenance=io::inspect_checkpoint_provenance(config,species,
        arch::dispatch::EosId::Helmholtz,true,"aprox13",false);
    io::require_loaded_eos_table_compatible(provenance.eos_table_sha256,actual_table_sha);
    read_chk(input.checkpoint,control,state,config,species,provenance,io::current_rz_checkpoint_geometry());
    io::require_loaded_eos_table_compatible(state.verified_eos_table_sha256,actual_table_sha);
    ::require(arch::core::file_sha256(input.table)==actual_table_sha,"Actual table changed during read_chk");
    static_cast<void>(eos); // Genuine owner exists; only Runtime grants its completed-patch EOS.
}
/** Actual active B/D/RK2Self mixed-AMR -> one checkpoint -> fresh M2.
 * The endpoint is frozen before owners. Each M2 uses its own actual accounting
 * differences; process-local journal prefixes/budgets are never checkpointed.
 * No formal Plot identity is invented: DriverIO's Plot member is unused here.
 */
static void warm_continuation(const std::filesystem::path& root,const std::string& table,
    arch::driver::GravityStage::Qualification qualification=arch::driver::GravityStage::Qualification::NativeRzSelfHydroCandidate) {
    using namespace native_active_four_module;
    const auto table_sha=arch::core::file_sha256(table);
    Owner original(table,true,3.*macro_dt,{},(root/"uninterrupted").string(),qualification);
    ::require(arch::core::file_sha256(table)==table_sha,"Actual new Helm table changed during construction");
    advance_dynamic_to_m1(original);
    ::require(bits(original.controller->dt_old,macro_dt),"Warm M1 lost actual dt_old");
    const auto split=warm_words(original);const auto repairs=original.controller->repairs;
    const double advice=original.burn_advice;
    const auto provenance=io::inspect_checkpoint_provenance(original.config,original.species,
        arch::dispatch::EosId::Helmholtz,true,"aprox13",false);
    ::require(provenance.eos_table_sha256==table_sha,"Warm checkpoint table identity changed");
    auto pressure=+[](const FluidVector& U,const double* X,const void* context) {
        return static_cast<const HelmEos*>(context)->get_pressure(U,X);
    };
    auto temperature=+[](const FluidVector& U,const double* X,const void* context) {
        return static_cast<const HelmEos*>(context)->get_temperature(U.rho,arch::state::recover(U).internal,X);
    };
    auto gamma=+[](const FluidVector& U,const double* X,const void* context) {
        const auto& eos=*static_cast<const HelmEos*>(context);const double p=eos.get_pressure(U,X);
        const double c=eos.get_sound_speed(U,p,X);
        return std::isfinite(p)&&p>0.&&std::isfinite(c)&&c>0.&&U.rho>0.?
            U.rho*c*c/p:std::numeric_limits<double>::quiet_NaN();
    };
    const io::PlotSourceIdentity unused_checkpoint_only_plot{};
    arch::driver::DriverIO output(*original.runtime,*original.controller,provenance,
        unused_checkpoint_only_plot,pressure,temperature,gamma,original.eos.get());
    original.gravity_stage->flush_committed_diagnostics();
    output.write_checkpoint(advice,false);
    const auto file=std::filesystem::path(original.config.io.out_dir)/"native-active_chk_0000.h5";
    const auto checkpoint_sha=arch::core::file_sha256(file.string());
    ::require(warm_words(original)==split,"Checkpoint writer changed native conserved means");
    WarmCheckpointInput loader_input{file.string(),table,table_sha};
    Owner resumed(table,true,3.*macro_dt,
        EmptyInitialization{&load_warm_checkpoint,&loader_input},(root/"resumed").string(),qualification);
    ::require(warm_words(resumed)==split,"Fresh EMPTY checkpoint owner changed accepted native bits");
    ::require(bits(resumed.controller->t_current,original.controller->t_current)
        &&resumed.controller->step_count==original.controller->step_count
        &&bits(resumed.controller->dt_old,original.controller->dt_old)&&bits(resumed.burn_advice,advice)
        &&resumed.controller->chk_file_index==original.controller->chk_file_index
        &&resumed.controller->plt_file_index==original.controller->plt_file_index,
        "Fresh warm checkpoint lost actual controller/advice/IO counters");
    warm_repairs(resumed.controller->repairs,repairs);
    prepare_continuation_current(original);prepare_continuation_current(resumed);
    ::require(warm_words(original)==split&&warm_words(resumed)==split,
        "Actual restart Current changed checkpoint fluid bits");
    const auto first=original.totals(),second=resumed.totals();
    const auto first_boundary=boundary_before(original),second_boundary=boundary_before(resumed);
    original.advance();check_macro_balance(original,first,first_boundary,original.totals());report_macro(original,2,original.macro_wall_seconds);
    resumed.advance();check_macro_balance(resumed,second,second_boundary,resumed.totals());report_macro(resumed,2,resumed.macro_wall_seconds);
    ::require(warm_words(original)==warm_words(resumed),"Warm M2 full native U/X/ENUC bitwise continuation differs");
    ::require(bits(original.controller->t_current,3.*macro_dt)
        &&bits(original.controller->t_current,resumed.controller->t_current)
        &&original.controller->step_count==3&&resumed.controller->step_count==3
        &&bits(original.controller->dt_old,resumed.controller->dt_old)
        &&bits(original.burn_advice,resumed.burn_advice)
        &&original.controller->plt_file_index==resumed.controller->plt_file_index
        &&original.controller->chk_file_index==resumed.controller->chk_file_index,
        "Warm M2 controller/counter/advice bits differ");
    warm_repairs(original.controller->repairs,resumed.controller->repairs);
    ::require(arch::core::file_sha256(file.string())==checkpoint_sha
        &&arch::core::file_sha256(table)==table_sha,"Warm consumers changed checkpoint/table contents");
    std::cout<<std::setprecision(17)<<(original.production()?"PASS PUBLIC_NATIVE_ACTIVE_CHECKPOINT_CONTINUATION production_route=1 split_time=":
        "PASS WARM_NATIVE_ACTIVE_CHECKPOINT_CONTINUATION split_time=")
        <<2.*macro_dt<<" final_time="<<original.controller->t_current<<" final_step=3 leaves=5 cells=1280"
        <<" bit_words="<<split.size()<<" checkpoint_sha="<<checkpoint_sha<<" table_sha="<<table_sha
        <<" genuine_burn_thermal_hydro_self=1 mixed_CF=1 raw_checkpoints=1 physical_qualified=0 total_energy_qualified=0 CUDA_qualified=0\n";
}
int main(int argc,char** argv) {
 try {
    // Explicit public mode preserves the same genuine EMPTY checkpoint and M2 window.
    if(argc==4&&std::string(argv[2])=="--public-native-active") {
        const std::filesystem::path root(argv[1]);require(!std::filesystem::exists(root),"output root exists");
        warm_continuation(root,argv[3],arch::driver::GravityStage::Qualification::Production);return 0;
    }
    if(argc==4&&std::string(argv[2])=="--warm-native-active") {
        const std::filesystem::path root(argv[1]);require(!std::filesystem::exists(root),"output root exists");
        warm_continuation(root,argv[3]);return 0;
    }
    require(argc==2 || (argc==3 && std::string(argv[2])=="--initial-thermal-rejection"),
            "new local output root and optional --initial-thermal-rejection required");
    const bool initial_rejection_probe=argc==3;
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
        // The unchanged configured lower bound must reject this initial thermal
        // state. Native RZ does not heat conserved means to satisfy a floor.
        if (initial_rejection_probe) config.numerics.min_eint=100.;
        config.io.tmax=2*dt;
        config.io.out_dir=(root/(std::to_string(direction)+"-"+std::to_string(inner))).string();
        config.io.base_name="internal-rz";
        amr::AMRControl control(32,2);initialize(control,config,direction);
        RunState initial;SimulationController counters(config,initial);
        counters.repairs.reset(species.count());
        BCHandler boundary(config,rz);
        arch::driver::DriverRuntime runtime(control,boundary,config,species,counters);
        runtime.bind_native_rz_eos(eos);
        if(initial_rejection_probe) {
            const auto before=snapshot(control);
            const auto repairs=counters.repairs.values;
            bool rejected=false;
            try{runtime.initialize_topology();}
            catch(const std::exception& error){
                // This warm nonrotating fixture has specific e=25/4, below
                // the unchanged configured 100 bound. Real AMR restriction
                // may reject during halo construction before the final EOS
                // owner. Both are legitimate initial rejection; neither is
                // evidence that native cold transfer has been qualified.
                const std::string message=error.what();
                rejected=message.find("RZ native closure/EOS rejected")!=std::string::npos
                    ||message=="AMR restriction produced an inadmissible coarse-cell fluid state.";
                if(!rejected)throw;
            }
            require(rejected&&snapshot(control)==before&&counters.repairs.values==repairs
                &&counters.step_count==0&&counters.t_current==0.,
                "invalid initial RZ thermal state was heated, advanced or accepted");
            std::cout<<"PASS RZ_INITIAL_THERMAL_REJECTION direction="<<direction
                <<" inner="<<inner<<" min_eint="<<config.numerics.min_eint
                <<" repairs=0 raw_output=0\n";
            continue;
        }
        runtime.initialize_topology();
        advance(runtime,counters,hydro);
        const auto split=snapshot(control);
        const auto provenance=io::inspect_checkpoint_provenance(config,species,
            arch::dispatch::EosId::Ideal,false,"none",false);
        auto pressure=+[](const FluidVector&,const double*,const void*){return 5.;};
        auto temperature=+[](const FluidVector&,const double*,const void*){return 25./12.;};
        auto gamma=+[](const FluidVector&,const double*,const void*){return 1.4;};
        const auto plot_identity=fixture_plot_identity(config,species,GridMetrics::GeometrySemantics::AxisymmetricRz);
        arch::driver::DriverIO output(runtime,counters,provenance,plot_identity,pressure,temperature,gamma,nullptr);
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
        resumed.bind_native_rz_eos(eos);
        resumed.initialize_topology();
        advance(runtime,counters,hydro);advance(resumed,resumed_counters,hydro);
        require(snapshot(control)==snapshot(restored),"continued and restarted scheduler states differ");
        require(counters.t_current==2*dt&&resumed_counters.t_current==counters.t_current
            &&counters.step_count==2&&resumed_counters.step_count==2,"continued controller differs");
        require(counters.repairs.values==resumed_counters.repairs.values
            &&counters.repairs.values[0]==0.,
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
