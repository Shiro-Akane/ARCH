/**
 * @file TabularLoaderUtils.h
 * @brief Shared host-only validation helpers for normalized tabular EOS files.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <highfive/H5File.hpp>

namespace tabular_eos::loader {

template <typename T>
inline void read_required(const HighFive::File& file,
                          const std::string& name, T& value)
{
    if (!file.exist(name)) {
        throw std::runtime_error(
            "Tabular EOS is missing required dataset '" + name + "'");
    }
    file.getDataSet(name).read(value);
}

inline void validate_shape(const HighFive::DataSet& dataset,
                           const std::vector<std::size_t>& expected,
                           const std::string& name)
{
    if (dataset.getSpace().getDimensions() != expected) {
        throw std::runtime_error(
            "Tabular EOS dataset '" + name +
            "' has a shape inconsistent with table_rank");
    }
}

inline std::vector<double> read_table_field(
    const HighFive::File& file, const std::string& name,
    const std::vector<std::size_t>& expected_shape)
{
    if (!file.exist(name)) {
        throw std::runtime_error(
            "Tabular EOS is missing required dataset '" + name + "'");
    }
    const HighFive::DataSet dataset = file.getDataSet(name);
    validate_shape(dataset, expected_shape, name);
    std::size_t count = 1;
    for (const std::size_t extent : expected_shape) count *= extent;
    std::vector<double> values(count);
    dataset.read(values.data());
    if (!std::all_of(values.begin(), values.end(),
                     [](double value) { return std::isfinite(value); })) {
        throw std::runtime_error(
            "Tabular EOS dataset '" + name +
            "' must contain only finite values");
    }
    return values;
}

inline void validate_direct_thermodynamics(
    const std::vector<double>& pressure,
    const std::vector<double>& energy,
    const std::vector<double>& sound_speed,
    const std::vector<double>& cv)
{
    if (pressure.size() != energy.size() ||
        pressure.size() != sound_speed.size() ||
        pressure.size() != cv.size()) {
        throw std::runtime_error(
            "Direct tabular EOS fields have inconsistent sizes");
    }
    for (std::size_t i = 0; i < pressure.size(); ++i) {
        if (!(pressure[i] > 0.0) || !(energy[i] > 0.0) ||
            !(sound_speed[i] > 0.0) || !(cv[i] > 0.0)) {
            throw std::runtime_error(
                "Direct tabular EOS requires positive pressure, energy, "
                "sound_speed, and cv");
        }
    }
}

inline void validate_energy_increases_with_temperature(
    const std::vector<double>& energy, int density_count,
    int temperature_count, int composition_count)
{
    for (int density = 0; density < density_count; ++density) {
        for (int composition = 0;
             composition < composition_count; ++composition) {
            for (int temperature = 0;
                 temperature + 1 < temperature_count; ++temperature) {
                const std::size_t lower =
                    (static_cast<std::size_t>(density) * temperature_count +
                     temperature) * composition_count + composition;
                if (!(energy[lower + composition_count] > energy[lower])) {
                    throw std::runtime_error(
                        "Direct tabular EOS energy must increase strictly "
                        "with temperature");
                }
            }
        }
    }
}

inline void validate_bounds(double lower, double upper,
                            const std::string& name)
{
    if (!std::isfinite(lower) || !std::isfinite(upper) || !(upper > lower)) {
        throw std::runtime_error(
            "Tabular EOS axis '" + name +
            "' must have finite, increasing bounds");
    }
}

inline void validate_schema_version(const HighFive::File& file)
{
    if (!file.exist("arch_eos_version")) {
        if (file.exist("table_rank") ||
            file.exist("thermodynamic_model")) {
            throw std::runtime_error(
                "Normalized tabular EOS files require arch_eos_version");
        }
        return;
    }
    int version = 0;
    file.getDataSet("arch_eos_version").read(version);
    if (version != 1) {
        throw std::runtime_error(
            "Unsupported normalized tabular EOS schema version: " +
            std::to_string(version));
    }
}

inline void warn_if_coarse_spacing(const char* policy,
                                   double density_spacing,
                                   double temperature_spacing)
{
    if (density_spacing > 0.02 || temperature_spacing > 0.02) {
        std::cerr << '[' << policy
                  << "] Warning: thermodynamic spacing exceeds 0.02 dex; "
                     "refine and run the error analyzer before production use."
                  << std::endl;
    }
}

} // namespace tabular_eos::loader
