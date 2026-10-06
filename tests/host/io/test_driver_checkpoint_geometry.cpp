// Actual CPU DriverIO checkpoint path; no timestep advancement.
#include "amr/AMRControl.h"
#include "driver/DriverUtils.h"
#include "driver/io/DriverIO.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "io/chk/CheckpointCompatibility.h"
#include "core/files/FileFingerprint.h"
#include "amr/refinement/RefinementThermodynamics.h"
#include "physics/eos/IdealGas.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <stdexcept>
static void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
// Real Runtime transaction gate, not an evolved trajectory acceptance.
static void jeans_runtime_gate() {
    SimConfig config;
    config.grid.dim=1;config.grid.nblockx1=1;
    config.grid.nblockx2=0;config.grid.nblockx3=0;config.grid.x1_max=8.;
    config.amr.lrefinemin=0;config.amr.lrefinemax=2;
    config.amr.refine_on_rho=false;config.amr.refine_on_jeans=true;
    config.amr.jeans_cells=8.;
    SpeciesManager species;IdealGas eos(1.5,species);
    amr::AMRControl control(16,1);control.tree->InitRootGrid(config,0);
    for(int id:control.tree->GetActiveBlocks()) {
        auto& block=control.pool->GetBlock(id);
        for(int cell=0;cell<block.grid.GetTotalSize();++cell)
            block.fluid_state.set(cell,{1e7,0.,0.,0.,1e7*4./3.});
    }
    amr::BindRefinementThermodynamics(*control.tree,eos);
    RunState start;SimulationController counters(config,start);
    BCHandler boundaries(config);
    arch::driver::DriverRuntime runtime(control,boundaries,config,species,counters);
    runtime.initialize_topology();
    runtime.ensure_jeans_resolution(0,0.);
    require(runtime.handles().size()==2,"Runtime JENS failed initial repair");
    require(counters.step_count==0&&counters.t_current==0.,"JENS repair advanced simulation");
    // Publish a new accepted state, rather than changing a cached diagnostic.
    // This fixture does not claim to evolve a scientific trajectory.
    for(int id:control.tree->GetActiveBlocks()) {
        auto& block=control.pool->GetBlock(id);
        for(int cell=0;cell<block.grid.GetTotalSize();++cell)
            block.fluid_state.set(cell,{1e7,0.,0.,0.,1e7/3.});
    }
    auto context=runtime.stage_context();
    (void)arch::scheduler::publish_completed_interior(
        context,runtime.handles(),arch::state::StateSlot::Current);
    runtime.ensure_jeans_resolution(1,.25);
    require(runtime.handles().size()==4,"new accepted state used stale JENS resolution");
    const auto handles=runtime.handles();
    runtime.ensure_jeans_resolution(1,.25);
    require(runtime.handles()==handles,"resolved accepted-state repair churned identity");
    config.amr.jeans_cells=32.;
    bool failed=false;
    try {runtime.ensure_jeans_resolution(1,.25);}catch(const std::runtime_error&){failed=true;}
    require(failed&&runtime.handles()==handles&&control.pool->GetNumActiveBlocks()==4,
            "failed Runtime repair published partial identity");
    config.amr.refine_on_jeans=false;
    const auto records=runtime.regrid_records().size();
    runtime.ensure_jeans_resolution(2,.5);
    require(runtime.regrid_records().size()==records,"disabled JENS executed a transaction");
    std::cout<<"JEANS_RUNTIME_TRANSACTION_PASS\n";
}
int main(int argc,char** argv) {
 try {
    jeans_runtime_gate();
    require(argc==2,"new persistent output directory required");
    const std::filesystem::path root(argv[1]);
    require(!std::filesystem::exists(root),"output directory must be new");
    for (bool rz : {false,true}) {
        SimConfig config;
        config.grid.dim=rz?2:1;config.grid.geometry=rz?"cylindrical":"cartesian";
        config.grid.nblockx1=1;config.grid.nblockx2=rz?1:0;config.grid.nblockx3=0;
        config.grid.x1_min=0.;config.grid.x1_max=1.;
        config.grid.x2_min=rz?-4.:0.;config.grid.x2_max=rz?4.:1.;
        config.grid.x1l_boundary_type="outflow";config.grid.x1r_boundary_type="outflow";
        config.grid.x2l_boundary_type="outflow";config.grid.x2r_boundary_type="outflow";
        config.amr.lrefinemin=0;config.amr.lrefinemax=0;
        config.io.out_dir=(root/(rz?"rz":"cartesian")).string();config.io.base_name="fixture";
        SpeciesManager species;species.add_species("fixture-gas",1.,1.,1.4,1.);
        const auto semantics=rz?GridMetrics::GeometrySemantics::AxisymmetricRz:
                                 GridMetrics::GeometrySemantics::Existing;
        amr::AMRControl control(4,config.grid.dim);control.tree->InitRootGrid(config,1,semantics);
        const double fractions[]={1.};
        for(int id:control.tree->GetActiveBlocks()){
            auto& f=control.pool->GetBlock(id).fluid_state;
            for(std::size_t c=0;c<f.rho.size();++c){
                f.set(c,FluidVector{2.,0.,0.,0.,5.});
                f.set_species_from_buffer(c,fractions);f.enuc_rate[c]=-.25;
            }
        }
        RunState start;start.chk_idx=7;start.plt_idx=11;
        SimulationController counters(config,start);
        counters.repairs.reset(species.count());
        BCHandler boundaries(config,semantics);
        arch::driver::DriverRuntime runtime(control,boundaries,config,species,counters);
        runtime.initialize_topology();
        const auto provenance=io::inspect_checkpoint_provenance(config,species,
            arch::dispatch::EosId::Ideal,false,"none",false);
        auto pressure=+[](const FluidVector&,const double*,const void*){return 2.;};
        auto temperature=+[](const FluidVector&,const double*,const void*){return 2.5;};
        auto gamma=+[](const FluidVector&,const double*,const void*){return 1.4;};
        arch::driver::DriverIO output(runtime,counters,provenance,pressure,temperature,gamma,nullptr);
        // A real serializer rejection must propagate without consuming an index.
        // Keep the scientific state valid; corrupt only the repair-ledger shape.
        const auto ledger=counters.repairs.values;
        counters.repairs.values.pop_back();
        bool propagated=false;
        try { output.write_checkpoint(.02,true); }
        catch(const std::exception&) { propagated=true; }
        counters.repairs.values=ledger;
        require(propagated,"checkpoint serializer failure swallowed");
        require(counters.chk_file_index==7,"failed checkpoint consumed Driver index");
        require(!std::filesystem::exists(std::filesystem::path(config.io.out_dir)/"fixture_chk_0007.h5"),
                "rejected checkpoint produced a scientific output file");
        output.write_checkpoint(.02,true);
        require(counters.chk_file_index==8,"checkpoint success did not advance once");
        require(counters.plt_file_index==11&&counters.step_count==0&&counters.t_current==0.,
                "checkpoint changed simulation/controller");
        const auto path=std::filesystem::path(config.io.out_dir)/"fixture_chk_0007.h5";
        const auto original_digest=arch::core::file_sha256(path.string());
        const auto original_directory=config.io.out_dir;
        const auto blocker=std::filesystem::path(config.io.out_dir)/"blocked-parent";
        std::ofstream(blocker)<<"keep";
        config.io.out_dir=blocker.string();
        propagated=false;
        try { output.write_checkpoint(.02,true); }
        catch(const std::exception&) { propagated=true; }
        require(propagated&&counters.chk_file_index==8,
                "create failure swallowed or consumed checkpoint index");
        require(std::filesystem::is_regular_file(blocker),
                "create failure modified blocking file");
        require(arch::core::file_sha256(path.string())==original_digest,
                "create failure changed previous checkpoint");
        config.io.out_dir=original_directory;
        output.write_checkpoint(.02,true);
        require(counters.chk_file_index==9 &&
                std::filesystem::is_regular_file(std::filesystem::path(config.io.out_dir)/"fixture_chk_0008.h5"),
                "create failure retry did not use the same index");
        const auto payload=io::read_hdf5_chk_impl(path.string());
        require(payload.geometry_identity.revision==(rz?io::rz_checkpoint_revision:1) &&
                payload.geometry_identity.chart==(rz?"axisymmetric-rz":"existing"),
                "Driver checkpoint did not use immutable Runtime profile");
        amr::AMRControl restored(4,config.grid.dim);RunState state;
        read_chk(path.string(),restored,state,config,species,provenance,
                 (rz?io::current_rz_checkpoint_geometry():io::CheckpointGeometryIdentity{1,"existing"}));
        require(payload.rho.size()==(rz?amr::BLOCK_NX*amr::BLOCK_NY:amr::BLOCK_NX),
                "unexpected native storage shape");
        for(int id:restored.tree->GetActiveBlocks()){
            const auto& b=restored.pool->GetBlock(id);
            for(int j=b.grid.Js();j<b.grid.Je();++j)
                for(int i=b.grid.Is();i<b.grid.Ie();++i){
                    const int c=b.grid.GetIndex(i,j,b.grid.Ks());
                    require(b.fluid_state.rho[c]==2.&&b.fluid_state.eng[c]==5.&&
                            b.fluid_state.mom_u[c]==0.&&b.fluid_state.mom_v[c]==0.&&
                            b.fluid_state.mom_w[c]==0.&&b.fluid_state.enuc_rate[c]==-.25&&
                            b.fluid_state.X(0,c)==1.,"Driver checkpoint changed raw state");
                }
        }
        require(state.time==0.&&state.step==0&&state.chk_idx==7&&state.plt_idx==11&&
                state.dt_burn==.02&&state.resume_after_regrid,"checkpoint controller changed");
        // Distinct ledger slots prove output labels, not a zero-only smoke test.
        counters.repairs.values[4]=1.25;
        counters.repairs.values[5]=-2.5;
        counters.repairs.values[6]=3.75;
        output.write_measurements({}, {});
        const auto report_path=std::filesystem::path(config.io.out_dir)/"state_repairs.txt";
        const auto read_report=[&] {
            std::ifstream in(report_path);
            return std::string(std::istreambuf_iterator<char>(in),{});
        };
        const auto report=read_report();
        if (rz) {
            require(report.find("geometry_chart=axisymmetric-rz\n")!=std::string::npos,
                    "RZ repair report omitted explicit chart");
            require(report.find("measure_normalization=full_rotation\n")!=std::string::npos,
                    "RZ repair report omitted full-ring measure");
            require(report.find("momentum_r=1.25\n")!=std::string::npos &&
                    report.find("momentum_z=-2.5\n")!=std::string::npos &&
                    report.find("angular_momentum_signed=3.75\n")!=std::string::npos,
                    "RZ repair report mislabeled component slots");
            require(report.find("repair_semantics=rz-native-V-angular-J-v1\n")!=std::string::npos
                &&report.find("angular_momentum_unit=g*cm^2/s\n")!=std::string::npos
                &&report.find("momentum_phi=")==std::string::npos,
                "RZ report lacks authoritative J identity/units");
            require(report.find("momentum_x=")==std::string::npos &&
                    report.find("momentum_y=")==std::string::npos,
                    "RZ repair report retained Cartesian component aliases");
        } else {
            require(report.starts_with("revision=P1.5-v1 units=CGS\n"),
                    "legacy repair report header changed");
            require(report.find("momentum_x=1.25\n")!=std::string::npos &&
                    report.find("momentum_y=-2.5\n")!=std::string::npos &&
                    report.find("momentum_z=3.75\n")!=std::string::npos,
                    "legacy repair component labels changed");
            require(report.find("geometry_chart=")==std::string::npos,
                    "legacy repair report unexpectedly migrated");
        }
        // Linux /dev/full makes the real buffered report flush fail.
        std::filesystem::remove(report_path);
        std::filesystem::create_symlink("/dev/full",report_path);
        propagated=false;
        try { output.write_measurements({}, {}); }
        catch (const std::exception&) { propagated=true; }
        require(propagated,"repair report write failure swallowed");
        std::filesystem::remove(report_path);
        output.write_measurements({}, {});
        require(read_report()==report,"repair report failure recovery changed ledger serialization");
        require(arch::core::file_sha256(path.string())==original_digest,
                "measurement failure altered checkpoint");
        bool all_timing_failures_propagated=true;
        for (const char* name : {"run_timings.tsv","cpu_stage_timings.tsv"}) {
            const auto timing_path=std::filesystem::path(config.io.out_dir)/name;
            std::filesystem::remove(timing_path);
            std::filesystem::create_symlink("/dev/full",timing_path);
            bool rejected=false;
            try { output.write_measurements({}, {}); }
            catch (const std::exception& error) {
                rejected=std::string(error.what()).find(
                    std::string(name)=="run_timings.tsv" ? "run timings" : "CPU stage timings")
                    !=std::string::npos;
            }
            std::cout<<"TIMING_FAILURE chart="<<(rz?"RZ":"Cartesian")
                     <<" file="<<name<<" propagated="<<rejected<<"\n";
            all_timing_failures_propagated &= rejected;
            std::filesystem::remove(timing_path);
            output.write_measurements({}, {});
            require(std::filesystem::file_size(timing_path)>0,
                    "timing report recovery produced no data");
            require(read_report()==report,
                    "timing failure recovery changed repair ledger serialization");
            require(arch::core::file_sha256(path.string())==original_digest,
                    "timing report failure changed previous checkpoint");
        }
        require(all_timing_failures_propagated,"timing buffered write failure swallowed");
        // Analytical nonzero velocity field probes the writer's chart,
        // component basis and shared diagnostics (not an evolved model).
        config.io.vars.rho=true;config.io.vars.u=true;
        config.io.vars.v=true;config.io.vars.w=true;
        config.io.vars.vort=true;config.io.vars.divv=true;
        config.io.vars.eng=true;
        for (int id : control.tree->GetActiveBlocks()) {
            auto& b=control.pool->GetBlock(id);
            const int nx=b.grid.stride_y, ny=b.grid.stride_z/nx;
            const int nz=b.grid.total_size/b.grid.stride_z;
            for (int k=0;k<nz;++k)
                for (int j=0;j<ny;++j)
                    for (int i=0;i<nx;++i) {
                        const auto pos=b.grid.GetPhysicalCoords(i,j,k,semantics);
                        const double u=rz?.1*pos.r_cy:0.;
                        const double v=rz?.2*pos.z_cy:0.;
                        const double w=rz?.3*pos.r_cy:0.;
                        b.fluid_state.set(b.grid.GetIndex(i,j,k),FluidVector{2.,2.*u,2.*v,2.*w,100.});
                    }
        }
        output.write_plot();
        require(counters.plt_file_index==12,"plot success did not advance exactly once");
        const auto plots_before=std::distance(std::filesystem::directory_iterator(config.io.out_dir),
                                             std::filesystem::directory_iterator{});
        bool profile_rejected=false;
        try {
            write_plt(control,pressure,temperature,gamma,nullptr,11,0.,config,species,
                      {},&provenance,{},static_cast<GridMetrics::GeometrySemantics>(99));
        } catch (const std::invalid_argument&) { profile_rejected=true; }
        require(profile_rejected,"unknown writer chart accepted");
        require(plots_before==std::distance(std::filesystem::directory_iterator(config.io.out_dir),
                                            std::filesystem::directory_iterator{}),
                "rejected chart created a new file");
        // Only-output consumer uses actual EOS closure and leaves tree/state
        // unchanged. No runtime JENS capability is advertised by this fixture.
        config.io.vars.jens=true;config.physics.gravity.type="self";
        IdealGas plot_eos(1.4,species);
        auto actual_pressure=+[](const FluidVector& u,const double* x,const void* eos) {
            return static_cast<const IdealGas*>(eos)->get_pressure(u,x);
        };
        auto actual_temperature=+[](const FluidVector& u,const double* x,const void* eos) {
            return static_cast<const IdealGas*>(eos)->get_temperature(u.rho,arch::state::recover(u).internal,x);
        };
        auto actual_gamma=+[](const FluidVector& u,const double* x,const void* eos) {
            const auto& gas=*static_cast<const IdealGas*>(eos);
            const double p=gas.get_pressure(u,x),cs=gas.get_sound_speed(u,p,x);
            return u.rho*cs*cs/p;
        };
        const auto leaves=control.tree->GetActiveBlocks();
        write_plt(control,actual_pressure,actual_temperature,actual_gamma,&plot_eos,
                  12,0.,config,species,{},&provenance,{},semantics);
        require(control.tree->GetActiveBlocks()==leaves,"JENS output changed AMR topology");
        const auto good_plot=std::filesystem::path(config.io.out_dir)/"fixture_SW_plt_0012.h5";
        const auto plot_digest=arch::core::file_sha256(good_plot.string());
        auto bad_gamma=+[](const FluidVector&,const double*,const void*) {
            return std::numeric_limits<double>::quiet_NaN();
        };
        bool diagnostic_rejected=false;
        try {
            write_plt(control,actual_pressure,actual_temperature,bad_gamma,&plot_eos,
                      13,0.,config,species,{},&provenance,{},semantics);
        } catch(const std::runtime_error&) {diagnostic_rejected=true;}
        require(diagnostic_rejected&&!std::filesystem::exists(
            std::filesystem::path(config.io.out_dir)/"fixture_SW_plt_0013.h5"),
            "invalid JENS diagnostic published output");
        config.physics.gravity.type="none";diagnostic_rejected=false;
        try {
            write_plt(control,actual_pressure,actual_temperature,actual_gamma,&plot_eos,
                      13,0.,config,species,{},&provenance,{},semantics);
        } catch(const std::invalid_argument&) {diagnostic_rejected=true;}
        require(diagnostic_rejected&&arch::core::file_sha256(good_plot.string())==plot_digest,
            "inapplicable JENS request damaged last successful output");
        config.physics.gravity.type="self";
        std::cout<<"JEANS_NATIVE_PLOT_PASS chart="<<(rz?"RZ":"Cartesian")<<"\n";
        std::cout<<"PASS actual DriverIO "<<(rz?"RZ":"Cartesian")<<" profile/native/controller/rejection/create/retry time=0 step=0\n";
    }
    return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
