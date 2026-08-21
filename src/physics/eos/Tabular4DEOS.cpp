/**
 * @file Tabular4DEOS.cpp
 * @brief Validated 4D HDF5 EOS loader for (log10 rho, log10 T, Abar, Zbar).
 */

#include "Tabular4DEOS.h"
#include "TabularLoaderUtils.h"

#include <algorithm>
#include <cmath>

Tabular4DEOS::Tabular4DEOS(const std::string& h5_filename,
                           const SpeciesManager* specs_ptr)
    : table_path(h5_filename)
{
    std::cout << "[Tabular4DEOS] Loading validated 4D HDF5 table: "
              << h5_filename << std::endl;
    HighFive::File file(h5_filename, HighFive::File::ReadOnly);
    tabular_eos::loader::validate_schema_version(file);

    if (file.exist("table_rank")) {
        int rank = 0;
        file.getDataSet("table_rank").read(rank);
        if (rank != 4) {
            throw std::runtime_error(
                "Tabular4DEOS received a table whose table_rank is not 4");
        }
    }

    tabular_eos::loader::read_required(file, "n_rho", view.n_rho);
    tabular_eos::loader::read_required(file, "n_T", view.n_T);
    tabular_eos::loader::read_required(file, "n_A", view.n_A);
    tabular_eos::loader::read_required(file, "n_Z", view.n_Z);
    if (view.n_rho < 2 || view.n_T < 2 || view.n_A < 2 || view.n_Z < 2) {
        throw std::runtime_error(
            "A 4D tabular EOS requires at least two points on every axis");
    }

    tabular_eos::loader::read_required(file, "log_rho_min", view.log_rho_min);
    tabular_eos::loader::read_required(file, "log_rho_max", view.log_rho_max);
    tabular_eos::loader::read_required(file, "log_T_min", view.log_T_min);
    tabular_eos::loader::read_required(file, "log_T_max", view.log_T_max);
    tabular_eos::loader::read_required(file, "A_min", view.A_min);
    tabular_eos::loader::read_required(file, "A_max", view.A_max);
    tabular_eos::loader::read_required(file, "Z_min", view.Z_min);
    tabular_eos::loader::read_required(file, "Z_max", view.Z_max);
    tabular_eos::loader::validate_bounds(
        view.log_rho_min, view.log_rho_max, "log10_rho");
    tabular_eos::loader::validate_bounds(
        view.log_T_min, view.log_T_max, "log10_temperature");
    tabular_eos::loader::validate_bounds(view.A_min, view.A_max, "Abar");
    tabular_eos::loader::validate_bounds(view.Z_min, view.Z_max, "Zbar");

    view.dlog_rho =
        (view.log_rho_max - view.log_rho_min) / (view.n_rho - 1);
    view.dlog_T = (view.log_T_max - view.log_T_min) / (view.n_T - 1);
    view.dA = (view.A_max - view.A_min) / (view.n_A - 1);
    view.dZ = (view.Z_max - view.Z_min) / (view.n_Z - 1);
    tabular_eos::loader::warn_if_coarse_spacing(
        "Tabular4DEOS", view.dlog_rho, view.dlog_T);

    const std::vector<std::size_t> shape{
        static_cast<std::size_t>(view.n_rho),
        static_cast<std::size_t>(view.n_T),
        static_cast<std::size_t>(view.n_A),
        static_cast<std::size_t>(view.n_Z)
    };
    const int composition_count = view.n_A * view.n_Z;
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
            h_table_E, view.n_rho, view.n_T, composition_count);
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

    specs_owner = specs_ptr;
    view.specs = specs_ptr ? specs_ptr->get_host_view() : SpeciesHostView{};
    std::cout << "[Tabular4DEOS] Loaded "
              << (view.uses_free_energy ? "free-energy" : "direct")
              << " table with automatic rank validation." << std::endl;
}
