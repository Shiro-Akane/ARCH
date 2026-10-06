// Exercise the real CPU DriverIO -> PlotIO -> HDF writer, without time advancement.
#include "amr/AMRControl.h"
#include "driver/DriverUtils.h"
#include "driver/io/DriverIO.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "core/files/FileFingerprint.h"
#include "io/hdf5/HDF5Writer.h"
#include <hdf5.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/vfs.h>
#include <sys/statvfs.h>
#include <linux/magic.h>

enum class Fault { None, Write, Flush, Close };
static Fault fault=Fault::None;
static int injected=0;
extern "C" herr_t __real_H5Dwrite(hid_t,hid_t,hid_t,hid_t,hid_t,const void*);
extern "C" herr_t __real_H5Fflush(hid_t,H5F_scope_t);
extern "C" herr_t __real_H5Fclose(hid_t);
extern "C" herr_t __wrap_H5Dwrite(hid_t a,hid_t b,hid_t c,hid_t d,hid_t e,const void* v) {
    if(fault==Fault::Write){fault=Fault::None;++injected;return -1;}
    return __real_H5Dwrite(a,b,c,d,e,v);
}
extern "C" herr_t __wrap_H5Fflush(hid_t a,H5F_scope_t b) {
    if(fault==Fault::Flush){fault=Fault::None;++injected;return -1;}
    return __real_H5Fflush(a,b);
}
extern "C" herr_t __wrap_H5Fclose(hid_t a) {
    if(fault==Fault::Close){fault=Fault::None;++injected;return -1;}
    return __real_H5Fclose(a);
}
static void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main(int argc,char** argv) {
 try {
    require(argc==2 || argc==3,"unique fixture directory [--enospc] required");
    const bool enospc = argc==3;
    require(!enospc || std::string(argv[2])=="--enospc","unknown fixture mode");
    const std::filesystem::path root(argv[1]);
    require(!std::filesystem::exists(root),"fixture directory must be new");
    std::filesystem::create_directories(root);
    SimConfig config;
    config.grid.dim=1;config.grid.nblockx1=1;config.grid.nblockx2=0;config.grid.nblockx3=0;
    config.grid.x1_min=0.;config.grid.x1_max=1.;
    config.grid.x2_min=0.;config.grid.x2_max=1.;config.grid.x3_min=0.;config.grid.x3_max=1.;
    config.grid.x1l_boundary_type="outflow";config.grid.x1r_boundary_type="outflow";
    config.amr.lrefinemin=0;config.amr.lrefinemax=0;
    config.io.out_dir=root.string();config.io.base_name="fixture";config.io.vars={};config.io.vars.rho=true;
    config.numerics.solver_name="HLLC";
    SpeciesManager species;species.add_species("fixture-gas",1.,1.,1.4,1.);
    amr::AMRControl control(4,1);control.tree->InitRootGrid(config,1);
    const double fractions[]={1.};
    for(int id:control.tree->GetActiveBlocks()){
        auto& state=control.pool->GetBlock(id).fluid_state;
        for(std::size_t cell=0;cell<state.rho.size();++cell){
            state.set(cell,FluidVector{1.,0.,0.,0.,2.5});
            state.set_species_from_buffer(cell,fractions);
        }
    }
    RunState start;start.plt_idx=17;
    SimulationController counters(config,start);
    BCHandler boundaries(config);
    arch::driver::DriverRuntime runtime(control,boundaries,config,species,counters);
    runtime.initialize_topology();
    io::CheckpointProvenance provenance;
    // Fixed valid closure witnesses: IO mechanics only, not an EOS accuracy oracle.
    auto pressure=+[](const FluidVector&,const double*,const void*){return 1.;};
    auto temperature=+[](const FluidVector&,const double*,const void*){return 2.5;};
    auto gamma=+[](const FluidVector&,const double*,const void*){return 1.4;};
    arch::driver::DriverIO output(runtime,counters,provenance,pressure,temperature,gamma,nullptr);
    auto target=[&](){
        std::ostringstream name;name<<"fixture_HLLC_plt_"<<std::setw(4)<<std::setfill('0')<<counters.plt_file_index<<".h5";
        return root/name.str();
    };
    // No active leaves cannot count as a completed publication.
    amr::AMRControl empty_control(4,1);
    arch::driver::DriverRuntime empty_runtime(empty_control,boundaries,config,species,counters);
    arch::driver::DriverIO empty_output(empty_runtime,counters,provenance,pressure,temperature,gamma,nullptr);
    const auto empty_directory=root/"empty-grid-output";
    config.io.out_dir=empty_directory.string();
    bool direct_empty_failed=false;
    try { write_plt(empty_control,pressure,temperature,gamma,nullptr,17,0.,config,species); }
    catch(const std::exception& error) {
        direct_empty_failed=std::string(error.what()).find("active leaf")!=std::string::npos;
    }
    require(direct_empty_failed,"direct writer accepted empty active grid");
    require(!std::filesystem::exists(empty_directory),"direct empty writer created output directory");
    bool empty_failed=false;
    try { empty_output.write_plot(); }
    catch(const std::exception& error) {
        std::cerr<<"empty-grid error: "<<error.what()<<'\n';
        empty_failed=std::string(error.what()).find("active leaf")!=std::string::npos;
    }
    require(empty_failed,"empty active grid publication did not fail explicitly");
    require(counters.plt_file_index==17,"empty grid consumed output identity");
    require(!std::filesystem::exists(empty_directory),"empty grid created output directory");
    config.io.out_dir=root.string();
    output.write_plot();
    require(counters.plt_file_index==18,"successful publication did not advance exactly once");
    const auto first=root/"fixture_HLLC_plt_0017.h5";
    const auto first_digest=arch::core::file_sha256(first.string());
    if (enospc) {
        // Only a caller-owned tiny tmpfs in a private mount namespace is eligible.
        // Never fill the project filesystem or substitute a fake HDF return code.
        struct statfs fs_info{};
        struct statvfs capacity{};
        require(::statfs(root.c_str(), &fs_info)==0 && fs_info.f_type==TMPFS_MAGIC,
                "ENOSPC fixture requires real tmpfs");
        require(::statvfs(root.c_str(), &capacity)==0, "tmpfs capacity unavailable");
        const auto total_bytes=static_cast<unsigned long long>(capacity.f_blocks)*capacity.f_frsize;
        require(total_bytes>0 && total_bytes<=8ULL*1024*1024,
                "ENOSPC fixture refuses filesystem larger than 8 MiB");
        const auto reservation=root/"space-reservation";
        const int fd=::open(reservation.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
        require(fd>=0,"cannot create controlled space reservation");
        std::array<char,65536> zero{};
        unsigned long long reserved=0;
        int space_errno=0;
        for (;;) {
            const auto written=::write(fd,zero.data(),zero.size());
            if(written<0){space_errno=errno;break;}
            if(written==0){space_errno=EIO;break;}
            reserved+=static_cast<unsigned long long>(written);
            require(reserved<=total_bytes,"reservation exceeded bounded filesystem");
        }
        require(::close(fd)==0,"reservation close failed");
        require(space_errno==ENOSPC,"kernel did not report actual ENOSPC");
        require(::statvfs(root.c_str(),&capacity)==0 && capacity.f_bavail==0,
                "controlled filesystem not actually full");
        const int before=counters.plt_file_index;
        const auto failed_path=target();
        bool propagated=false;
        std::ostringstream logs;
        auto* old=std::cout.rdbuf(logs.rdbuf());
        try { output.write_plot(); }
        catch(const std::exception& error) {
            propagated=true;
            std::cerr<<"actual ENOSPC Driver error: "<<error.what()<<'\n';
        }
        std::cout.rdbuf(old);
        require(propagated,"full filesystem write was accepted");
        require(counters.plt_file_index==before,"ENOSPC advanced publication index");
        require(!std::filesystem::exists(failed_path),"ENOSPC exposed final file");
        require(logs.str().find("Saved PLT")==std::string::npos,"ENOSPC logged success");
        require(arch::core::file_sha256(first.string())==first_digest,
                "ENOSPC changed previous successful publication");
        for(const auto& item:std::filesystem::directory_iterator(root))
            require(item.path().filename().string().find(".partial-")==std::string::npos,
                    "ENOSPC leaked partial file");
        require(std::filesystem::remove(reservation),"cannot release owned space reservation");
        output.write_plot();
        require(counters.plt_file_index==before+1 && std::filesystem::is_regular_file(failed_path),
                "ENOSPC retry did not publish same index exactly once");
        require(arch::core::file_sha256(first.string())==first_digest,
                "retry changed previous successful file");
        require(counters.step_count==0 && counters.t_current==0.,
                "ENOSPC IO-only fixture advanced simulation");
        std::cout<<"PASS actual_kernel_errno="<<space_errno
                 <<" tmpfs_bytes="<<total_bytes<<" reserved_bytes="<<reserved
                 <<" failed_index="<<before<<" retry_index="<<counters.plt_file_index
                 <<" original_sha256="<<first_digest<<" time=0 step=0\n";
        return 0;
    }
    for(Fault stage:{Fault::Write,Fault::Flush,Fault::Close}){
        const int before=counters.plt_file_index;const auto failed_path=target();
        fault=stage;injected=0;bool propagated=false;
        std::ostringstream logs;auto* old=std::cout.rdbuf(logs.rdbuf());
        try{output.write_plot();}catch(const std::exception&){propagated=true;}
        std::cout.rdbuf(old);
        require(propagated&&injected==1,"writer failure did not reach Driver caller");
        require(counters.plt_file_index==before,"failed publication advanced Driver plot index");
        require(!std::filesystem::exists(failed_path),"failed publication exposed a final file");
        require(logs.str().find("Saved PLT")==std::string::npos,"failed publication logged success");
        require(arch::core::file_sha256(first.string())==first_digest,"failure changed earlier output");
        output.write_plot();
        require(counters.plt_file_index==before+1&&std::filesystem::is_regular_file(failed_path),
                "retry did not publish the same index exactly once");
    }
    // Rename failure after successful HDF close must retain the reserved index too.
    const auto collision=target();std::filesystem::create_directory(collision);
    std::ofstream(collision/"keep")<<"existing";
    const int before=counters.plt_file_index;bool propagated=false;
    try{output.write_plot();}catch(const std::exception&){propagated=true;}
    require(propagated&&counters.plt_file_index==before,"rename failure advanced/swallowed");
    require(std::filesystem::exists(collision/"keep"),"rename failure replaced existing directory");
    std::filesystem::rename(collision,root/"preserved-collision");
    output.write_plot();
    require(counters.plt_file_index==before+1&&std::filesystem::is_regular_file(collision),
            "rename failure retry did not retain output identity");
    const int before_create=counters.plt_file_index;
    const auto blocker=root/"blocked-parent";std::ofstream(blocker)<<"keep";
    config.io.out_dir=blocker.string();propagated=false;
    try{output.write_plot();}catch(const std::exception&){propagated=true;}
    require(propagated&&counters.plt_file_index==before_create,"create failure advanced/swallowed");
    require(std::filesystem::is_regular_file(blocker),"create failure changed blocking file");
    config.io.out_dir=root.string();const auto create_retry=target();
    output.write_plot();
    require(counters.plt_file_index==before_create+1&&std::filesystem::is_regular_file(create_retry),
            "create failure retry did not retain output identity");
    for(const auto& item:std::filesystem::directory_iterator(root))
        require(item.path().filename().string().find(".partial-")==std::string::npos,"failed temporary leaked");
    require(counters.step_count==0&&counters.t_current==0.,"IO fixture advanced simulation");
    std::cout<<"PASS empty-grid direct/Driver rejection; real Driver write/flush/close/rename/create propagation; unchanged failure index; same-index retries; time=0 step=0\n";
    return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
