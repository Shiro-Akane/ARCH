/**
 * @file NativeTabularRegression.cpp
 * @brief Native table loading, thermodynamic identities and rejection controls.
 */
#include "fixtures/NativeTabularFixture.h"
#include "physics/eos/Tabular3DEOS.h"
#include "physics/eos/TabularSource.h"
#include "core/FileFingerprint.h"
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void close(double value, double reference, double tolerance, const char* what)
{
    if (!std::isfinite(value) || std::abs(value - reference)
        > tolerance * std::max(std::abs(reference), 1.0))
        throw std::runtime_error(std::string("native EOS mismatch: ") + what);
}
template <class Action>
void rejects(Action action, const char* what)
{
    try { action(); }
    catch (const std::exception&) { return; }
    throw std::runtime_error(std::string("native EOS accepted ") + what);
}
template <class Action>
void rejects_as(Action action, const char* expected, const char* what)
{
    try { action(); }
    catch (const std::runtime_error& error) {
        if (std::string(error.what()) == expected) return;
        throw;
    }
    throw std::runtime_error(std::string("native EOS accepted ") + what);
}
constexpr const char* invalid_cell = "Native tabular EOS query touches an invalid thermodynamic cell";
constexpr const char* ambiguous_inverse = "Native tabular EOS has multiple valid temperature roots";
SpeciesManager species()
{
    SpeciesManager result;
    result.add_species("n", 1.0, 0.0, 5.0/3.0, 1.5e8);
    result.add_species("p", 1.0, 1.0, 5.0/3.0, 1.5e8);
    return result;
}
void manufactured(const std::filesystem::path& directory)
{
    using namespace native_tabular_test;
    const auto path = (directory / "native-eosdriver.h5").string();
    write_table(path);
    const auto info = inspect_tabular_source(path);
    if (info.rank != 3 || !info.nuclear_equilibrium || info.format != TabularSourceFormat::EosDriver)
        throw std::runtime_error("EOSDriver family inspection lost its physical contract");
    if (tabular_source_fingerprint(path) == arch::core::file_sha256(path))
        throw std::runtime_error("native interpretation is absent from the EOS fingerprint");
    auto specs = species();
    Tabular3DEOS owner(path, &specs);
    const auto eos = owner.get_view();
    close(eos.source_energy_shift, energy_shift, 0.0, "energy reference");
    if (!eos.native_direct || !eos.axis_nodes[0] || !eos.axis_nodes[1] || !eos.axis_nodes[2])
        throw std::runtime_error("native table axes were not retained");
    for (int i = 0; i < 40; ++i) {
        const double rho = std::pow(10.0, 3.1 + 1.7 * (i + 0.3) / 40.0);
        const double T = std::pow(10.0, 7.02 + 1.9 * ((i * 17) % 40 + 0.2) / 40.0);
        const double ye = 0.13 + 0.42 * ((i * 7) % 40 + 0.3) / 40.0;
        const double X[]{1.0 - ye, ye};
        const double composition = std::exp(composition_coefficient * ye);
        const double energy = gas_cv * T * composition;
        const double p = rho * gas_R * T * composition;
        const double cv = gas_cv * composition;
        close(eos.get_pressure_from_rho_T(rho, T, X), p, 2e-12, "pressure");
        close(eos.get_eint_from_T(rho, T, X), energy, 2e-12, "shifted energy");
        close(eos.get_cv(rho, T, X), cv, 2e-12, "cv");
        close(eos.get_temperature(rho, energy, X), T, 2e-12, "inverse");
        close(eos.get_dp_drho_e(rho, energy, X), gas_R*T*composition, 2e-12, "chi");
        close(eos.get_dp_de_rho(rho, energy, X), rho*gas_R/gas_cv, 2e-12, "kappa");
        close(eos.get_sound_speed_from_rho_T(rho, T, X), std::sqrt((5.0/3.0)*p/rho), 2e-12, "sound speed");
        close(eos.get_total_energy_primitive(rho,2.0,-3.0,4.0,p,X),
            rho*(energy+14.5),2e-12,"primitive pressure inverse");
        double gradient[3]{}, capacity_gradient[3]{}, action[3]{};
        const double flow[]{0.0, 1.0, 0.0};
        eos.get_energy_composition_gradient<3>(rho, T, X, gradient);
        eos.get_cv_gradient<3>(rho, T, X, capacity_gradient);
        eos.get_energy_composition_hessian_action<3>(rho, T, X, flow, action);
        close(gradient[0], 0.0, 0.0, "neutron energy derivative");
        close(gradient[1], composition_coefficient*energy, 2e-11, "proton energy derivative");
        close(capacity_gradient[1], composition_coefficient*cv, 2e-11, "cv derivative");
        close(action[1], composition_coefficient*composition_coefficient*energy, 3e-11, "energy Hessian");
    }
    // Every source vertex, including all finite domain faces, has an
    // independent analytic value. This also catches source-layout mistakes.
    for (double lr : log_density) for (double lt : log_kelvin) for (double ye : electron_fraction) {
        const double rho=std::pow(10.0,lr), T=std::pow(10.0,lt), X[]{1.0-ye,ye};
        const double composition=std::exp(composition_coefficient*ye);
        const double energy=gas_cv*T*composition, pressure=rho*gas_R*T*composition;
        close(eos.get_pressure_from_rho_T(rho,T,X),pressure,2e-12,"source vertex pressure");
        close(eos.get_eint_from_T(rho,T,X),energy,2e-12,"source vertex energy");
        close(eos.get_temperature(rho,eos.get_eint_from_T(rho,T,X),X),T,2e-12,"source vertex inverse");
    }
    // Exercise an encoded endpoint whose exp/log round trip loses one ulp.
    // No tolerance may turn the immediately smaller physical energy into a
    // valid in-table input.
    auto endpoint_view=eos;
    std::vector<double> endpoint_energy(static_cast<std::size_t>(eos.n_rho)*eos.n_T*eos.n_X);
    for (int r=0;r<eos.n_rho;++r) for (int t=0;t<eos.n_T;++t) for (int x=0;x<eos.n_X;++x)
        endpoint_energy[(r*eos.n_T+t)*eos.n_X+x]=0.01+log_kelvin[t]-log_kelvin[0];
    endpoint_view.table_E=endpoint_energy.data();
    const double endpoint_X[]{0.9,0.1}, endpoint_value=std::pow(10.0,endpoint_energy[0]);
    close(endpoint_view.get_temperature(1e3,endpoint_value,endpoint_X),1e7,2e-12,"encoded endpoint roundtrip");
    rejects_as([&]{ endpoint_view.get_temperature(1e3,std::nextafter(endpoint_value,0.0),endpoint_X); },
        "Native tabular EOS has no valid temperature inverse","energy below decoded native endpoint");
    const double X[]{0.8, 0.2};
    rejects([&]{ eos.get_pressure_from_rho_T(0.0, 1e8, X); }, "zero density");
    rejects([&]{ eos.get_eint_from_T(1e4, 1e5, X); }, "out-of-domain temperature");
    rejects([&]{ eos.get_temperature(1e4, 1.0, X); }, "out-of-domain energy");
    rejects([&]{ eos.get_temperature(std::numeric_limits<double>::quiet_NaN(),1e16,X); }, "NaN density");
    rejects([&]{ double gradient[3]{}; eos.get_energy_composition_gradient<3>(0.0,1e8,X,gradient); },
        "composition derivative outside native domain");
    const double invalid_X[]{0.0, 1.0};
    rejects([&]{ eos.get_cv(1e4, 1e8, invalid_X); }, "out-of-domain Ye");
    const auto bad = (directory / "invalid-native-eosdriver.h5").string();
    write_table(bad, true);
    Tabular3DEOS invalid(bad, &specs);
    rejects([&]{ invalid.get_view().get_cv(1.1e3, 1.1e7, X); }, "invalid source cell");
    rejects([&]{ Tabular3DEOS missing_species(path); }, "missing composition metadata");
    const std::array<double,4> decreasing{3.0,2.0,1.0,0.0};
    const std::array<double,4> multibranch{0.0,1.0,0.0,1.0};
    const std::array<double,4> constant{1.0,1.0,1.0,1.0};
    const double rho=2.0e4, T=std::pow(10.0,7.75), ye=0.2;
    const auto negative_pressure_path=(directory/"negative-pressure-slope.h5").string();
    write_table(negative_pressure_path,false,&decreasing);
    Tabular3DEOS negative_pressure(negative_pressure_path,&specs);
    const auto negative_view=negative_pressure.get_view();
    const double pressure=negative_view.get_pressure_from_rho_T(rho,T,X);
    close(negative_view.get_total_energy_primitive(rho,0,0,0,pressure,X),
        rho*gas_cv*T*std::exp(composition_coefficient*ye),2e-12,"decreasing pressure inverse");
    const auto multiple_path=(directory/"multiple-pressure-roots.h5").string();
    write_table(multiple_path,false,&multibranch);
    Tabular3DEOS multiple_pressure(multiple_path,&specs);
    const auto multiple_view=multiple_pressure.get_view();
    const double multiple_target=multiple_view.get_pressure_from_rho_T(rho,T,X);
    rejects_as([&]{ multiple_view.get_total_energy_primitive(rho,0,0,0,multiple_target,X); },
        ambiguous_inverse,"multiple positive/negative pressure branches");
    const auto constant_path=(directory/"constant-pressure.h5").string();
    write_table(constant_path,false,&constant);
    Tabular3DEOS constant_pressure(constant_path,&specs);
    const auto constant_view=constant_pressure.get_view();
    const double constant_target=constant_view.get_pressure_from_rho_T(rho,1e7,X);
    rejects_as([&]{ constant_view.get_total_energy_primitive(rho,0,0,0,constant_target,X); },
        ambiguous_inverse,"constant pressure interval");
    std::cout << "native EOSDriver: analytic values/derivatives/inverse and strict rejection PASS\n";
}
void real_table(const std::string& path)
{
    auto specs = species();
    Tabular3DEOS owner(path, &specs);
    const auto eos = owner.get_view();
    std::size_t accepted = 0, resolved = 0, under_resolved = 0, rejected = 0, ambiguous = 0;
    double worst_inverse = 0.0, worst_resolved_inverse = 0.0;
    double worst_condition = 0.0, worst_energy_residual = 0.0;
    // Deterministic interior samples traverse the full native coordinate range.
    for (int i = 0; i < 400; ++i) {
        const double lr = eos.log_rho_min + (eos.log_rho_max-eos.log_rho_min)*(i+0.5)/400.0;
        const double lt = eos.log_T_min + (eos.log_T_max-eos.log_T_min)*((i*73)%400+0.5)/400.0;
        const double ye = eos.X_min + (eos.X_max-eos.X_min)*((i*127)%400+0.5)/400.0;
        const double X[]{1.0-ye,ye}, rho=std::pow(10.0,lr), T=std::pow(10.0,lt);
        double energy;
        try { energy = eos.get_eint_from_T(rho,T,X); }
        catch (const std::runtime_error& error) {
            if (std::string(error.what()) != invalid_cell) throw;
            ++rejected;
            continue;
        }
        // Diagnose the attainable thermal resolution BEFORE testing the
        // inverse. The HDF5 source stores log(E), so one stored-field ulp
        // corresponds to ln(10)*ulp(logE)*E/(T*cv) in relative temperature.
        // This does not qualify such a state at the fixed 2e-8 T budget.
        const double cv=eos.get_cv(rho,T,X);
        const double condition=energy/(T*cv);
        const auto lower=[](const double* axis,int count,double value) {
            return std::max(0,std::min(count-2,
                static_cast<int>(std::upper_bound(axis,axis+count,value)-axis)-1));
        };
        const int ir=lower(eos.axis_nodes[0],eos.n_rho,std::log10(rho));
        const int it=lower(eos.axis_nodes[1],eos.n_T,std::log10(T));
        const int iy=lower(eos.axis_nodes[2],eos.n_X,ye);
        double source_log_ulp=0.0;
        for (int dr=0;dr<2;++dr) for (int dt=0;dt<2;++dt) for (int dy=0;dy<2;++dy) {
            const double q=eos.table_E[eos.free_energy_index(ir+dr,it+dt,iy+dy)];
            source_log_ulp=std::max(source_log_ulp,
                std::nextafter(q,std::numeric_limits<double>::infinity())-q);
        }
        const double thermal_resolution=std::log(10.0)*source_log_ulp*condition;
        const bool thermally_resolved=thermal_resolution<=2e-8;
        // A conservative 16-ulp log-field arithmetic budget covers the three
        // nested blends and exp/log inverse operations. It is distinct from
        // the physical T accuracy requirement, which is never relaxed.
        const double arithmetic_budget=16.0*thermal_resolution
            +16.0*std::numeric_limits<double>::epsilon()*std::log(10.0)
                *std::max(1.0,std::abs(std::log10(T)));
        if (!std::isfinite(condition) || !std::isfinite(arithmetic_budget))
            throw std::runtime_error("native source has non-finite thermal conditioning");
        double recovered;
        try { recovered=eos.get_temperature(rho,energy,X); }
        catch (const std::runtime_error& error) {
            if (std::string(error.what()) != ambiguous_inverse) throw;
            if (ambiguous == 0)
                std::cout << std::setprecision(17) << "native first ambiguous sample=" << i
                    << " rho=" << rho << " T=" << T << " Ye=" << ye << '\n';
            ++ambiguous;
            continue;
        }
        const double relative_error=std::abs(recovered-T)/T;
        const double recovered_energy=eos.get_eint_from_T(rho,recovered,X);
        close(recovered_energy,energy,2e-12,"real-table energy residual");
        worst_energy_residual=std::max(worst_energy_residual,std::abs(recovered_energy-energy)/energy);
        worst_inverse=std::max(worst_inverse,relative_error);
        worst_condition=std::max(worst_condition,condition);
        if (!thermally_resolved)
            std::cout << std::setprecision(17) << "native inverse diagnostic sample=" << i
                << " rho=" << rho << " T=" << T << " Ye=" << ye
                << " recovered=" << recovered << " E=" << energy
                << " cv=" << cv << " condition_E_over_Tcv=" << condition
                << " source_logE_ulp=" << source_log_ulp
                << " relative_T_resolution=" << thermal_resolution
                << " arithmetic_budget=" << arithmetic_budget
                << " relative_T_error=" << relative_error << std::endl;
        if (thermally_resolved) {
            close(recovered,T,2e-8,"real-table resolved inverse");
            worst_resolved_inverse=std::max(worst_resolved_inverse,relative_error);
            ++resolved;
        } else {
            if (!std::isfinite(relative_error) || relative_error>arithmetic_budget)
                throw std::runtime_error("native inverse exceeds source-precision arithmetic budget");
            ++under_resolved;
        }
        ++accepted;
    }
    if (resolved < 100 || accepted+rejected+ambiguous!=400 || resolved+under_resolved!=accepted)
        throw std::runtime_error("real table has insufficient or inconsistent sampled coverage");
    std::cout << "native real table: total=400 accepted=" << accepted
              << " resolved=" << resolved << " under_resolved=" << under_resolved
              << " invalid_cell=" << rejected
              << " ambiguous_inverse=" << ambiguous
              << " max_inverse_relative=" << worst_inverse
              << " max_resolved_inverse_relative=" << worst_resolved_inverse
              << " max_condition_E_over_Tcv=" << worst_condition
              << " max_energy_residual=" << worst_energy_residual << '\n';
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3) throw std::runtime_error("expected output directory [native EOSDriver file]");
    std::filesystem::create_directories(argv[1]);
    manufactured(argv[1]);
    if (argc == 3) real_table(argv[2]);
}
