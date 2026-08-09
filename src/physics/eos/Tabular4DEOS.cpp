/**
 * @file Tabular4DEOS.cpp
 * @brief Implementation of the 4D Tabular EOS HDF5 reader.
 *
 * Workflow:
 * 1. Open the HDF5 EOS table in ReadOnly mode.
 * 2. Load the grid dimensions (n_rho, n_T, n_A, n_Z) and boundaries.
 * 3. Precompute step sizes (dlog_rho, dlog_T, dA, dZ).
 * 4. Load the core thermodynamic data tables (pressure, energy, sound_speed, cv).
 * 5. Optionally attempt to load analytical derivative tables.
 */

#include "Tabular4DEOS.h"
#include "highfive/H5File.hpp"
#include <iostream>

// =========================================================
// =================== Constructor =========================
// =========================================================

Tabular4DEOS::Tabular4DEOS(const std::string &h5_filename, const SpeciesManager *specs_ptr)
    : table_path(h5_filename)
{
    // =========================================================
    // 1. Initialize & Open File
    // =========================================================
    std::cout << "[Tabular4DEOS] Loading 4D HDF5 table: " << h5_filename << std::endl;
    HighFive::File file(h5_filename, HighFive::File::ReadOnly);

    // =========================================================
    // 2. Load Grid Dimensions
    // =========================================================
    file.getDataSet("n_rho").read(view.n_rho);
    file.getDataSet("n_T").read(view.n_T);
    file.getDataSet("n_A").read(view.n_A);
    file.getDataSet("n_Z").read(view.n_Z);

    // =========================================================
    // 3. Load Grid Boundaries
    // =========================================================
    file.getDataSet("log_rho_min").read(view.log_rho_min);
    file.getDataSet("log_rho_max").read(view.log_rho_max);
    file.getDataSet("log_T_min").read(view.log_T_min);
    file.getDataSet("log_T_max").read(view.log_T_max);
    file.getDataSet("A_min").read(view.A_min);
    file.getDataSet("A_max").read(view.A_max);
    file.getDataSet("Z_min").read(view.Z_min);
    file.getDataSet("Z_max").read(view.Z_max);

    view.dlog_rho = (view.log_rho_max - view.log_rho_min) / (view.n_rho - 1);
    view.dlog_T = (view.log_T_max - view.log_T_min) / (view.n_T - 1);
    view.dA = (view.A_max - view.A_min) / (view.n_A - 1);
    view.dZ = (view.Z_max - view.Z_min) / (view.n_Z - 1);

    // =========================================================
    // 4. Load Thermodynamic Tables
    // =========================================================
    file.getDataSet("pressure").read(h_table_P);
    file.getDataSet("energy").read(h_table_E);
    file.getDataSet("sound_speed").read(h_table_cs);
    file.getDataSet("cv").read(h_table_cv);

    // =========================================================
    // 5. Load Derivative Tables (Optional)
    // =========================================================
    try
    {
        file.getDataSet("dp_drho").read(h_table_dP_drho);
        view.table_dP_drho = h_table_dP_drho.data();
        file.getDataSet("dp_dT").read(h_table_dP_dT);
        view.table_dP_dT = h_table_dP_dT.data();
        std::cout << "[Tabular4DEOS] Loaded 4D analytical derivative tables." << std::endl;
    }
    catch (...)
    {
        view.table_dP_drho = nullptr;
        view.table_dP_dT = nullptr;
        std::cout << "[Tabular4DEOS] No derivative tables found. Falling back to finite difference." << std::endl;
    }

    // =========================================================
    // 6. Bind Pointers for Device View
    // =========================================================
    view.table_P = h_table_P.data();
    view.table_E = h_table_E.data();
    view.table_cs = h_table_cs.data();
    view.table_cv = h_table_cv.data();

    view.specs = specs_ptr;

    std::cout << "[Tabular4DEOS] 4D Table loaded successfully." << std::endl;
}
