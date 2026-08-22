/**
 * @file Tabular3DEOS.cpp
 * @brief Validated 3D HDF5 EOS loader for (log10 rho, log10 T, composition).
 */

#include "Tabular3DEOS.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>

#include <highfive/H5File.hpp>

namespace {

template <typename T>
void read_required(const HighFive::File& file, const std::string& name, T& value)
{
    if (!file.exist(name)) {
        throw std::runtime_error("Tabular EOS is missing required dataset '" + name + "'");
    }
    file.getDataSet(name).read(value);
}

void validate_shape(const HighFive::DataSet& dataset,
                    const std::vector<std::size_t>& expected,
                    const std::string& name)
{
    const auto actual = dataset.getSpace().getDimensions();
    if (actual != expected) {
        throw std::runtime_error(
            "Tabular EOS dataset '" + name + "' has a shape inconsistent with table_rank");
    }
}

void validate_bounds(double lower, double upper, const std::string& name)
{
    if (!std::isfinite(lower) || !std::isfinite(upper) || !(upper > lower)) {
        throw std::runtime_error(
            "Tabular EOS axis '" + name + "' must have finite, increasing bounds");
    }
}

} // namespace

Tabular3DEOS::Tabular3DEOS(const std::string& h5_filename,
                           const SpeciesManager* specs_ptr)
    : table_path(h5_filename)
{
    std::cout << "[Tabular3DEOS] Loading validated 3D HDF5 table: "
              << h5_filename << std::endl;
    HighFive::File file(h5_filename, HighFive::File::ReadOnly);

    if (file.exist("table_rank")) {
        int rank = 0;
        file.getDataSet("table_rank").read(rank);
        if (rank != 3) {
            throw std::runtime_error(
                "Tabular3DEOS received a table whose table_rank is not 3");
        }
    }

    read_required(file, "n_rho", view.n_rho);
    read_required(file, "n_T", view.n_T);
    read_required(file, "n_X", view.n_X);
    if (view.n_rho < 2 || view.n_T < 2 || view.n_X < 2) {
        throw std::runtime_error(
            "A 3D tabular EOS requires at least two points on every axis");
    }

    read_required(file, "log_rho_min", view.log_rho_min);
    read_required(file, "log_rho_max", view.log_rho_max);
    read_required(file, "log_T_min", view.log_T_min);
    read_required(file, "log_T_max", view.log_T_max);
    read_required(file, "X_min", view.X_min);
    read_required(file, "X_max", view.X_max);
    validate_bounds(view.log_rho_min, view.log_rho_max, "log10_rho");
    validate_bounds(view.log_T_min, view.log_T_max, "log10_temperature");
    validate_bounds(view.X_min, view.X_max, "composition");

    view.dlog_rho =
        (view.log_rho_max - view.log_rho_min) / (view.n_rho - 1);
    view.dlog_T = (view.log_T_max - view.log_T_min) / (view.n_T - 1);
    view.dX = (view.X_max - view.X_min) / (view.n_X - 1);
    if (view.dlog_rho > 0.1 || view.dlog_T > 0.1) {
        std::cerr
            << "[Tabular3DEOS] Warning: thermodynamic spacing exceeds 0.1 dex; "
               "refine and run the error analyzer before production use."
            << std::endl;
    }

    const std::vector<std::size_t> shape{
        static_cast<std::size_t>(view.n_rho),
        static_cast<std::size_t>(view.n_T),
        static_cast<std::size_t>(view.n_X)
    };
    const std::size_t table_size =
        static_cast<std::size_t>(view.n_rho) * view.n_T * view.n_X;

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
        validate_shape(dataset, shape, "free_energy");
        std::vector<double> free_energy(table_size);
        dataset.read(free_energy.data());
        if (free_energy.size() != table_size ||
            !std::all_of(free_energy.begin(), free_energy.end(),
                         [](double value) { return std::isfinite(value); })) {
            throw std::runtime_error(
                "free_energy must contain only finite specific energies in erg/g");
        }
        h_free_energy_fields = tabular_eos::build_derivative_fields(
            free_energy, view.n_rho, view.n_T, view.n_X,
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
            validate_shape(dataset, shape, name);
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
            for (int k = 0; k < view.n_X; ++k) {
                for (int j = 0; j + 1 < view.n_T; ++j) {
                    const std::size_t lower =
                        (static_cast<std::size_t>(i) * view.n_T + j) *
                        view.n_X + k;
                    if (!(h_table_E[lower + view.n_X] > h_table_E[lower])) {
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
    view.target_species_id = -1;
    if (file.exist("composition_axis")) {
        std::string composition_axis;
        file.getDataSet("composition_axis").read(composition_axis);
        constexpr const char* species_prefix = "species:";
        if (composition_axis.rfind(species_prefix, 0) == 0) {
            if (specs_ptr == nullptr) {
                throw std::runtime_error(
                    "A species composition_axis requires a SpeciesManager");
            }
            const std::string species_name =
                composition_axis.substr(std::char_traits<char>::length(species_prefix));
            view.target_species_id = specs_ptr->GetSpeciesID(species_name);
            if (view.target_species_id < 0) {
                throw std::runtime_error(
                    "composition_axis names an unregistered species: " + species_name);
            }
        } else if (composition_axis != "Ye") {
            throw std::runtime_error(
                "3D composition_axis must be 'Ye' or 'species:<registered-name>'");
        }
    }

    std::cout << "[Tabular3DEOS] Loaded "
              << (view.uses_free_energy ? "free-energy" : "direct")
              << " table with automatic rank validation." << std::endl;
}
