/**
 * @file BaryonEosRegression.cpp
 * @brief Bounded real author-table acceptance through the public EOS owner.
 *
 * The independently tested ASCII reader supplies the source oracle; separately
 * tested Helm component queries are accumulated in long double, without calling
 * the completion routine. Source nodes have a fixed P/E error budget. Interior
 * points establish single-potential closure and inverses, not interpolation
 * accuracy relative to an unavailable continuous nuclear-matter model.
 * Large external tables are explicit arguments and are not a default CI input.
 */
#include "physics/eos/HelmEos.h"
#include "physics/eos/Tabular3DEOS.h"
#include "physics/eos/TabularBaryonSource.h"
#include "core/FileFingerprint.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using Status = tabular_eos::FreeEnergyStatus;
constexpr int node_samples = 200, interior_samples = 50;
constexpr int minimum_nodes = 80, minimum_interiors = 20;
constexpr double node_budget = 2e-8, inverse_energy_budget = 2e-12;
constexpr double resolved_temperature_budget = 2e-8, identity_budget = 5e-13;
constexpr const char* ambiguous_inverse = "Native tabular EOS has multiple valid temperature roots";

void require(bool value, const char* message)
{ if (!value) throw std::runtime_error(message); }

double check(double actual, double expected, double tolerance, const char* message,
             double minimum_scale = 1.0)
{
    const double relative = std::abs(actual-expected)/std::max(std::abs(expected),minimum_scale);
    if (!std::isfinite(actual) || !std::isfinite(expected) || relative>tolerance) {
        std::cerr << std::setprecision(17) << message << " actual=" << actual
                  << " expected=" << expected << " relative=" << relative
                  << " budget=" << tolerance << '\n';
        throw std::runtime_error(message);
    }
    return relative;
}

struct Counts {
    int accepted=0, source_hole=0, component_hole=0, stencil_hole=0;
    int nonpositive_pe=0, nonpositive_cv=0, nonpositive_sound=0, ambiguous=0;
    int resolved=0, underresolved=0;
    int total() const {
        return accepted+source_hole+component_hole+stencil_hole+
            nonpositive_pe+nonpositive_cv+nonpositive_sound+ambiguous;
    }
};

// Only these documented physical rejections are classified. A bad domain,
// nonfinite derivative, missing inverse, unexpected exception or wrong error
// budget is a hard failure rather than an accepted sample omission.
bool classify(Status status, Counts& counts)
{
    switch(status) {
    case Status::success: return false;
    case Status::invalid_native_cell: ++counts.stencil_hole; return true;
    case Status::invalid_pressure_or_energy: ++counts.nonpositive_pe; return true;
    case Status::invalid_heat_capacity: ++counts.nonpositive_cv; return true;
    case Status::invalid_sound_speed: ++counts.nonpositive_sound; return true;
    default: throw std::runtime_error("Unexpected baryon EOS failure status="+std::to_string(int(status)));
    }
}

void finite_physical_rejection(const Tabular3DEOSHostView& eos,
    double rho,double T,double ye,Status status)
{
    if(status==Status::success || status==Status::invalid_native_cell) return;
    if(status!=Status::invalid_pressure_or_energy && status!=Status::invalid_heat_capacity
        && status!=Status::invalid_sound_speed) return; // classify() rejects every other status.
    const auto f=eos.interpolate_free_energy(rho,T,ye);
    for(double value : {f.a,f.ax,f.ay,f.axx,f.axy,f.ayy})
        require(std::isfinite(value),"nonfinite interpolated potential cannot be classified as a physical rejection");
    const double pressure=rho*f.ax,energy=f.a-f.ay+eos.energy_reference_shift;
    const double cv=(f.ay-f.ayy)/T;
    require(std::isfinite(pressure) && std::isfinite(energy) && std::isfinite(cv),
            "nonfinite P/E/cv cannot be classified as a nonpositive state");
    if(status==Status::invalid_sound_speed) {
        const double pt=rho*f.axy/T,er=(f.ax-f.axy)/rho;
        const double chi=f.ax+f.axx-pt*er/cv,kappa=pt/cv;
        require(std::isfinite(chi) && std::isfinite(kappa)
            && std::isfinite(chi+kappa*pressure/(rho*rho)),
            "nonfinite acoustics cannot be classified as a nonpositive state");
    }
}

double ulp(double value)
{ return std::nextafter(std::abs(value),std::numeric_limits<double>::infinity())-std::abs(value); }

void report_counts(const char* label, const Counts& counts)
{
    std::cout << label << " accepted=" << counts.accepted
        << " source_hole=" << counts.source_hole << " component_hole=" << counts.component_hole
        << " derivative_stencil_hole=" << counts.stencil_hole
        << " nonpositive_pressure_energy=" << counts.nonpositive_pe
        << " nonpositive_cv=" << counts.nonpositive_cv
        << " nonpositive_sound_speed=" << counts.nonpositive_sound
        << " ambiguous_inverse=" << counts.ambiguous
        << " resolved=" << counts.resolved << " underresolved=" << counts.underresolved << '\n';
}

void identities(const Tabular3DEOSHostView& eos, double rho, double T, double ye,
                const tabular_eos::ThermodynamicState& state)
{
    const auto f=eos.interpolate_free_energy(rho,T,ye);
    // These contractions are deliberately outside evaluate_thermodynamics and
    // use extended precision. They test the route's one-potential/one-gauge
    // identity, not an independent continuous-source interpolation oracle.
    const long double pressure=static_cast<long double>(rho)*f.ax;
    const long double energy=static_cast<long double>(f.a)-f.ay+eos.energy_reference_shift;
    const long double cv=(static_cast<long double>(f.ay)-f.ayy)/T;
    const long double pressure_T=static_cast<long double>(rho)*f.axy/T;
    const long double energy_rho=(static_cast<long double>(f.ax)-f.axy)/rho;
    const long double chi=f.ax+static_cast<long double>(f.axx)-pressure_T*energy_rho/cv;
    const long double kappa=pressure_T/cv;
    const long double acoustic=chi+kappa*pressure/(static_cast<long double>(rho)*rho);
    check(state.pressure,static_cast<double>(pressure),identity_budget,"single-potential P identity");
    check(state.energy,static_cast<double>(energy),identity_budget,"single-potential E/gauge identity");
    check(state.cv,static_cast<double>(cv),identity_budget,"single-potential cv identity");
    check(state.dp_dT,static_cast<double>(pressure_T),identity_budget,"single-potential P_T identity");
    check(state.dp_drho_e,static_cast<double>(chi),identity_budget,"single-potential chi identity",
          std::max(std::abs(f.ax),std::abs(f.axx)));
    check(state.dp_de_rho,static_cast<double>(kappa),identity_budget,"single-potential kappa identity");
    check(state.sound_speed*state.sound_speed,static_cast<double>(acoustic),identity_budget,
          "single-potential sound-speed identity");
    check(static_cast<double>(static_cast<long double>(rho)*rho*energy_rho),
          static_cast<double>(pressure-T*pressure_T),identity_budget,
          "first-law density identity",state.pressure);
}

void test_table(const std::filesystem::path& directory, const char* helm_path,
                const char* source_path)
{
    const auto started=std::chrono::steady_clock::now();
    const auto source=tabular_eos::source::read_baryon_ascii_table(source_path);
    SpeciesManager specs;
    specs.add_species("n",1,0,5.0/3.0,1.5e8);
    specs.add_species("p",1,1,5.0/3.0,1.5e8);
    HelmEos helm(helm_path,nullptr);
    Tabular3DEOS owner(source_path,&specs,helm_path);
    const auto eos=owner.get_view();
    require(eos.strict_domain && eos.uses_free_energy && !eos.native_direct,
            "raw baryon owner did not select strict completed free energy");
    require(eos.n_rho==int(source.density.size()) && eos.n_T==int(source.temperature.size())
            && eos.n_X==int(source.electron_fraction.size()),"baryon source axes were resampled");
    require(eos.axis_nodes[2]!=nullptr,"baryon owner lost explicit composition nodes");
    for(int k=0;k<eos.n_X;++k)
        require(eos.axis_nodes[2][k]==source.electron_fraction[k],"native Ye node changed");
    const double mass_scale=1.0/(source.baryon_mass_g*HelmEosHostView::avo);
    const auto digest=arch::core::file_sha256(source_path);
    std::ofstream detail(directory/(std::filesystem::path(source_path).filename().string()+"-samples.tsv"),std::ios::app);
    require(bool(detail),"cannot open baryon sample report");
    detail << std::setprecision(17) << "# source=" << source_path << " sha256=" << digest
           << " helm_sha256=" << arch::core::file_sha256(helm_path) << '\n'
           << "# kind\tsample\trho\tT\tYe\tstatus\tP_error\tE_error\tT_error\tT_resolution\n";
    Counts nodes,interiors;
    double max_pressure_error=0,max_energy_error=0,max_printed_source_difference=0;
    double max_inverse_error=0,max_resolved_inverse_error=0,max_inverse_energy_error=0;
    double max_thermal_condition=0;
    for(int sample=0;sample<node_samples;++sample) {
        const int r=int(std::llround(double(eos.n_rho-1)*sample/(node_samples-1)));
        const int t=int(std::llround(double(eos.n_T-1)*((73*sample)%node_samples)/(node_samples-1)));
        const int y=int(std::llround(double(eos.n_X-1)*((127*sample)%node_samples)/(node_samples-1)));
        const std::size_t index=(static_cast<std::size_t>(r)*eos.n_T+t)*eos.n_X+y;
        const double rho=source.density[r],T=source.temperature[t],ye=source.electron_fraction[y];
        const double X[]{1-ye,ye};
        const auto result=eos.free_energy_result(rho,T,ye);
        detail << "node\t" << sample << '\t' << rho << '\t' << T << '\t' << ye
               << '\t' << int(result.status);
        if(source.source_valid[index]!=1.0) {
            require(result.status==Status::invalid_native_cell,"source hole accepted by completed owner");
            ++nodes.source_hole; detail << "\tnan\tnan\tnan\tnan\n"; continue;
        }
        HelmEosHostView::ComponentThermodynamics electron,photon;
        if(!helm.electron_positron_component(rho*mass_scale,T,ye,electron)
            || !HelmEosHostView::photon_component(rho,T,photon)) {
            require(result.status==Status::invalid_native_cell,"component hole accepted by completed owner");
            ++nodes.component_hole; detail << "\tnan\tnan\tnan\tnan\n"; continue;
        }
        finite_physical_rejection(eos,rho,T,ye,result.status);
        if(classify(result.status,nodes)) { detail << "\tnan\tnan\tnan\tnan\n"; continue; }
        const long double expected_pressure=static_cast<long double>(source.pressure[index])
            +electron.pressure+photon.pressure;
        // The entropy retains cold thermal information that independently
        // rounded printed F/E cannot reliably encode by their difference.
        const long double baryon_energy=static_cast<long double>(source.free_energy[index])
            +static_cast<long double>(T)*source.entropy[index];
        const long double expected_energy=baryon_energy
            +static_cast<long double>(mass_scale)*electron.energy+photon.energy+eos.energy_reference_shift;
        const double pe=check(eos.get_pressure_from_rho_T(rho,T,X),static_cast<double>(expected_pressure),
                              node_budget,"raw-node completed pressure");
        const double ee=check(eos.get_eint_from_T(rho,T,X),static_cast<double>(expected_energy),
                              node_budget,"raw-node completed energy");
        max_pressure_error=std::max(max_pressure_error,pe); max_energy_error=std::max(max_energy_error,ee);
        max_printed_source_difference=std::max(max_printed_source_difference,
            std::abs(static_cast<double>(baryon_energy)-source.energy[index])/
            std::max({std::abs(source.free_energy[index]),std::abs(source.energy[index]),
                      std::abs(T*source.entropy[index]),1.0}));
        identities(eos,rho,T,ye,result.state);
        ++nodes.accepted;
        detail << '\t' << pe << '\t' << ee << "\tnan\tnan\n";
    }
    for(int sample=0;sample<interior_samples;++sample) {
        const double lr=eos.log_rho_min+(eos.log_rho_max-eos.log_rho_min)*(sample+.5)/interior_samples;
        const double lt=eos.log_T_min+(eos.log_T_max-eos.log_T_min)*((17*sample)%interior_samples+.37)/interior_samples;
        const double ye=eos.X_min+(eos.X_max-eos.X_min)*((31*sample)%interior_samples+.23)/interior_samples;
        const double rho=std::pow(10.,lr),T=std::pow(10.,lt),X[]{1-ye,ye};
        const auto result=eos.free_energy_result(rho,T,ye);
        detail << "interior\t" << sample << '\t' << rho << '\t' << T << '\t' << ye
               << '\t' << int(result.status);
        finite_physical_rejection(eos,rho,T,ye,result.status);
        if(classify(result.status,interiors)) { detail << "\tnan\tnan\tnan\tnan\n"; continue; }
        identities(eos,rho,T,ye,result.state);
        const auto f=eos.interpolate_free_energy(rho,T,ye);
        const double scale=std::abs(f.a)+std::abs(f.ay)+std::abs(eos.energy_reference_shift);
        const double condition=result.state.energy/(T*result.state.cv);
        // Classify resolution before inversion from energy quantization and
        // the stored potential's subtractive conditioning. The 64-ulp bound
        // is only an arithmetic check for unresolved points, not a relaxed
        // physical-temperature accuracy claim.
        const double resolution=(ulp(scale)+ulp(result.state.energy))/(T*result.state.cv);
        const bool resolved=resolution<=resolved_temperature_budget;
        const double arithmetic_budget=64*resolution+64*std::numeric_limits<double>::epsilon()
            *std::log(10.)*std::max(1.0,std::abs(lt));
        require(std::isfinite(resolution) && std::isfinite(arithmetic_budget)
            && std::isfinite(condition),"nonfinite baryon thermal conditioning");
        double recovered;
        try { recovered=eos.get_temperature(rho,result.state.energy,X); }
        catch(const std::runtime_error& error) {
            if(std::string(error.what())!=ambiguous_inverse) throw;
            ++interiors.ambiguous; detail << "\tnan\tnan\tambiguous\t" << resolution << '\n'; continue;
        }
        const double inverse_error=std::abs(recovered-T)/T;
        max_inverse_energy_error=std::max(max_inverse_energy_error,
            check(eos.get_eint_from_T(rho,recovered,X),result.state.energy,inverse_energy_budget,
                  "baryon inverse energy residual"));
        max_inverse_error=std::max(max_inverse_error,inverse_error);
        max_thermal_condition=std::max(max_thermal_condition,condition);
        if(resolved) {
            check(recovered,T,resolved_temperature_budget,"baryon resolved inverse temperature");
            max_resolved_inverse_error=std::max(max_resolved_inverse_error,inverse_error);
            ++interiors.resolved;
        } else {
            require(std::isfinite(inverse_error) && inverse_error<=arithmetic_budget,
                    "baryon unresolved inverse exceeds arithmetic budget");
            ++interiors.underresolved;
        }
        ++interiors.accepted;
        detail << "\tnan\tnan\t" << inverse_error << '\t' << resolution << '\n';
    }
    // Domain controls are independent of whether an endpoint cell is masked.
    const double middle_ye=.5*(eos.X_min+eos.X_max),middle_rho=std::pow(10.,.5*(eos.log_rho_min+eos.log_rho_max));
    const double middle_T=std::pow(10.,.5*(eos.log_T_min+eos.log_T_max));
    for(const auto& point : {std::array<double,3>{std::nextafter(source.density.front(),0.),middle_T,middle_ye},
                            std::array<double,3>{middle_rho,std::nextafter(source.temperature.back(),INFINITY),middle_ye},
                            std::array<double,3>{middle_rho,middle_T,std::nextafter(eos.X_min,-INFINITY)},
                            std::array<double,3>{NAN,middle_T,middle_ye}})
        require(eos.free_energy_result(point[0],point[1],point[2]).status==Status::invalid_native_domain,
                "raw baryon owner accepted outside-domain control");
    const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    std::cout << std::setprecision(17) << "baryon EOS source=" << source_path << " sha256=" << digest
        << " nodes=" << source.energy.size() << " energy_shift=" << eos.energy_reference_shift
        << " electron_mass_scale=" << mass_scale << '\n';
    report_counts("node samples",nodes); report_counts("interior samples",interiors);
    std::cout << "max_node_P_relative=" << max_pressure_error << " max_node_E_relative=" << max_energy_error
        << " node_budget=" << node_budget << " max_source_printed_E_scaled_difference=" << max_printed_source_difference
        << " max_inverse_T_relative=" << max_inverse_error << " max_resolved_inverse_T_relative=" << max_resolved_inverse_error
        << " max_inverse_E_relative=" << max_inverse_energy_error
        << " max_condition_E_over_Tcv=" << max_thermal_condition << " elapsed_seconds=" << elapsed << '\n';
    require(nodes.total()==node_samples && interiors.total()==interior_samples
        && interiors.resolved+interiors.underresolved==interiors.accepted,"inconsistent real-table sample accounting");
    require(nodes.accepted>=minimum_nodes && interiors.accepted>=minimum_interiors,
            "insufficient real baryon EOS valid-state coverage");
}
} // namespace

int main(int argc,char** argv)
{
    try {
        if(argc<4) throw std::runtime_error("expected output directory, Helm table, and original baryon table(s)");
        std::filesystem::create_directories(argv[1]);
        for(int i=3;i<argc;++i) test_table(argv[1],argv[2],argv[i]);
        std::cout << "explicit real baryon EOS owner acceptance PASS\n";
    } catch(const std::exception& error) {
        std::cerr << "real baryon EOS acceptance FAILED: " << error.what() << '\n';
        return 1;
    }
}
