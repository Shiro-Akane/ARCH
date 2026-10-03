#include "io/hdf5/HDF5Writer.h"
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
