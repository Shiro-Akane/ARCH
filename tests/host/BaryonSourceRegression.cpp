/**
 * @file BaryonSourceRegression.cpp
 * @brief Independent ASCII layout, units, reference and source-hole controls.
 */
#include "physics/eos/TabularBaryonSource.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
constexpr double mass_g = 1.66054e-24;
constexpr double mev_erg = 1.602176634e-6;
constexpr double boltzmann = 1.380649e-16;
constexpr std::array<double,5> log_rho{6.0,6.1,6.2,6.3,6.4};
constexpr std::array<double,5> log_mev{-1.0,-0.6,-0.2,0.2,0.6};
constexpr std::array<double,3> ye_axis{0.1,0.3,0.6};
enum class Issue { None, NumberDensity, ChargeCoordinate, NegativePressure,
    TruncatedBlock, DensityGrid, NonfiniteField, ZeroTemperature, WrongHeader };

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void close(double actual, double expected, double tolerance, const char* message)
{
    require(std::isfinite(actual)
        && std::abs(actual-expected)<=tolerance*std::max(1.0,std::abs(expected)),message);
}
template <class Action>
void rejects(Action action, const char* message)
{
    try { action(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error(message);
}

void write_source(const std::string& path, bool old_label, Issue issue=Issue::None)
{
    std::ofstream output(path);
    require(bool(output),"cannot create baryon source fixture");
    output << std::scientific;
    for (std::size_t t=0;t<log_mev.size();++t) {
        const double tm=std::pow(10.0,log_mev[t]);
        output << " cccccccccccc\r\n";
        output << (issue==Issue::WrongHeader ? " unknown columns\n"
            : old_label ? " Log10(T) T\r\n" : " Log10(Temp) Temp\r\n");
        output << std::setprecision(6) << log_mev[t] << ' '
            << (issue==Issue::ZeroTemperature ? 0.0 : tm) << "\r\n\r\n";
        for (std::size_t y=0;y<ye_axis.size();++y) for (std::size_t r=0;r<log_rho.size();++r) {
            const bool selected=t==2 && y==1 && r==2;
            if (selected && issue==Issue::TruncatedBlock) continue;
            const double rho=std::pow(10.0,log_rho[r]);
            const double nb=rho/mass_g/1e39, ye=ye_axis[y];
            const double energy=1.5*tm-2.0;
            const double entropy=30.0+1.5*std::log(tm)-std::log(nb/1e-9)+0.1*ye;
            const double free_energy=energy-tm*entropy-6.506;
            const double pressure=nb*tm;
            std::array<double,16> row{log_rho[r],nb,ye,free_energy,energy,entropy,
                0.0,0.0,938.0,1.0-ye,ye,0.0,0.0,pressure,free_energy,free_energy};
            if (selected && issue==Issue::NumberDensity) row[1]*=1.01;
            if (selected && issue==Issue::ChargeCoordinate) row[2]+=0.001;
            if (selected && issue==Issue::NegativePressure) row[13]=-pressure;
            if (selected && issue==Issue::DensityGrid) row[0]+=0.02;
            for (std::size_t c=0;c<row.size();++c) {
                if (selected && issue==Issue::NonfiniteField && c==4) output << "nan";
                else output << std::setprecision(c==0 || c==2 ? 3 : 6) << row[c];
                output << ' ';
            }
            output << "\r\n";
        }
    }
}

void manufactured(const std::filesystem::path& directory)
{
    using namespace tabular_eos::source;
    const auto path=(directory/"opaque-source.bin").string();
    write_source(path,false);
    require(is_baryon_ascii_table(path),"schema probe used the filename instead of the header");
    const auto table=read_baryon_ascii_table(path);
    require(table.density.size()==5 && table.temperature.size()==5
        && table.electron_fraction.size()==3,"source axes were not recovered");
    require(std::count(table.source_valid.begin(),table.source_valid.end(),1.0)==75,
        "printed valid fixture acquired holes");
    close(table.baryon_mass_g,mass_g,0.0,"source mass convention changed");
    close(table.energy_reference_mev,931.494,0.0,"source energy reference changed");
    close(table.free_energy_reference_mev,938.0,0.0,"source free-energy reference changed");
    for (std::size_t r=0;r<5;++r) for (std::size_t t=0;t<5;++t) for (std::size_t y=0;y<3;++y) {
        const std::size_t i=(r*5+t)*3+y;
        const double rho=std::pow(10.0,log_rho[r]),tm=std::pow(10.0,log_mev[t]);
        const double nb=rho/mass_g/1e39;
        const double energy=1.5*tm-2.0;
        const double entropy=30.0+1.5*std::log(tm)-std::log(nb/1e-9)+0.1*ye_axis[y];
        // The independent expectations use the analytic gas and the declared
        // seven-significant-digit ASCII budget, not production conversion helpers.
        close(table.density[r],rho,2e-15,"density axis changed");
        close(table.temperature[t],tm*mev_erg/boltzmann,5e-15,"temperature units changed");
        close(table.pressure[i],nb*tm*mev_erg*1e39,6e-7,"pressure units/layout changed");
        close(table.energy[i],energy*mev_erg/mass_g,6e-7,"energy units/layout changed");
        close(table.entropy[i],entropy*boltzmann/mass_g,6e-7,"entropy units/layout changed");
        const double raw_free_energy=energy-tm*entropy-6.506;
        // A fixed reference addition does not preserve relative error near
        // zero. Bound the source's absolute seven-digit rounding instead.
        const double free_energy_rounding=0.5*std::pow(10.0,
            std::floor(std::log10(std::abs(raw_free_energy)))-6.0)*mev_erg/mass_g;
        require(std::abs(table.free_energy[i]-(energy-tm*entropy)*mev_erg/mass_g)
            <=free_energy_rounding+1e-13*std::abs(table.free_energy[i]),
            "free energy was not aligned to the source energy reference");
        close(table.source_free_energy_mev[i],energy-tm*entropy-6.506,6e-7,
            "original free energy was overwritten");
    }
    require(*std::min_element(table.energy.begin(),table.energy.end())<0.0,
        "reader silently selected a positivity energy gauge");
    const auto old=(directory/"alternate-header.data").string();
    write_source(old,true);
    const auto same=read_baryon_ascii_table(old);
    require(same.energy==table.energy && same.free_energy==table.free_energy,
        "header aliases selected different physical interpretations");
    for (auto issue : {Issue::NumberDensity,Issue::ChargeCoordinate}) {
        const auto bad=(directory/"source-coordinate-hole.tab").string();
        write_source(bad,false,issue);
        const auto masked=read_baryon_ascii_table(bad);
        require(std::count(masked.source_valid.begin(),masked.source_valid.end(),0.0)==1,
            "source-coordinate mismatch was not isolated as a hole");
        require(masked.density==table.density && masked.electron_fraction==table.electron_fraction,
            "source-coordinate mismatch moved nominal axes");
        require(masked.energy==table.energy,"number-density noise changed the specific-energy units");
    }
    const auto negative=(directory/"negative-baryon-pressure.tab").string();
    write_source(negative,false,Issue::NegativePressure);
    const auto signed_pressure=read_baryon_ascii_table(negative);
    require(*std::min_element(signed_pressure.pressure.begin(),signed_pressure.pressure.end())<0.0
        && std::count(signed_pressure.source_valid.begin(),signed_pressure.source_valid.end(),1.0)==75,
        "baryon-only pressure was subjected to a total-EOS positivity constraint");
    for (auto issue : {Issue::TruncatedBlock,Issue::DensityGrid,Issue::NonfiniteField,
            Issue::ZeroTemperature,Issue::WrongHeader}) {
        const auto bad=(directory/"malformed-source.tab").string();
        write_source(bad,false,issue);
        rejects([&]{ read_baryon_ascii_table(bad); },"malformed baryon source was accepted");
    }
    std::cout << "baryon ASCII analytic units/reference, header aliases and rejection controls PASS\n";
}

void original(const std::string& path)
{
    const auto table=tabular_eos::source::read_baryon_ascii_table(path);
    require(table.density.size()==110 && table.temperature.size()==91
        && table.electron_fraction.size()==65,"original author table dimensions changed");
    const auto invalid=std::count(table.source_valid.begin(),table.source_valid.end(),0.0);
    double worst_reference=0.0;
    for (std::size_t r=0;r<110;++r) for (std::size_t t=0;t<91;++t) for (std::size_t y=0;y<65;++y) {
        const std::size_t i=(r*91+t)*65+y;
        const double ts=table.temperature[t]*table.entropy[i];
        const double residual=table.free_energy[i]-table.energy[i]+ts;
        const double scale=std::max({std::abs(table.free_energy[i]),std::abs(table.energy[i]),std::abs(ts),1.0});
        worst_reference=std::max(worst_reference,std::abs(residual)/scale);
    }
    std::cout << std::setprecision(17) << "original baryon source=" << path
        << " nodes=" << table.energy.size() << " source_invalid=" << invalid
        << " max_scaled_F_E_TS_residual=" << worst_reference << '\n';
}
} // namespace

int main(int argc,char** argv)
{
    if (argc<2) throw std::runtime_error("expected output directory [original author tables...]");
    std::filesystem::create_directories(argv[1]);
    manufactured(argv[1]);
    for (int i=2;i<argc;++i) original(argv[i]);
}
