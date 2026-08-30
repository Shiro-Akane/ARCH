/**
 * @file Tabular3DEOS.cpp
 * @brief Validated 3D HDF5 EOS loader for (log10 rho, log10 T, composition).
 */

#include "Tabular3DEOS.h"
#include "TabularLoaderUtils.h"

#include <algorithm>
#include <cmath>
#include <numeric>

Tabular3DEOS::Tabular3DEOS(const std::string& h5_filename,
                           const SpeciesManager* specs_ptr)
    : table_path(h5_filename)
{
    std::cout << "[Tabular3DEOS] Loading validated 3D HDF5 table: "
              << h5_filename << std::endl;
    HighFive::File file(h5_filename, HighFive::File::ReadOnly);
    tabular_eos::loader::validate_schema_version(file);

    if (file.exist("table_rank")) {
        int rank = 0;
        file.getDataSet("table_rank").read(rank);
        if (rank != 3) {
            throw std::runtime_error(
                "Tabular3DEOS received a table whose table_rank is not 3");
        }
    }

    tabular_eos::loader::read_required(file, "n_rho", view.n_rho);
    tabular_eos::loader::read_required(file, "n_T", view.n_T);
    tabular_eos::loader::read_required(file, "n_X", view.n_X);
    if (view.n_rho < 2 || view.n_T < 2 || view.n_X < 2) {
        throw std::runtime_error(
            "A 3D tabular EOS requires at least two points on every axis");
    }

    tabular_eos::loader::read_required(file, "log_rho_min", view.log_rho_min);
    tabular_eos::loader::read_required(file, "log_rho_max", view.log_rho_max);
    tabular_eos::loader::read_required(file, "log_T_min", view.log_T_min);
    tabular_eos::loader::read_required(file, "log_T_max", view.log_T_max);
    tabular_eos::loader::read_required(file, "X_min", view.X_min);
    tabular_eos::loader::read_required(file, "X_max", view.X_max);
    tabular_eos::loader::validate_bounds(
        view.log_rho_min, view.log_rho_max, "log10_rho");
    tabular_eos::loader::validate_bounds(
        view.log_T_min, view.log_T_max, "log10_temperature");
    tabular_eos::loader::validate_bounds(view.X_min, view.X_max, "composition");

    view.dlog_rho =
        (view.log_rho_max - view.log_rho_min) / (view.n_rho - 1);
    view.dlog_T = (view.log_T_max - view.log_T_min) / (view.n_T - 1);
    view.dX = (view.X_max - view.X_min) / (view.n_X - 1);
    tabular_eos::loader::warn_if_coarse_spacing(
        "Tabular3DEOS", view.dlog_rho, view.dlog_T);

    const std::vector<std::size_t> shape{
        static_cast<std::size_t>(view.n_rho),
        static_cast<std::size_t>(view.n_T),
        static_cast<std::size_t>(view.n_X)
    };
    std::string thermodynamic_model = "direct";
    if (file.exist("thermodynamic_model")) {
        file.getDataSet("thermodynamic_model").read(thermodynamic_model);
    }

    if (thermodynamic_model == "free_energy") {
        if (view.n_rho < 5 || view.n_T < 5) {
            throw std::runtime_error(
                "Free-energy tables require at least five rho and temperature points");
        }
        std::vector<double> free_energy =
            tabular_eos::loader::read_table_field(file, "free_energy", shape);
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
        h_table_P = tabular_eos::loader::read_table_field(
            file, "pressure", shape);
        h_table_E = tabular_eos::loader::read_table_field(
            file, "energy", shape);
        h_table_cs = tabular_eos::loader::read_table_field(
            file, "sound_speed", shape);
        h_table_cv = tabular_eos::loader::read_table_field(
            file, "cv", shape);
        tabular_eos::loader::validate_direct_thermodynamics(
            h_table_P, h_table_E, h_table_cs, h_table_cv);
        tabular_eos::loader::validate_energy_increases_with_temperature(
            h_table_E, view.n_rho, view.n_T, view.n_X);
        view.table_P = h_table_P.data();
        view.table_E = h_table_E.data();
        view.table_cs = h_table_cs.data();
        view.table_cv = h_table_cv.data();

        view.table_dP_drho = nullptr;
        view.table_dP_dT = nullptr;
        if (file.exist("dp_drho")) {
            h_table_dP_drho = tabular_eos::loader::read_table_field(
                file, "dp_drho", shape);
            view.table_dP_drho = h_table_dP_drho.data();
        }
        if (file.exist("dp_dT")) {
            h_table_dP_dT = tabular_eos::loader::read_table_field(
                file, "dp_dT", shape);
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
