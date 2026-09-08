/**
 * @file HelmComponentsRegression.cpp
 * @brief Selective electron/photon Helmholtz components without ion subtraction.
 *
 * An independent quadratic free energy checks every component field. Actual
 * Timmes data check direct-electron identity, strict table endpoints and the
 * unchanged complete-EOS reference. No alternate runtime EOS is introduced.
 */
#include "physics/eos/HelmEos.h"
#include "fixtures/HelmReference.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using View = BasicHelmEosView<SpeciesHostView>;
using Component = View::ComponentThermodynamics;

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void close(double actual, double expected, double tolerance, const char* message)
{
    if (!std::isfinite(actual)
        || std::abs(actual - expected) > tolerance * std::max(std::abs(expected), 1.0)) {
        std::cerr << std::setprecision(17) << message << ": actual=" << actual
                  << " expected=" << expected << " tolerance=" << tolerance << '\n';
        throw std::runtime_error(message);
    }
}

bool invalid(const Component& value)
{
    return std::isnan(value.pressure) && std::isnan(value.energy)
        && std::isnan(value.free_energy) && std::isnan(value.cv)
        && std::isnan(value.pressure_density) && std::isnan(value.pressure_temperature)
        && std::isnan(value.energy_density) && std::isnan(value.cv_temperature)
        && std::isnan(value.entropy);
}

void boundaries(const View& view)
{
    Component result;
    for (double density : {view.density_nodes[0], view.density_nodes[View::imax - 1]})
        for (double temperature : {view.temperature_nodes[0], view.temperature_nodes[View::jmax - 1]})
            require(view.electron_positron_component(density, temperature, 1.0, result),
                    "electron component rejected an exact supported endpoint");
    const double low_density = std::nextafter(view.density_nodes[0], 0.0);
    const double high_density = std::nextafter(view.density_nodes[View::imax - 1],
                                               std::numeric_limits<double>::infinity());
    const double low_temperature = std::nextafter(view.temperature_nodes[0], 0.0);
    const double high_temperature = std::nextafter(view.temperature_nodes[View::jmax - 1],
                                                   std::numeric_limits<double>::infinity());
    const double rho = 0.5 * (view.density_nodes[0] + view.density_nodes[View::imax - 1]);
    const double T = 0.5 * (view.temperature_nodes[0] + view.temperature_nodes[View::jmax - 1]);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double rejected[][3]{{low_density, T, 1.0}, {high_density, T, 1.0},
        {rho, low_temperature, 0.5}, {rho, high_temperature, 0.5},
        {rho, T, 0.0}, {rho, T, 1.01}, {rho, T, -0.1},
        {nan, T, 0.5}, {rho, nan, 0.5}, {rho, T, nan}, {0.0, T, 0.5}};
    for (const auto& point : rejected) {
        require(!view.electron_positron_component(point[0], point[1], point[2], result)
                && invalid(result), "electron component extrapolated or retained stale output");
    }
    View missing = view;
    missing.f[4] = nullptr;
    require(!missing.electron_positron_component(rho, T, 0.5, result) && invalid(result),
            "electron component accepted missing free-energy data");
    require(!View::photon_component(0.0, 1e8, result) && invalid(result),
            "photon component accepted zero density");
    require(!View::photon_component(1e6, nan, result) && invalid(result),
            "photon component accepted NaN temperature");
}

void polynomial_components()
{
    // Electron-table F(d,T)=a*d-b*T^2+m*d*T, d=rho*Ye. The Hermite
    // interpolant must reproduce this polynomial, not just approximate a gas.
    // Dyadic nodes and coefficients make the fixture data exactly representable;
    // third derivatives must not mistake table-value rounding for polynomial error.
    constexpr double a = 2.0, b = 0.5, m = 0.125;
    View view;
    std::array<double, View::imax> density;
    std::array<double, View::jmax> temperature;
    for (int i = 0; i < View::imax; ++i)
        density[i] = 1.0 + i;
    for (int j = 0; j < View::jmax; ++j)
        temperature[j] = 1024.0 + 16.0*j;
    std::array<std::vector<double>, 9> fields;
    for (int field = 0; field < 9; ++field) {
        fields[field].resize(View::imax * View::jmax);
        view.f[field] = fields[field].data();
    }
    view.density_nodes = density.data();
    view.temperature_nodes = temperature.data();
    for (int j = 0; j < View::jmax; ++j) for (int i = 0; i < View::imax; ++i) {
        const int index = j * View::imax + i;
        const double d = density[i], T = temperature[j];
        fields[0][index] = a*d - b*T*T + m*d*T;
        fields[1][index] = a + m*T;
        fields[2][index] = -2*b*T + m*d;
        fields[4][index] = -2*b;
        fields[5][index] = m;
    }
    const double points[][3]{{64.0, 2040.0, 0.5}, {63.0, 2051.0, 0.25}, {192.0, 3103.0, 0.75}};
    for (const auto& point : points) {
        const double rho = point[0], T = point[1], ye = point[2], d = rho * ye;
        Component actual;
        require(view.electron_positron_component(rho, T, ye, actual),
                "polynomial component query failed");
        close(actual.pressure, d*d*(a+m*T), 2e-11, "polynomial electron pressure");
        close(actual.energy, ye*(a*d+b*T*T), 2e-11, "polynomial electron energy");
        close(actual.free_energy, ye*(a*d-b*T*T+m*d*T), 2e-11, "polynomial electron free energy");
        close(actual.entropy, ye*(2*b*T-m*d), 2e-11, "polynomial electron entropy");
        close(actual.cv, 2*ye*b*T, 2e-11, "polynomial electron heat capacity");
        close(actual.pressure_density, 2*ye*d*(a+m*T), 2e-11, "polynomial electron pressure-density derivative");
        close(actual.pressure_temperature, m*d*d, 2e-11, "polynomial electron pressure-temperature derivative");
        close(actual.energy_density, a*ye*ye, 2e-11, "polynomial electron energy-density derivative");
        close(actual.cv_temperature, 2*ye*b, 2e-11, "polynomial electron heat-capacity derivative");

        Component photon;
        require(View::photon_component(rho, T, photon), "photon component requires a table");
        const long double energy_density = static_cast<long double>(View::asol)
            * std::pow(static_cast<long double>(T), 4);
        close(photon.pressure, static_cast<double>(energy_density / 3), 2e-15, "photon pressure");
        close(photon.energy, static_cast<double>(energy_density / rho), 2e-15, "photon energy");
        close(photon.free_energy, static_cast<double>(-energy_density / (3*rho)), 2e-15, "photon free energy");
        close(photon.entropy, static_cast<double>(4*energy_density / (3*rho*T)), 2e-15, "photon entropy");
        close(photon.cv, static_cast<double>(4*energy_density / (rho*T)), 2e-15, "photon cv");
        close(photon.pressure_density, 0.0, 0.0, "photon fixed-T pressure-density derivative");
        close(photon.pressure_temperature, 4*photon.pressure/T, 2e-15, "photon pressure-temperature derivative");
        close(photon.energy_density, -photon.energy/rho, 2e-15, "photon energy-density derivative");
        close(photon.cv_temperature, 3*photon.cv/T, 2e-15, "photon heat-capacity derivative");
    }
    boundaries(view);
}

void actual_table(const char* path)
{
    SpeciesManager species;
    species.add_species("h1", 1, 1, 5.0/3.0, 0.0);
    species.add_species("he4", 4, 2, 5.0/3.0, 0.0);
    HelmEos eos(path, &species);
    const double fractions[]{0.25, 0.75};
    constexpr double ye = 0.625;
    for (const auto& point : HelmReference::points) {
        Component electron;
        require(eos.electron_positron_component(point.rho, point.temperature, ye, electron),
                "actual Timmes electron component failed");
        double pressure, energy, cv, free_energy, entropy;
        View::ThermodynamicDerivatives derivatives;
        eos.interpolate_ele_pos(point.rho, point.temperature, ye, pressure, energy,
                                &cv, &derivatives, &free_energy, &entropy);
        require(electron.pressure == pressure && electron.energy == energy
                && electron.cv == cv && electron.free_energy == free_energy && electron.entropy == entropy
                && electron.pressure_density == derivatives.pressure_density
                && electron.pressure_temperature == derivatives.pressure_temperature,
                "selective electron component contains an extra correction");
        close(electron.free_energy + point.temperature * electron.entropy,
              electron.energy, 4*std::numeric_limits<double>::epsilon(),
              "electron potential entropy energy identity");
        close(electron.pressure, point.values[3], 8e-14, "independent Timmes electron reference");
        eos.calc_thermo_with_cv(point.rho, point.temperature, fractions, pressure, energy, &cv);
        close(pressure, point.values[0], 8e-14, "unchanged complete Helmholtz pressure");
        close(energy, point.values[1], 1.5e-15, "unchanged complete Helmholtz energy");
        close(cv, point.values[2], 32*std::numeric_limits<double>::epsilon(),
              "unchanged complete Helmholtz cv");
    }
    boundaries(eos);
}
} // namespace

int main(int argc, char** argv)
{
    try {
        require(argc == 2, "expected path to canonical helm_table.dat");
        polynomial_components();
        actual_table(argv[1]);
        std::cout << "Helmholtz electron/photon components: independent fields, strict domain and complete-EOS reference PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
