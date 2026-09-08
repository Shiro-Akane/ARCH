/**
 * @file TabularCompletion.h
 * @brief Host-only component assembly into a single tabulated free energy.
 *
 * Format readers supply component declarations, CGS axes/potential and source
 * validity. This layer adds only missing electron/positron and photon terms;
 * it neither replaces baryons nor introduces an EOS-name-dependent model.
 */
#pragma once

#include "TabularSource.h"
#include "TabularFreeEnergy.h"
#include <string>
#include <vector>

namespace tabular_eos {

void complete_free_energy(std::vector<double>& potential,
    std::vector<double>& validity, const std::vector<double>& log_density,
    const std::vector<double>& log_temperature,
    const std::vector<double>& electron_fraction,
    const TabularComponents& present, const std::string& helm_path,
    double baryon_mass_g = 0.0,
    std::vector<double>* density_derivative = nullptr,
    std::vector<double>* temperature_derivative = nullptr);

// Propagate source/domain validity through exactly the dependencies of the
// existing repeated five-point derivative operator, including one-sided edges.
std::vector<double> free_energy_derivative_validity(
    const std::vector<double>& input, int nr, int nt, int nc);

// One immutable specific-energy reference for the complete loaded table. It
// does not modify pressure/entropy/cv or depend on a runtime query/composition.
double positive_energy_reference(
    const std::array<std::vector<double>, FieldCount>& fields,
    const std::vector<double>& validity);

std::vector<double> uniform_axis(int count, double minimum, double maximum);
double require_uniform_axis(const std::vector<double>& values,const char* name);

} // namespace tabular_eos
