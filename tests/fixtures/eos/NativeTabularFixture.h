/**
 * @file NativeTabularFixture.h
 * @brief Independent log-linear gas written in the native EOSDriver schema.
 *
 * Its physical energy origin is negative in part of the domain. The encoded
 * energy e+shift is positive, and rho/T/Ye nodes are deliberately nonuniform.
 */
#pragma once

#include <array>
#include <cmath>
#include <string>
#include <vector>
#include <highfive/H5File.hpp>
#include "physics/constant/PhysicalConstants.h"

namespace native_tabular_test {
inline constexpr double gas_R = 1.0e8;
inline constexpr double gas_cv = 1.5e8;
inline constexpr double composition_coefficient = 0.7;
inline constexpr double energy_shift = 1.0e18;
inline constexpr double mev_per_kelvin = arch::constants::statistical::cgs::boltzmann
    / arch::constants::units::erg_per_mev;
inline constexpr std::array<double, 3> log_density{3.0, 3.7, 5.0};
inline constexpr std::array<double, 4> log_kelvin{7.0, 7.3, 8.2, 9.1};
inline constexpr std::array<double, 3> electron_fraction{0.1, 0.32, 0.6};

inline void write_table(const std::string& path, bool invalid_node = false,
    const std::array<double, 4>* pressure_profile = nullptr)
{
    HighFive::File file(path, HighFive::File::Overwrite);
    file.createDataSet("pointsrho", std::vector<int>{3});
    file.createDataSet("pointstemp", std::vector<int>{4});
    file.createDataSet("pointsye", std::vector<int>{3});
    file.createDataSet("energy_shift", std::vector<double>{energy_shift});
    file.createDataSet("logrho", std::vector<double>(log_density.begin(), log_density.end()));
    file.createDataSet("ye", std::vector<double>(electron_fraction.begin(), electron_fraction.end()));
    std::vector<double> native_temperature;
    for (double lt : log_kelvin) native_temperature.push_back(lt + std::log10(mev_per_kelvin));
    file.createDataSet("logtemp", native_temperature);
    std::array<std::vector<double>, 6> fields;
    for (double ye : electron_fraction) for (std::size_t t = 0; t < log_kelvin.size(); ++t)
        for (double lr : log_density) {
        const double lt = log_kelvin[t];
        const double rho = std::pow(10.0, lr), temperature = std::pow(10.0, lt);
        const double composition = std::exp(composition_coefficient * ye);
        const double p = rho * gas_R * temperature * composition;
        const double encoded_energy = gas_cv * temperature * composition;
        fields[0].push_back(pressure_profile
            ? std::log10(rho * 1.0e10 * composition) + (*pressure_profile)[t]
            : std::log10(p));
        fields[1].push_back(std::log10(encoded_energy));
        fields[2].push_back(gas_cv * composition / mev_per_kelvin);
        fields[3].push_back((5.0 / 3.0) * gas_R * temperature * composition);
        fields[4].push_back(gas_R * temperature * composition);
        fields[5].push_back(rho * gas_R / gas_cv);
    }
    if (invalid_node) fields[2][0] = -1.0;
    const char* names[]{"logpress", "logenergy", "dedt", "cs2", "dpdrhoe", "dpderho"};
    for (int i = 0; i < 6; ++i)
        file.createDataSet<double>(names[i], HighFive::DataSpace({3, 4, 3}))
            .write_raw(fields[i].data());
}
} // namespace native_tabular_test
