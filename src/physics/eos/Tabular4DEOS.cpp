/**
 * @file Tabular4DEOS.cpp
 * @brief Validated 4D HDF5 EOS loader for (log10 rho, log10 T, Abar, Zbar).
 */

#include "Tabular4DEOS.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <highfive/H5File.hpp>

namespace {

template <typename T>
void read_required_4d(const HighFive::File& file, const std::string& name, T& value)
{
    if (!file.exist(name)) {
        throw std::runtime_error("Tabular EOS is missing required dataset '" + name + "'");
    }
    file.getDataSet(name).read(value);
}

void validate_shape_4d(const HighFive::DataSet& dataset,
                       const std::vector<std::size_t>& expected,
                       const std::string& name)
{
    if (dataset.getSpace().getDimensions() != expected) {
        throw std::runtime_error(
            "Tabular EOS dataset '" + name + "' has a shape inconsistent with table_rank");
    }
}

void validate_bounds_4d(double lower, double upper, const std::string& name)
{
    if (!std::isfinite(lower) || !std::isfinite(upper) || !(upper > lower)) {
        throw std::runtime_error(
            "Tabular EOS axis '" + name + "' must have finite, increasing bounds");
    }
}

} // namespace

Tabular4DEOS::Tabular4DEOS(const std::string& h5_filename,
                           const SpeciesManager* specs_ptr)
    : table_path(h5_filename)
{
    std::cout << "[Tabular4DEOS] Loading validated 4D HDF5 table: "
              << h5_filename << std::endl;
    HighFive::File file(h5_filename, HighFive::File::ReadOnly);

    if (file.exist("table_rank")) {
        int rank = 0;
        file.getDataSet("table_rank").read(rank);
        if (rank != 4) {
            throw std::runtime_error(
                "Tabular4DEOS received a table whose table_rank is not 4");
        }
    }

    read_required_4d(file, "n_rho", view.n_rho);
    read_required_4d(file, "n_T", view.n_T);
    read_required_4d(file, "n_A", view.n_A);
    read_required_4d(file, "n_Z", view.n_Z);
    if (view.n_rho < 2 || view.n_T < 2 || view.n_A < 2 || view.n_Z < 2) {
        throw std::runtime_error(
            "A 4D tabular EOS requires at least two points on every axis");
    }

    read_required_4d(file, "log_rho_min", view.log_rho_min);
    read_required_4d(file, "log_rho_max", view.log_rho_max);
    read_required_4d(file, "log_T_min", view.log_T_min);
    read_required_4d(file, "log_T_max", view.log_T_max);
    read_required_4d(file, "A_min", view.A_min);
    read_required_4d(file, "A_max", view.A_max);
    read_required_4d(file, "Z_min", view.Z_min);
    read_required_4d(file, "Z_max", view.Z_max);
    validate_bounds_4d(view.log_rho_min, view.log_rho_max, "log10_rho");
    validate_bounds_4d(view.log_T_min, view.log_T_max, "log10_temperature");
    validate_bounds_4d(view.A_min, view.A_max, "Abar");
    validate_bounds_4d(view.Z_min, view.Z_max, "Zbar");

    view.dlog_rho =
        (view.log_rho_max - view.log_rho_min) / (view.n_rho - 1);
    view.dlog_T = (view.log_T_max - view.log_T_min) / (view.n_T - 1);
    view.dA = (view.A_max - view.A_min) / (view.n_A - 1);
    view.dZ = (view.Z_max - view.Z_min) / (view.n_Z - 1);
    if (view.dlog_rho > 0.1 || view.dlog_T > 0.1) {
        std::cerr
            << "[Tabular4DEOS] Warning: thermodynamic spacing exceeds 0.1 dex; "
               "refine and run the error analyzer before production use."
            << std::endl;
    }

    const std::vector<std::size_t> shape{
        static_cast<std::size_t>(view.n_rho),
        static_cast<std::size_t>(view.n_T),
        static_cast<std::size_t>(view.n_A),
        static_cast<std::size_t>(view.n_Z)
    };
    const int composition_count = view.n_A * view.n_Z;
    const std::size_t table_size =
        static_cast<std::size_t>(view.n_rho) * view.n_T *
        composition_count;

    std::string thermodynamic_model = "direct";
    if (file.exist("thermodynamic_model")) {
        file.getDataSet("thermodynamic_model").read(thermodynamic_model);
    }

    if (thermodynamic_model == "free_energy") {
        if (view.n_rho < 5 || view.n_T < 5) {
            throw std::runtime_error(
                "Free-energy tables require at least five rho and temperature points");
        }
        if (!file.exist("free_energy")) {
            throw std::runtime_error(
                "Free-energy tabular EOS is missing dataset 'free_energy'");
        }
        auto dataset = file.getDataSet("free_energy");
        validate_shape_4d(dataset, shape, "free_energy");
        std::vector<double> free_energy(table_size);
        dataset.read(free_energy.data());
        if (free_energy.size() != table_size ||
            !std::all_of(free_energy.begin(), free_energy.end(),
                         [](double value) { return std::isfinite(value); })) {
            throw std::runtime_error(
                "free_energy must contain only finite specific energies in erg/g");
        }
        h_free_energy_fields = tabular_eos::build_derivative_fields(
            free_energy, view.n_rho, view.n_T, composition_count,
            std::log(10.0) * view.dlog_rho,
            std::log(10.0) * view.dlog_T);
        for (int field = 0; field < tabular_eos::FieldCount; ++field) {
            view.free_energy_fields[field] =
                h_free_energy_fields[field].data();
        }
        view.uses_free_energy = true;
        view.table_P = nullptr;
        view.table_E = nullptr;
        view.table_cs = nullptr;
        view.table_cv = nullptr;
        view.table_dP_drho = nullptr;
        view.table_dP_dT = nullptr;
    } else if (thermodynamic_model == "direct") {
        auto load = [&](const std::string& name, std::vector<double>& values) {
            if (!file.exist(name)) {
                throw std::runtime_error(
                    "Direct tabular EOS is missing dataset '" + name + "'");
            }
            auto dataset = file.getDataSet(name);
            validate_shape_4d(dataset, shape, name);
            values.resize(table_size);
            dataset.read(values.data());
            if (values.size() != table_size ||
                !std::all_of(values.begin(), values.end(),
                             [](double value) { return std::isfinite(value); })) {
                throw std::runtime_error(
                    "Direct tabular EOS dataset '" + name +
                    "' must contain only finite values");
            }
        };
        load("pressure", h_table_P);
        load("energy", h_table_E);
        load("sound_speed", h_table_cs);
        load("cv", h_table_cv);
        for (std::size_t i = 0; i < table_size; ++i) {
            if (!(h_table_P[i] > 0.0) || !(h_table_cs[i] > 0.0) ||
                !(h_table_cv[i] > 0.0)) {
                throw std::runtime_error(
                    "Direct tabular EOS requires positive pressure, sound_speed, and cv");
            }
        }
        for (int i = 0; i < view.n_rho; ++i) {
            for (int c = 0; c < composition_count; ++c) {
                for (int j = 0; j + 1 < view.n_T; ++j) {
                    const std::size_t lower =
                        (static_cast<std::size_t>(i) * view.n_T + j) *
                        composition_count + c;
                    if (!(h_table_E[lower + composition_count] >
                          h_table_E[lower])) {
                        throw std::runtime_error(
                            "Direct tabular EOS energy must increase strictly with temperature");
                    }
                }
            }
        }
        view.table_P = h_table_P.data();
        view.table_E = h_table_E.data();
        view.table_cs = h_table_cs.data();
        view.table_cv = h_table_cv.data();

        view.table_dP_drho = nullptr;
        view.table_dP_dT = nullptr;
        if (file.exist("dp_drho")) {
            load("dp_drho", h_table_dP_drho);
            view.table_dP_drho = h_table_dP_drho.data();
        }
        if (file.exist("dp_dT")) {
            load("dp_dT", h_table_dP_dT);
            view.table_dP_dT = h_table_dP_dT.data();
        }
    } else {
        throw std::runtime_error(
            "Unknown thermodynamic_model '" + thermodynamic_model +
            "'; expected 'free_energy' or 'direct'");
    }

    view.specs = specs_ptr;
    std::cout << "[Tabular4DEOS] Loaded "
              << (view.uses_free_energy ? "free-energy" : "direct")
              << " table with automatic rank validation." << std::endl;
}
