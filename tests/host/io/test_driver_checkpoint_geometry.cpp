// Actual CPU DriverIO checkpoint path; no timestep advancement.
#include "amr/AMRControl.h"
#include "driver/DriverUtils.h"
#include "driver/io/DriverIO.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "io/chk/CheckpointCompatibility.h"
#include "core/files/FileFingerprint.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <stdexcept>
static void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main(int argc,char** argv) {
 try {
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
        require(payload.geometry_identity.revision==1 &&
                payload.geometry_identity.chart==(rz?"axisymmetric-rz":"existing"),
                "Driver checkpoint did not use immutable Runtime profile");
        amr::AMRControl restored(4,config.grid.dim);RunState state;
        read_chk(path.string(),restored,state,config,species,provenance,
                 {1,rz?"axisymmetric-rz":"existing"});
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
                    report.find("momentum_phi=3.75\n")!=std::string::npos,
                    "RZ repair report mislabeled component slots");
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
        std::cout<<"PASS actual DriverIO "<<(rz?"RZ":"Cartesian")<<" profile/native/controller/rejection/create/retry time=0 step=0\n";
    }
    return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
