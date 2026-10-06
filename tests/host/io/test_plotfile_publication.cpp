#include "core/files/FileFingerprint.h"
#include "core/files/RunIdentity.h"
#include <regex>
#include "io/hdf5/HDF5Writer.h"
#include "io/plot/PlotGridMetadata.h"
#include "io/plot/PlotFieldMetadata.h"
#include <highfive/H5File.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <sstream>
#ifdef ARCH_PLOT_FAILURE_TEST
// Linker wrapping belongs solely to this Linux test executable.
enum class Fault { None, Write, Flush, Close, PauseFlush };
static Fault fault=Fault::None;
static int injected=0;
extern "C" herr_t __real_H5Dwrite(hid_t,hid_t,hid_t,hid_t,hid_t,const void*);
extern "C" herr_t __real_H5Fflush(hid_t,H5F_scope_t);
extern "C" herr_t __real_H5Fclose(hid_t);
extern "C" herr_t __wrap_H5Dwrite(hid_t a,hid_t b,hid_t c,hid_t d,hid_t e,const void* data) {
    if(fault==Fault::Write){fault=Fault::None;++injected;return -1;}
    return __real_H5Dwrite(a,b,c,d,e,data);
}
extern "C" herr_t __wrap_H5Fflush(hid_t a,H5F_scope_t b) {
    if(fault==Fault::PauseFlush) {
        fault=Fault::None;
        std::cout<<"PAUSED before-flush"<<std::endl;
        std::string resume;std::getline(std::cin,resume);
    }
    if(fault==Fault::Flush){fault=Fault::None;++injected;return -1;}
    return __real_H5Fflush(a,b);
}
extern "C" herr_t __wrap_H5Fclose(hid_t a) {
    if(fault==Fault::Close){fault=Fault::None;++injected;return -1;}
    return __real_H5Fclose(a);
}
#endif
void require(bool b, const char* m) { if (!b) throw std::runtime_error(m); }
int main(int argc, char** argv) {
 try {
#ifdef ARCH_PLOT_FAILURE_TEST
    if(argc==3 && std::string(argv[1])=="--publication-pause") {
        fault=Fault::PauseFlush;
        io::write_hdf5_plt_impl(argv[2],0,1,"cartesian",{1,2},
            {.25,.75},{0,0},{0,0},{0},{0},{{"DENS",{9.,10.}}});
        return 0;
    }
#endif
    if(argc==2 && std::string(argv[1])=="--fingerprint-pause") {
        const auto expected=arch::core::running_executable_sha256();
        std::cout<<"READY "<<expected<<std::endl;
        std::string resume;std::getline(std::cin,resume);
        require(arch::core::file_sha256("/proc/self/exe")==expected,"running inode changed with launch path");
        require(arch::core::running_executable_sha256()==expected,"cached running digest changed");
        std::cout<<"PASS "<<expected<<std::endl;
        return 0;
    }
    require(argc == 2, "fixture directory required");
    std::filesystem::path root(argv[1]);
    std::filesystem::create_directories(root);
    // Non-dyadic spacing must give the same stored face to adjacent rows.
    // Compare independently emitted cells, not a rounded width tolerance.
    {
        Grid grid;
        grid.dim=2; grid.ng=2;
        grid.x1_min=0.; grid.x1_max=3.2;
        grid.x2_min=0.; grid.x2_max=3.2;
        grid.InitializeTopology();
        for (int j=grid.Js();j+1<grid.Je();++j) {
            io::PlotNativeGrid left,right;
            io::append_plot_native_cell(left,grid,grid.Is(),j,0);
            io::append_plot_native_cell(right,grid,grid.Is(),j+1,0);
            require(left.upper[1].back()==right.lower[1].back(),
                    "adjacent native y faces disagree");
        }
    }
    // Internal RZ candidate must expose W and the single angular state. Invalid
    // output views are rejected before replacing an already published file.
    {
        const auto profile=GridMetrics::GeometrySemantics::AxisymmetricRz;
        Grid grid;grid.dim=2;grid.ng=2;grid.geometry="cylindrical";
        grid.x1_min=0.;grid.x1_max=1.;grid.x2_min=-1.;grid.x2_max=1.;
        grid.InitializeTopology();
        io::PlotNativeGrid native;io::PlotRzAngularState angular;
        native.logical[0]={0};native.logical[1]={0};native.logical[2]={0};
        std::vector<double> x,y,z,density;
        for(int j=grid.Js();j<grid.Je();++j)
        for(int i=grid.Is();i<grid.Ie();++i) {
            io::append_plot_native_cell(native,grid,i,j,grid.Ks(),profile);
            const auto pos=grid.GetPhysicalCoords(i,j,grid.Ks(),profile);
            x.push_back(pos.x);y.push_back(pos.y);z.push_back(pos.z);density.push_back(2.);
            angular.m_phi.push_back(1.);
            angular.angular_momentum_density.push_back(
                native.angular_measure.back()/native.cell_measure.back());
        }
        const std::vector<size_t> dims{1,amr::BLOCK_NY,amr::BLOCK_NX};
        const auto path=(root/"native-rz-angular.h5").string();
        const auto write=[&](const io::PlotNativeGrid* n,const io::PlotRzAngularState* s,
                             GridMetrics::GeometrySemantics semantics=GridMetrics::GeometrySemantics::AxisymmetricRz) {
            io::write_hdf5_plt_impl(path,0,2,
                semantics==profile ? "cylindrical" : "cartesian",dims,x,y,z,{0},{0},
                {{"DENS",density}},n,nullptr,nullptr,semantics,s);
        };
        write(&native,&angular);
        {
            HighFive::File f(path,HighFive::File::ReadOnly);std::string tag;
            f.getAttribute("state_semantics").read(tag);
            require(tag=="rz-m-phi-j-over-w-v1","RZ angular identity absent");
            std::vector<double> raw(density.size());
            f.getDataSet("NativeState/m_phi").read(raw.data());
            require(raw==angular.m_phi,"m_phi output changed raw state");
            f.getDataSet("NativeState/angular_momentum_density").read(raw.data());
            require(raw==angular.angular_momentum_density,"J/V output changed");
            f.getDataSet("NativeGrid/angular_measure").read(raw);
            require(raw==native.angular_measure,"W output changed");
        }
        const auto digest=arch::core::file_sha256(path);
        for (int mutation=0;mutation<10;++mutation) {
            auto n=native;auto s=angular;
            const io::PlotNativeGrid* np=&n;const io::PlotRzAngularState* sp=&s;
            if(mutation==0) np=nullptr;
            if(mutation==1) sp=nullptr;
            if(mutation==2) n.angular_measure.pop_back();
            if(mutation==3) n.angular_measure[0]=0.;
            if(mutation==4) s.m_phi[0]=std::numeric_limits<double>::quiet_NaN();
            if(mutation==5) s.angular_momentum_density[0]=std::numeric_limits<double>::infinity();
            if(mutation==6) s.m_phi.pop_back();
            if(mutation==7) s.angular_momentum_density[0]+=1.;
            if(mutation==8) s.angular_momentum_density.pop_back();
            bool rejected=false;
            try { write(np,sp,mutation==9 ? GridMetrics::GeometrySemantics::Existing : profile); }
            catch(const std::invalid_argument&){rejected=true;}
            require(rejected,"invalid RZ angular payload accepted");
            require(arch::core::file_sha256(path)==digest,"rejected RZ payload replaced file");
        }
    }
    for (int dimension : {1,2}) {
        io::PlotNativeGrid native;
        std::vector<double> cx,cy,cz,field;
        const size_t nx=amr::BLOCK_NX, ny=dimension==2 ? amr::BLOCK_NY : 1;
        for (int block=0;block<2;++block) {
            Grid grid;
            grid.dim=dimension; grid.ng=2;
            grid.x1_min=double(block); grid.x1_max=double(block+1);
            grid.x2_min=2.; grid.x2_max=4.; grid.x3_min=0.; grid.x3_max=0.;
            grid.InitializeTopology();
            native.logical[0].push_back(block);
            native.logical[1].push_back(0); native.logical[2].push_back(0);
            for(int k=grid.Ks();k<grid.Ke();++k)
            for(int j=grid.Js();j<grid.Je();++j)
            for(int i=grid.Is();i<grid.Ie();++i) {
                io::append_plot_native_cell(native,grid,i,j,k);
                const auto pos=grid.GetPhysicalCoords(i,j,k);
                cx.push_back(pos.x); cy.push_back(pos.y); cz.push_back(pos.z);
                const size_t index=field.size(); field.push_back(double(index)+.25);
                const double expected_lower=block+double(i-grid.ng)/nx;
                require(native.lower[0].back()==expected_lower,"ghost offset/bounds mismatch");
                require(native.upper[0].back()==expected_lower+1./nx,"upper bound mismatch");
                const double expected_measure=dimension==1 ? 1./nx : 2./(nx*ny);
                require(native.cell_measure.back()==expected_measure,"independent Cartesian measure mismatch");
                require(pos.x==(native.lower[0].back()+native.upper[0].back())*.5,
                        "Cartesian center/bounds mismatch");
            }
            grid.geometry="cylindrical";
            require(!io::supports_plot_native_grid(grid),"curved support falsely claimed");
        }
        std::vector<size_t> native_dims{2};
        if(dimension==2) native_dims.push_back(ny);
        native_dims.push_back(nx);
        auto path=root/("native-"+std::to_string(dimension)+".h5");
        io::PlotSourceIdentity identity;
        identity.run_id=arch::core::new_run_identity();
        require(std::regex_match(identity.run_id,std::regex(
            "[a-f0-9]{8}-[a-f0-9]{4}-4[a-f0-9]{3}-[89ab][a-f0-9]{3}-[a-f0-9]{12}")),
            "invalid OS-generated run UUID");
        require(identity.run_id!=arch::core::new_run_identity(),"run instances reused UUID");
        identity.binary_sha256=arch::core::running_executable_sha256();
        identity.raw_config_sha256="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
        identity.case_id="Sod"; identity.eos_type="ideal"; identity.ideal_gamma=1.4;
        identity.species_names={"test-species"}; identity.unit_system="cgs";
        identity.species_A={12.};identity.species_Z={6.};
        identity.species_gamma={1.4};identity.species_Cv={3.};
        const std::map<std::string,io::PlotFieldMetadata> declarations{
            {"DENS",io::plot_field_metadata("DENS",true)},
            {"ENTR",io::plot_field_metadata("ENTR",true)},
            {"species",io::plot_species_metadata()}};

        io::write_hdf5_plt_impl(path.string(),0,dimension,"cartesian",native_dims,
            cx,cy,cz,{0,0},{1,2},{{"DENS",field},{"ENTR",field},{"species",field},{"unregistered",field}},&native,&identity,&declarations);
        {
            HighFive::File f(path.string(),HighFive::File::ReadOnly);
            std::string id;
            f.getAttribute("plot_identity_state").read(id);
            require(id=="unknown","partial evidence falsely certified");
            f.getGroup("SourceIdentity").getAttribute("case_id").read(id);
            require(id=="Sod","case source evidence missing");
            for(const auto& [name,expected]:std::map<std::string,std::string>{
                {"effective_config_sha256","authoritative effective-config identity not supplied to writer"},
                {"build_id","authoritative Build Manifest identity not supplied to writer"},
                {"source_git_head","authoritative source Git identity not supplied to writer"}}) {
                f.getGroup("SourceIdentity").getAttribute(name).read(id);
                require(id=="unknown","unsupported identity falsely certified");
                f.getGroup("SourceIdentity").getAttribute(name+"_reason").read(id);
                require(id==expected,"unknown identity reason missing or changed");
            }
            f.getGroup("SourceIdentity").getAttribute("run_id").read(id);
            require(id==identity.run_id,"run output identity lost");
            f.getGroup("SourceIdentity").getAttribute("run_id_source").read(id);
            require(id=="DriverIO output session; OS-generated UUIDv4","run source guessed");
            f.getGroup("SourceIdentity").getAttribute("binary_sha256").read(id);
            require(id==identity.binary_sha256,"running binary identity differs");
            f.getGroup("SourceIdentity").getAttribute("raw_config_sha256").read(id);
            require(id==identity.raw_config_sha256,"captured raw digest changed");
            double gamma=0.;
            f.getGroup("SourceIdentity").getAttribute("ideal_gamma").read(gamma);
            require(gamma==1.4,"resolved gamma changed");
            std::vector<std::string> species;
            f.getDataSet("SourceIdentity/species_names").read(species);
            require(species==identity.species_names,"species ordering changed");
            for(const auto& [name,expected]:std::map<std::string,std::vector<double>>{
                {"species_A",identity.species_A},{"species_Z",identity.species_Z},
                {"species_gamma",identity.species_gamma},{"species_Cv",identity.species_Cv}}) {
                std::vector<double> actual;f.getDataSet("SourceIdentity/"+name).read(actual);
                require(actual==expected,"resolved species properties changed");
            }
            f.getGroup("SourceIdentity").getAttribute("species_properties_state").read(id);
            require(id=="recorded","species properties state missing");
            f.getGroup("SourceIdentity").getAttribute("species_properties_source").read(id);
            require(id=="resolved-runtime-checkpoint-provenance","species properties source guessed");
            const auto attribute=[&](const std::string& dataset,const char* name) {
                std::string value;f.getDataSet(dataset).getAttribute(name).read(value);return value;
            };
            require(attribute("Data/DENS","unit")=="g/cm^3","density unit missing");
            require(attribute("Data/DENS","centering")=="cell","field centering missing");
            require(attribute("Data/DENS","basis")=="scalar","density basis mismatch");
            require(attribute("Data/ENTR","unit")=="unknown","ENTR falsely fixed entropy unit");
            require(attribute("Data/ENTR","meaning")=="pressure_density_proxy","ENTR mislabeled");
            require(!attribute("Data/ENTR","unit_reason").empty(),"unknown unit reason absent");
            require(attribute("Data/species","unit")=="1","dimensionless species mistaken for unknown");
            require(attribute("Data/unregistered","unit")=="unknown","serializer inferred undeclared field");
            f.getGroup("NativeGrid").getAttribute("measure_unit").read(id);
            require(id==(dimension==1 ? "cm" : "cm^2"),"low-dimensional measure unit mismatch");
            f.getGroup("NativeGrid").getAttribute("measure_normalization").read(id);
            require(id==(dimension==1 ? "per_unit_transverse_area" : "per_unit_transverse_length"),
                    "low-dimensional normalization mismatch");
            f.getGroup("SourceIdentity").getAttribute("eos_unit_system").read(id);
            require(id=="cgs","producer unit system not stored");
            f.getGroup("Grid").getAttribute("coordinate_unit").read(id);
            require(id=="cm","Cartesian physical coordinate unit absent");
            f.getAttribute("time_unit").read(id);
            require(id=="s","CGS time unit absent");
            std::vector<double> measure;
            f.getDataSet("NativeGrid/cell_measure").read(measure);
            require(measure==native.cell_measure,"stored measure differs");
            std::vector<double> lower;
            f.getDataSet("NativeGrid/x1_lower").read(lower);
            require(lower==native.lower[0],"stored lower bounds differ");
            std::vector<double> raw(field.size());
            f.getDataSet("Data/DENS").read(raw.data());
            require(raw==field,"metadata changed field order");
            std::vector<uint32_t> logical;
            f.getDataSet("NativeGrid/logical_x1").read(logical);
            require(logical==std::vector<uint32_t>{0,1},"logical mapping changed");
        }
        const auto original_digest=arch::core::file_sha256(path.string());
        auto unavailable_properties=identity;
        unavailable_properties.species_A.clear();unavailable_properties.species_Z.clear();
        unavailable_properties.species_gamma.clear();unavailable_properties.species_Cv.clear();
        const auto legacy_path=root/("unknown-properties-"+std::to_string(dimension)+".h5");
        io::write_hdf5_plt_impl(legacy_path.string(),0,dimension,"cartesian",native_dims,
            cx,cy,cz,{0,0},{1,2},{{"DENS",field}},&native,&unavailable_properties);
        {
            HighFive::File file(legacy_path.string(),HighFive::File::ReadOnly);
            auto source=file.getGroup("SourceIdentity");std::string value;
            source.getAttribute("species_properties_state").read(value);
            require(value=="unknown","missing caller properties were guessed");
            source.getAttribute("species_properties_reason").read(value);
            require(!value.empty(),"unknown properties reason missing");
            require(!source.exist("species_A"),"missing properties synthesized");
        }
        auto bad_metadata=declarations;bad_metadata["ENTR"].unit_reason.clear();
        bool metadata_rejected=false;
        try {
            io::write_hdf5_plt_impl(path.string(),0,dimension,"cartesian",native_dims,
                cx,cy,cz,{0,0},{1,2},{{"DENS",field},{"ENTR",field},{"species",field}},&native,&identity,&bad_metadata);
        }catch(const std::invalid_argument&){metadata_rejected=true;}
        require(metadata_rejected,"unknown field unit without reason accepted");
        require(arch::core::file_sha256(path.string())==original_digest,"bad metadata replaced published file");
        auto bad_measure=native;bad_measure.normalization="total_3d_volume";
        bool normalization_rejected=false;
        try {
            io::write_hdf5_plt_impl(path.string(),0,dimension,"cartesian",native_dims,
                cx,cy,cz,{0,0},{1,2},{{"DENS",field}},&bad_measure);
        }catch(const std::invalid_argument&){normalization_rejected=true;}
        require(normalization_rejected,"3D normalization accepted for low-dimensional measure");
        require(arch::core::file_sha256(path.string())==original_digest,"bad normalization replaced file");
        for(int corruption=0;corruption<2;++corruption) {
            auto bad_properties=identity;
            if(corruption==0)bad_properties.species_Cv.clear();
            else bad_properties.species_A[0]=std::numeric_limits<double>::quiet_NaN();
            bool rejected=false;
            try {
                io::write_hdf5_plt_impl(path.string(),0,dimension,"cartesian",native_dims,
                    cx,cy,cz,{0,0},{1,2},{{"DENS",field}},&native,&bad_properties);
            }catch(const std::invalid_argument&){rejected=true;}
            require(rejected,"partial/nonfinite resolved species evidence accepted");
            require(arch::core::file_sha256(path.string())==original_digest,"bad species evidence replaced file");
        }
        auto invalid_run=identity;invalid_run.run_id="filename-derived";
        bool invalid_run_rejected=false;
        try {
            io::write_hdf5_plt_impl(path.string(),0.,dimension,"cartesian",native_dims,
                cx,cy,cz,{0,0},{1,2},{{"DENS",field}},&native,&invalid_run);
        } catch(const std::invalid_argument&) {invalid_run_rejected=true;}
        require(invalid_run_rejected,"malformed run identity accepted");
        require(arch::core::file_sha256(path.string())==original_digest,"invalid run replaced published file");
        auto bad_identity=identity;bad_identity.eos_table_sha256="not-a-digest";
        bool bad_source_rejected=false;
        try {
            io::write_hdf5_plt_impl(path.string(),0,dimension,"cartesian",native_dims,
                cx,cy,cz,{0,0},{1,2},{{"DENS",field}},&native,&bad_identity);
        }catch(const std::invalid_argument&){bad_source_rejected=true;}
        require(bad_source_rejected,"invalid EOS source digest accepted");
        auto invalid=native;
        invalid.upper[0][0]=invalid.lower[0][0];
        bool rejected=false;
        try {
            io::write_hdf5_plt_impl(path.string(),0,dimension,"cartesian",native_dims,
                cx,cy,cz,{0,0},{1,2},{{"DENS",field}},&invalid);
        } catch(const std::invalid_argument&) { rejected=true; }
        require(rejected,"invalid native bounds accepted");
    }
    require(io::plot_field_metadata("PRES",true).unit=="erg/cm^3","pressure unit changed");
    require(io::plot_field_metadata("TEMP",true).unit=="K","temperature unit changed");
    require(io::plot_field_metadata("ENER",true).meaning=="total_energy_density","energy meaning changed");
    require(io::plot_field_metadata("ENUC",true).unit=="erg/g/s","specific burn rate unit changed");
    require(io::plot_field_metadata("VELX",true).basis=="cartesian","Cartesian velocity basis absent");
    require(io::plot_field_metadata("VELX",false).basis=="unknown","curved basis guessed");
    require(arch::fields::cgs_unit("ENTR").empty(),"API assigned fixed ENTR unit");
    require(arch::fields::cgs_unit("not-a-field").empty(),"unknown unit inferred");
    auto target = root / "candidate.h5";
    std::vector<size_t> dims{2,3,5};
    std::vector<double> x(30), y(30), z(30,0), v(30);
    for (size_t i=0;i<30;++i) { x[i]=i%5; y[i]=(i/5)%3; v[i]=double(i)+.125; }
    v[7]=std::numeric_limits<double>::quiet_NaN();
    v[8]=std::numeric_limits<double>::infinity();
    auto write=[&](auto path, auto values) {
        io::write_hdf5_plt_impl(path,0,2,"cartesian",dims,x,y,z,{0,1},{9,3},{{"DENS",values}});
    };
    // Sod-shaped 1D layout uses the same unchanged [block,x1] storage.
    auto one = root / "sod-layout.h5";
    io::write_hdf5_plt_impl(one.string(),0,1,"cartesian",{2,5},
        {0,1,2,3,4,5,6,7,8,9},std::vector<double>(10,0),std::vector<double>(10,0),
        {0,1},{7,2},{{"DENS",{1,1,1,1,1,.125,.125,.125,.125,.125}}});
    {
        HighFive::File f(one.string(),HighFive::File::ReadOnly);
        require(f.getDataSet("Data/DENS").getSpace().getDimensions()==std::vector<size_t>{2,5},
                "Sod-shaped 1D layout changed");
    }
    auto check=[&]() {
        HighFive::File f(target.string(),HighFive::File::ReadOnly);
        require(f.getDataSet("Data/DENS").getSpace().getDimensions()==dims,"shape changed");
        std::vector<double> raw(30); f.getDataSet("Data/DENS").read(raw.data());
        for(size_t i=0;i<30;++i)
            require((std::isnan(v[i])&&std::isnan(raw[i]))||v[i]==raw[i],"raw value changed");
        std::string unit;f.getDataSet("Data/DENS").getAttribute("unit").read(unit);
        require(unit=="unknown","low-level writer inferred unit from field name");
        std::string version; f.getAttribute("plot_publication_version").read(version);
        require(version=="candidate-1","version absent");
    };
    write(target.string(),v); check();
#ifdef ARCH_PLOT_FAILURE_TEST
    for(Fault stage : {Fault::Write,Fault::Flush,Fault::Close}) {
        const auto original_digest=arch::core::file_sha256(target.string());
        fault=stage;injected=0;bool propagated=false;
        std::ostringstream captured;
        auto* original_output=std::cout.rdbuf(captured.rdbuf());
        try { write(target.string(),v); }catch(const std::exception&) { propagated=true; }
        std::cout.rdbuf(original_output);
        require(propagated&&injected==1&&fault==Fault::None,"HDF5 failure injection missed/swallowed");
        require(captured.str().find("Saved PLT")==std::string::npos,"false success after HDF5 failure");
        require(arch::core::file_sha256(target.string())==original_digest,"HDF5 failure replaced prior published bytes");
        check();
        for(auto& e:std::filesystem::directory_iterator(root))
            require(e.path().filename().string().find(".partial-")==std::string::npos,"failure temporary leaked");
    }
    std::cout<<"PASS injected HDF5 write/flush/close failures; no false success; previous file unchanged"<<std::endl;
#endif
    bool failed=false;
    try { write(target.string(),std::vector<double>{1}); }
    catch(const std::invalid_argument&) { failed=true; }
    require(failed,"invalid field length accepted"); check();
    auto collision=root/"existing-directory"; std::filesystem::create_directories(collision);
    std::ofstream(collision/"keep")<<"existing";
    failed=false;
    try { write(collision.string(),v); } catch(const std::exception&) { failed=true; }
    require(failed&&std::filesystem::exists(collision/"keep"),"rename failure/preservation");
    failed=false;
    try { write((root/"missing-parent"/"out.h5").string(),v); }
    catch(const std::exception&) { failed=true; }
    require(failed,"create failure swallowed");
    v[0]=-7.25; write(target.string(),v); check();
    for(auto& e:std::filesystem::directory_iterator(root))
        require(e.path().filename().string().find(".partial-")==std::string::npos,"temporary leaked");
    std::cout<<"PASS non-square raw/nonfinite values, replacement, failures and cleanup\n";
    return 0;
 } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
