// Actual CPU DriverIO checkpoint path; no timestep advancement.
#include "amr/AMRControl.h"
#include "driver/DriverUtils.h"
#include "driver/io/DriverIO.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "io/chk/CheckpointCompatibility.h"
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
        config.grid.x2_min=0.;config.grid.x2_max=1.;
        config.grid.x1l_boundary_type="outflow";config.grid.x1r_boundary_type="outflow";
        config.grid.x2l_boundary_type="outflow";config.grid.x2r_boundary_type="outflow";
        config.amr.lrefinemin=0;config.amr.lrefinemax=0;
        config.io.out_dir=(root/(rz?"rz":"cartesian")).string();config.io.base_name="fixture";
        SpeciesManager species;species.add_species("fixture-gas",1.,1.,1.4,1.);
        amr::AMRControl control(4,config.grid.dim);control.tree->InitRootGrid(config,1);
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
        const auto semantics=rz?GridMetrics::GeometrySemantics::AxisymmetricRz:
                                 GridMetrics::GeometrySemantics::Existing;
        BCHandler boundaries(config,semantics);
        arch::driver::DriverRuntime runtime(control,boundaries,config,species,counters);
        runtime.initialize_topology();
        const auto provenance=io::inspect_checkpoint_provenance(config,species,
            arch::dispatch::EosId::Ideal,false,"none",false);
        auto pressure=+[](const FluidVector&,const double*,const void*){return 2.;};
        auto temperature=+[](const FluidVector&,const double*,const void*){return 2.5;};
        auto gamma=+[](const FluidVector&,const double*,const void*){return 1.4;};
        arch::driver::DriverIO output(runtime,counters,provenance,pressure,temperature,gamma,nullptr);
        output.write_checkpoint(.02,true);
        require(counters.chk_file_index==8,"checkpoint success did not advance once");
        require(counters.plt_file_index==11&&counters.step_count==0&&counters.t_current==0.,
                "checkpoint changed simulation/controller");
        const auto path=std::filesystem::path(config.io.out_dir)/"fixture_chk_0007.h5";
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
        std::cout<<"PASS actual DriverIO "<<(rz?"RZ":"Cartesian")<<" profile/native/controller time=0 step=0\n";
    }
    return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
