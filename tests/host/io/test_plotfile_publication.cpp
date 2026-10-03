#include "io/hdf5/HDF5Writer.h"
#include "io/plot/PlotGridMetadata.h"
#include <highfive/H5File.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <limits>
#include <stdexcept>
void require(bool b, const char* m) { if (!b) throw std::runtime_error(m); }
int main(int argc, char** argv) {
 try {
    require(argc == 2, "fixture directory required");
    std::filesystem::path root(argv[1]);
    std::filesystem::create_directories(root);
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
        identity.raw_config_sha256="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
        identity.case_id="Sod"; identity.eos_type="ideal"; identity.ideal_gamma=1.4;
        identity.species_names={"test-species"};
        io::write_hdf5_plt_impl(path.string(),0,dimension,"cartesian",native_dims,
            cx,cy,cz,{0,0},{1,2},{{"DENS",field}},&native,&identity);
        {
            HighFive::File f(path.string(),HighFive::File::ReadOnly);
            std::string id;
            f.getAttribute("plot_identity_state").read(id);
            require(id=="unknown","partial evidence falsely certified");
            f.getGroup("SourceIdentity").getAttribute("case_id").read(id);
            require(id=="Sod","case source evidence missing");
            f.getGroup("SourceIdentity").getAttribute("binary_sha256").read(id);
            require(id=="unknown","binary identity fabricated");
            f.getGroup("SourceIdentity").getAttribute("raw_config_sha256").read(id);
            require(id==identity.raw_config_sha256,"captured raw digest changed");
            double gamma=0.;
            f.getGroup("SourceIdentity").getAttribute("ideal_gamma").read(gamma);
            require(gamma==1.4,"resolved gamma changed");
            std::vector<std::string> species;
            f.getDataSet("SourceIdentity/species_names").read(species);
            require(species==identity.species_names,"species ordering changed");
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
        std::string version; f.getAttribute("plot_publication_version").read(version);
        require(version=="candidate-1","version absent");
    };
    write(target.string(),v); check();
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
