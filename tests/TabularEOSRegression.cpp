#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <highfive/H5File.hpp>
#include "core/FileFingerprint.h"
#include "core/RuntimeParams.h"
#include "physics/eos/Tabular3DEOS.h"
#include "physics/eos/Tabular4DEOS.h"
#include "physics/eos/eosdispatch.h"
#include "physics/species/Species.h"

namespace {
constexpr double R = 1.0e8;
constexpr double gamma_gas = 5.0/3.0;
constexpr double cv_gas = R/(gamma_gas-1.0);

double helmholtz(double rho, double temperature) {
    return R*temperature*std::log(rho) -
           cv_gas*temperature*std::log(temperature);
}
template <typename T>
void scalar(HighFive::File& file, const std::string& name, const T& value) {
    file.createDataSet(name, value);
}
void field(HighFive::File& file, const std::string& name,
           const std::vector<std::size_t>& shape,
           const std::vector<double>& values) {
    auto dataset=file.createDataSet<double>(name, HighFive::DataSpace(shape));
    dataset.write_raw(values.data());
}
std::vector<double> values(int nr, int nt, int nc, double r0, double dr,
                           double t0, double dt) {
    std::vector<double> out(static_cast<std::size_t>(nr)*nt*nc);
    for (int i=0; i<nr; ++i) for (int j=0; j<nt; ++j) for (int c=0; c<nc; ++c)
        out[(static_cast<std::size_t>(i)*nt+j)*nc+c] =
            helmholtz(std::pow(10.0,r0+i*dr), std::pow(10.0,t0+j*dt));
    return out;
}
void common(HighFive::File& file, int rank, int nr, int nt,
            double r0, double r1, double t0, double t1) {
    scalar(file,"arch_eos_version",1); scalar(file,"table_rank",rank);
    scalar(file,"thermodynamic_model",std::string("free_energy"));
    scalar(file,"n_rho",nr); scalar(file,"n_T",nt);
    scalar(file,"log_rho_min",r0); scalar(file,"log_rho_max",r1);
    scalar(file,"log_T_min",t0); scalar(file,"log_T_max",t1);
}
void write3(const std::filesystem::path& path,
            const std::string& composition_axis="Ye") {
    constexpr int nr=161, nt=161, nx=3; constexpr double r0=0,r1=2,t0=6,t1=8;
    HighFive::File file(path.string(),HighFive::File::Overwrite);
    common(file,3,nr,nt,r0,r1,t0,t1);
    scalar(file,"composition_axis",composition_axis);
    scalar(file,"n_X",nx); scalar(file,"X_min",0.4); scalar(file,"X_max",0.6);
    field(file,"free_energy",{nr,nt,nx},
          values(nr,nt,nx,r0,(r1-r0)/(nr-1),t0,(t1-t0)/(nt-1)));
}
void write4(const std::filesystem::path& path) {
    constexpr int nr=161, nt=161, na=2, nz=2; constexpr double r0=0,r1=2,t0=6,t1=8;
    HighFive::File file(path.string(),HighFive::File::Overwrite);
    common(file,4,nr,nt,r0,r1,t0,t1);
    scalar(file,"n_A",na); scalar(file,"n_Z",nz);
    scalar(file,"A_min",1.0); scalar(file,"A_max",3.0);
    scalar(file,"Z_min",0.5); scalar(file,"Z_max",1.5);
    field(file,"free_energy",{nr,nt,na,nz},
          values(nr,nt,na*nz,r0,(r1-r0)/(nr-1),t0,(t1-t0)/(nt-1)));
}
void write_direct3(const std::filesystem::path& path) {
    constexpr int nr=41, nt=41, nx=3;
    constexpr double r0=0.0, r1=2.0, t0=6.0, t1=8.0;
    const std::vector<std::size_t> shape{nr,nt,nx};
    const std::size_t count=static_cast<std::size_t>(nr)*nt*nx;
    std::vector<double> pressure(count), energy(count), sound(count), cv(count);
    std::vector<double> dp_drho(count), dp_dT(count);
    for(int i=0;i<nr;++i) for(int j=0;j<nt;++j) for(int k=0;k<nx;++k) {
        const double rho=std::pow(10.0,r0+i*(r1-r0)/(nr-1));
        const double T=std::pow(10.0,t0+j*(t1-t0)/(nt-1));
        const std::size_t q=(static_cast<std::size_t>(i)*nt+j)*nx+k;
        pressure[q]=rho*R*T; energy[q]=cv_gas*T;
        sound[q]=std::sqrt(gamma_gas*R*T); cv[q]=cv_gas;
        dp_drho[q]=R*T; dp_dT[q]=rho*R;
    }
    HighFive::File file(path.string(),HighFive::File::Overwrite);
    // Deliberately omit table_rank and thermodynamic_model to exercise
    // unambiguous legacy 3D inference and the retained direct-table path.
    scalar(file,"n_rho",nr); scalar(file,"n_T",nt); scalar(file,"n_X",nx);
    scalar(file,"log_rho_min",r0); scalar(file,"log_rho_max",r1);
    scalar(file,"log_T_min",t0); scalar(file,"log_T_max",t1);
    scalar(file,"X_min",0.4); scalar(file,"X_max",0.6);
    field(file,"pressure",shape,pressure); field(file,"energy",shape,energy);
    field(file,"sound_speed",shape,sound); field(file,"cv",shape,cv);
    field(file,"dp_drho",shape,dp_drho); field(file,"dp_dT",shape,dp_dT);
}
double rel(double actual,double expected) {
    return std::abs(actual-expected)/std::max(std::abs(expected),1.0e-300);
}
template <typename View>
double analyze(const View& eos,const double* x) {
    double worst=0.0;
    auto check=[&](double lr,double lt) {
        double rho=std::pow(10.0,lr), T=std::pow(10.0,lt);
        double p=rho*R*T, e=cv_gas*T, cs=std::sqrt(gamma_gas*p/rho);
        worst=std::max({worst,
            rel(eos.get_pressure_from_rho_T(rho,T,x),p),
            rel(eos.get_eint_from_T(rho,T,x),e),
            rel(eos.get_cv(rho,T,x),cv_gas),
            rel(eos.get_sound_speed_from_rho_T(rho,T,x),cs),
            rel(eos.get_dp_drho_e(rho,e,x),R*T),
            rel(eos.get_dp_de_rho(rho,e,x),rho*(gamma_gas-1.0)),
            rel(eos.get_temperature(rho,e,x),T)});
    };
    for (int s=0;s<100;++s)
        check(0.12+1.75*(s+0.37)/100.0,
              6.12+1.75*((37*s)%100+0.23)/100.0);
    for (int ir=0;ir<2;++ir)
        for (int it=0;it<2;++it) check(2.0*ir,6.0+2.0*it);
    return worst;
}
}
int main(int argc,char** argv) {
    if(argc!=2) throw std::runtime_error("expected output directory");
    std::filesystem::path dir=argv[1]; std::filesystem::create_directories(dir);
    auto p3=dir/"ideal_gas_3d.h5", p4=dir/"ideal_gas_4d.h5";
    auto pd=dir/"ideal_gas_direct_legacy_3d.h5";
    auto pcache=dir/"species_cache_3d.h5";
    write3(p3); write4(p4); write_direct3(pd);
    write3(pcache,"species:test");
    if(inspect_eos_table_rank(p3.string())!=3 ||
       inspect_eos_table_rank(p4.string())!=4 ||
       inspect_eos_table_rank(pd.string())!=3)
        throw std::runtime_error("automatic rank detection failed");
    SpeciesManager species; species.add_species("test",2.0,1.0,gamma_gas,cv_gas);
    const double x[1]{1.0};
    Tabular3DEOS eos3(p3.string(),&species), direct(pd.string(),&species);
    Tabular4DEOS eos4(p4.string(),&species);
    double e3=analyze(eos3.get_view(),x), e4=analyze(eos4.get_view(),x);
    double ed=analyze(direct.get_view(),x);
    std::cout<<"ideal-gas free-energy table max relative error: 3D="<<e3
             <<", 4D="<<e4<<"; legacy direct 3D="<<ed<<std::endl;
    if(e3>1.0e-3 || e4>1.0e-3 || ed>1.0e-2)
        throw std::runtime_error("tabular EOS error exceeds its regression tolerance");

    SimConfig ideal_config;
    ideal_config.physics.eos_type="ideal";
    ideal_config.physics.eos_table_path=(dir/"does-not-exist").string();
    bool one_argument_callback=false;
    EOSDispatcher::dispatch_eos(
        arch::dispatch::EosId::Ideal, ideal_config, species,
        [&](auto&&) { one_argument_callback=true; });
    if(!one_argument_callback)
        throw std::runtime_error("one-argument EOS callback was not invoked");

    SimConfig dispatch_config;
    dispatch_config.physics.eos_type="tabular";
    dispatch_config.physics.eos_table_path=pcache.string();
    std::string first_digest;
    EOSDispatcher::dispatch_eos(
        arch::dispatch::EosId::Tabular3D, dispatch_config, species,
        [&](auto&&, std::string_view digest) {
            first_digest=std::string(digest);
        });
    if(first_digest!=arch::core::file_sha256(pcache.string()))
        throw std::runtime_error("dispatcher did not report the loaded table digest");

    {
        HighFive::File file(pcache.string(),HighFive::File::ReadWrite);
        scalar(file,"cache_generation",1);
    }
    std::string reloaded_digest;
    EOSDispatcher::dispatch_eos(
        arch::dispatch::EosId::Tabular3D, dispatch_config, species,
        [&](auto&&, std::string_view digest) {
            reloaded_digest=std::string(digest);
        });
    if(reloaded_digest==first_digest
       || reloaded_digest!=arch::core::file_sha256(pcache.string())
       || EOSDispatcher::cached_table_sha256!=reloaded_digest)
        throw std::runtime_error("changed table content did not invalidate the EOS cache");

    // The cached Host view and the 3D target-species id both depend on the
    // ordered registry values, not merely on the SpeciesManager object's
    // address.  Insert into the same object to exercise reallocation, order,
    // size, and target-id changes together.
    species.species_list.insert(
        species.species_list.begin(),
        GasProperty{"other",4.0,2.0,gamma_gas,cv_gas});
    const double reordered_x[2]{0.2,0.8};
    double target_x=-1.0;
    EOSDispatcher::dispatch_eos(
        arch::dispatch::EosId::Tabular3D, dispatch_config, species,
        [&](auto&& eos, std::string_view) {
            if constexpr (requires { eos.get_target_X(reordered_x); })
                target_x=eos.get_target_X(reordered_x);
        });
    if(target_x!=reordered_x[1])
        throw std::runtime_error(
            "same-object species reorder reused a stale tabular target id");

    // This edit keeps the vector size and allocation unchanged.  A stale
    // pointer-only cache would silently retain the former target id, whereas
    // reloading must reject the now-unregistered axis species.
    species.species_list[1].name="renamed";
    bool in_place_change_rejected=false;
    try {
        EOSDispatcher::dispatch_eos(
            arch::dispatch::EosId::Tabular3D, dispatch_config, species,
            [&](auto&&) {});
    } catch(const std::exception&) {
        in_place_change_rejected=true;
    }
    if(!in_place_change_rejected)
        throw std::runtime_error(
            "same-object species value change reused the stale EOS cache");
    species.species_list[1].name="test";

    {
        std::ofstream damaged(pcache,std::ios::binary|std::ios::trunc);
        damaged<<"not an HDF5 EOS table";
    }
    bool corrupt_rejected=false;
    try {
        EOSDispatcher::dispatch_eos(
            arch::dispatch::EosId::Tabular3D, dispatch_config, species,
            [&](auto&&) {});
    } catch(const std::exception&) {
        corrupt_rejected=true;
    }
    if(!corrupt_rejected)
        throw std::runtime_error("changed invalid table reused the stale EOS cache");
}
