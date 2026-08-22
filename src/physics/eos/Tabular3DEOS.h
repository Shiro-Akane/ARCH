/**
 * @file Tabular3DEOS.h
 * @brief Tabular Equation of State reading from HDF5.
 * Implements Host-Device View pattern for CUDA compatibility
 * and provides thermodynamic states (T, Ye) for nuclear reaction networks.
 */

/**
 * Workflow:
 * 1. Construct or query the configured thermodynamic closure from canonical state variables.
 * 2. Return pressure, temperature, and transport quantities with validated bounds.
 * 3. Keep host and future device views consistent through one dispatch contract.
 */
#pragma once

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "eos.h"
#include "eos_Utils.h"
#include "TabularFreeEnergy.h"

#include "../species/Species.h"

// Non-owning table view suitable for host/device numerical kernels.
struct Tabular3DEOSView
{
    // Table dimensions and logarithmic coordinate bounds.
    int n_rho, n_T, n_X;
    double log_rho_min, log_rho_max, dlog_rho;
    double log_T_min, log_T_max, dlog_T;
    double X_min, X_max, dX; // Range of Mass fraction X

    // Non-owning pointers; a device backend must place the data in accessible memory.
    const double *table_P;
    const double *table_E;
    const double *table_cs;
    const double *table_cv;

    double *table_dP_drho; // Optional precomputed pressure-density derivative.
    double *table_dP_dT;   // Optional precomputed pressure-temperature derivative.

    bool uses_free_energy = false;
    std::array<const double*, tabular_eos::FieldCount> free_energy_fields{};

    const SpeciesManager *specs;

    int target_species_id;

    // Boltzmann constant in CGS units, erg/K.
    static constexpr double k_B_cgs = 1.380649e-16; // erg/K
    static constexpr double m_u_cgs = 1.660539e-24; // g

    // Domain checks and analytic ideal-gas fallback.

    bool is_out_of_bounds(double log_rho, double log_T, double X) const
    {
        return (log_rho < log_rho_min || log_rho > log_rho_max ||
                log_T < log_T_min || log_T > log_T_max ||
                X < X_min || X > X_max);
    }

    double fallback_gamma() const { return 5.0 / 3.0; } // Monatomic ideal-gas fallback.

    double fallback_pressure(double rho, double e) const
    {
        return rho * e * (fallback_gamma() - 1.0);
    }

    double fallback_temperature(double e, const double *Xi) const
    {
        // Abar=1 represents pure hydrogen when no SpeciesManager is attached.
        double Abar = (specs && specs->count() > 0) ? specs->calc_Abar(Xi) : 1.0;
        double R_spec = k_B_cgs / (Abar * m_u_cgs);
        return e * (fallback_gamma() - 1.0) / R_spec;
    }

    double fallback_sound_speed(double rho, double e) const
    {
        double p = fallback_pressure(rho, e);
        return std::sqrt(fallback_gamma() * p / rho);
    }

    // Trilinear interpolation in log(rho), log(T), and composition coordinate.
    double interpolate_3d(const double *table, double rho, double T, double X) const
    {
        if (rho <= 1e-12 || T <= 1e-12)
            return 0.0;

        double x = log10(rho);
        double y = log10(T);
        double z = X;

        // Compute the enclosing lower grid vertex.
        int i = static_cast<int>((x - log_rho_min) / dlog_rho);
        int j = static_cast<int>((y - log_T_min) / dlog_T);
        int k = static_cast<int>((z - X_min) / dX);

        // Clamp indices so every lower vertex has a valid upper neighbor.
        i = std::max(0, std::min(i, n_rho - 2));
        j = std::max(0, std::min(j, n_T - 2));
        k = std::max(0, std::min(k, n_X - 2));

        // Fractional offsets within the enclosing cell, nominally in [0,1).
        double tx = (x - (log_rho_min + i * dlog_rho)) / dlog_rho;
        double ty = (y - (log_T_min + j * dlog_T)) / dlog_T;
        double tz = (z - (X_min + k * dX)) / dX;

// Flatten (i,j,k) as i*(n_T*n_X)+j*n_X+k.
#define IDX(ii, jj, kk) ((ii) * n_T * n_X + (jj) * n_X + (kk))

        // Load the eight cell vertices.
        double c000 = table[IDX(i, j, k)];
        double c100 = table[IDX(i + 1, j, k)];
        double c010 = table[IDX(i, j + 1, k)];
        double c110 = table[IDX(i + 1, j + 1, k)];
        double c001 = table[IDX(i, j, k + 1)];
        double c101 = table[IDX(i + 1, j, k + 1)];
        double c011 = table[IDX(i, j + 1, k + 1)];
        double c111 = table[IDX(i + 1, j + 1, k + 1)];
#undef IDX

        // Interpolate along log-density.
        double c00 = c000 * (1.0 - tx) + c100 * tx;
        double c01 = c001 * (1.0 - tx) + c101 * tx;
        double c10 = c010 * (1.0 - tx) + c110 * tx;
        double c11 = c011 * (1.0 - tx) + c111 * tx;

        // Interpolate along log-temperature.
        double c0 = c00 * (1.0 - ty) + c10 * ty;
        double c1 = c01 * (1.0 - ty) + c11 * ty;

        // Interpolate along composition to obtain the final value.
        return c0 * (1.0 - tz) + c1 * tz;
    }

    tabular_eos::FreeEnergyState interpolate_free_energy(
        double rho, double T, double composition) const
    {
        const double log_rho = std::log10(rho);
        const double log_temperature = std::log10(T);
        int i = static_cast<int>((log_rho - log_rho_min) / dlog_rho);
        int j = static_cast<int>((log_temperature - log_T_min) / dlog_T);
        int k = static_cast<int>((composition - X_min) / dX);
        i = std::max(0, std::min(i, n_rho - 2));
        j = std::max(0, std::min(j, n_T - 2));
        k = std::max(0, std::min(k, n_X - 2));

        const double tx =
            (log_rho - (log_rho_min + i * dlog_rho)) / dlog_rho;
        const double ty =
            (log_temperature - (log_T_min + j * dlog_T)) / dlog_T;
        const double tc = (composition - (X_min + k * dX)) / dX;
        const std::size_t rho_stride =
            static_cast<std::size_t>(n_T) * n_X;
        auto index = [&](int irho, int itemperature, int icomposition) {
            return static_cast<std::size_t>(irho) * rho_stride +
                   static_cast<std::size_t>(itemperature) * n_X +
                   icomposition;
        };
        auto at_composition = [&](int kc) {
            const std::array<std::size_t, 4> corners{
                index(i, j, kc), index(i, j + 1, kc),
                index(i + 1, j, kc), index(i + 1, j + 1, kc)
            };
            return tabular_eos::interpolate_biquintic(
                free_energy_fields, corners, tx, ty,
                std::log(10.0) * dlog_rho,
                std::log(10.0) * dlog_T);
        };
        return tabular_eos::blend(
            at_composition(k), at_composition(k + 1), tc);
    }

    tabular_eos::ThermodynamicState free_energy_state(
        double rho, double T, double composition) const
    {
        return tabular_eos::to_thermodynamics(
            interpolate_free_energy(rho, T, composition), rho, T);
    }

    // Composition-coordinate query used by generated network interfaces.

    double get_target_X(const double *Xi) const
    {
        // An explicit target species selects its mass fraction directly.
        if (target_species_id >= 0)
        {
            return Xi[target_species_id];
        }

        // Otherwise derive electron fraction Ye when species metadata is available.
        if (specs && specs->count() > 0)
        {
            return specs->calc_Ye(Xi);
        }

        // Ye=0.5 is the neutral symmetric-matter fallback without metadata.
        return 0.5;
    }

    double get_pressure_from_rho_T(double rho, double T, const double *Xi) const
    {
        if (rho <= 1e-12 || T <= 1e-12)
            return 0.0;
        double X = get_target_X(Xi);
        if (is_out_of_bounds(std::log10(rho), std::log10(T), X))
        {
            double e = get_eint_from_T(rho, T, Xi);
            return fallback_pressure(rho, e);
        }
        return uses_free_energy ? free_energy_state(rho, T, X).pressure :
               interpolate_3d(table_P, rho, T, X);
    }

    double get_pressure_from_rho_e(double rho, double e, const double *Xi) const
    {
        if (rho <= 1e-12 || e <= 1e-12)
            return 0.0;
        double T = get_temperature(rho, e, Xi);
        return get_pressure_from_rho_T(rho, T, Xi);
    }

    double get_eint_from_T(double rho, double T_target, const double *Xi) const
    {
        if (rho <= 1e-12 || T_target <= 1e-12)
            return 0.0;

        double X = get_target_X(Xi);

        if (is_out_of_bounds(std::log10(rho), std::log10(T_target), X))
        {
            double Abar = (specs && specs->count() > 0) ? specs->calc_Abar(Xi) : 1.0;
            double R_spec = k_B_cgs / (Abar * m_u_cgs);
            return T_target * R_spec / (fallback_gamma() - 1.0);
        }

        return uses_free_energy ? free_energy_state(rho, T_target, X).energy :
               interpolate_3d(table_E, rho, T_target, X);
    }

    double get_cv(double rho, double T_target, const double *Xi) const
    {
        if (rho <= 1e-12 || T_target <= 1e-12)
            return 0.0;

        double X = get_target_X(Xi);

        if (is_out_of_bounds(std::log10(rho), std::log10(T_target), X))
        {
            double Abar = (specs && specs->count() > 0) ? specs->calc_Abar(Xi) : 1.0;
            double R_spec = k_B_cgs / (Abar * m_u_cgs);
            return R_spec / (fallback_gamma() - 1.0);
        }

        return uses_free_energy ? free_energy_state(rho, T_target, X).cv :
               interpolate_3d(table_cv, rho, T_target, X);
    }

    double get_temperature(double rho, double e, const double *Xi) const
    {
        if (rho <= 1e-12 || e <= 1e-12)
            return 0.0;

        double X = get_target_X(Xi);
        double T_min = std::pow(10, log_T_min);
        double T_max = std::pow(10, log_T_max);

        // Out of bounds check for density or composition
        if (std::log10(rho) < log_rho_min || std::log10(rho) > log_rho_max ||
            X < X_min || X > X_max)
        {
            return fallback_temperature(e, Xi);
        }

        // Fast boundary check: if e is below the minimum table energy, return T_min
        double e_min_table = uses_free_energy ? free_energy_state(rho, T_min, X).energy :
                             interpolate_3d(table_E, rho, T_min, X);
        if (e <= e_min_table) {
            return T_min;
        }

        // Newton-Raphson iteration
        double T_guess = 1e8; // reasonable astrophysics start
        const int max_iters = 20;
        const double tol = 1e-6;

        for (int i = 0; i < max_iters; ++i) {
            T_guess = std::max(T_min, std::min(T_guess, T_max));

            double e_eval = uses_free_energy ? free_energy_state(rho, T_guess, X).energy :
                            interpolate_3d(table_E, rho, T_guess, X);
            double cv_eval = uses_free_energy ? free_energy_state(rho, T_guess, X).cv :
                             interpolate_3d(table_cv, rho, T_guess, X);

            if (cv_eval <= 0.0) {
                // Finite difference fallback
                double dT_fd = T_guess * 0.01;
                double e_plus = uses_free_energy ?
                    free_energy_state(rho, T_guess + dT_fd, X).energy :
                    interpolate_3d(table_E, rho, T_guess + dT_fd, X);
                cv_eval = (e_plus - e_eval) / dT_fd;
                if (cv_eval <= 0.0) cv_eval = e_eval / T_guess;
            }

            double f = e_eval - e;
            double dT = -f / cv_eval;

            // Limit step size to avoid divergence (max 50% change)
            if (dT > 0.5 * T_guess) dT = 0.5 * T_guess;
            if (dT < -0.5 * T_guess) dT = -0.5 * T_guess;

            T_guess += dT;

            if (std::abs(dT) / T_guess < tol) break;
        }

        return T_guess;
    }

    double get_pressure(const FluidVector &U, const double *Xi) const
    {
        double e_int = eos_utils::extract_specific_internal_energy(U);
        return get_pressure_from_rho_e(U.rho, e_int, Xi);
    }

    double get_sound_speed(const FluidVector &U, double p, const double *Xi) const
    {
        double e_int = eos_utils::extract_specific_internal_energy(U);
        if (U.rho <= 1e-12 || e_int <= 1e-12)
            return 0.0;

        double X = get_target_X(Xi);
        double T = get_temperature(U.rho, e_int, Xi);
        if (is_out_of_bounds(std::log10(U.rho), std::log10(T), X))
        {
            return fallback_sound_speed(U.rho, e_int);
        }
        return uses_free_energy ? free_energy_state(U.rho, T, X).sound_speed :
               interpolate_3d(table_cs, U.rho, T, X);
    }

    double get_gamma(const double *Xi, double rho = 0.0, double e = 0.0) const
    {
        if (rho < 1e-12 || e < 1e-12)
            return fallback_gamma();
        double p = get_pressure_from_rho_e(rho, e, Xi);
        if (p < 1e-12)
            return fallback_gamma();

        double X = get_target_X(Xi);
        double T = get_temperature(rho, e, Xi);
        const double sound_speed = get_sound_speed_from_rho_T(rho, T, Xi);
        return rho * sound_speed * sound_speed / p;
    }

    double get_sound_speed_from_rho_T(double rho, double T, const double *Xi) const
    {
        double X = get_target_X(Xi);
        if (is_out_of_bounds(std::log10(rho), std::log10(T), X))
        {
            double e = get_eint_from_T(rho, T, Xi);
            return fallback_sound_speed(rho, e);
        }
        return uses_free_energy ? free_energy_state(rho, T, X).sound_speed :
               interpolate_3d(table_cs, rho, T, X);
    }

    // Derivative interface using table data when present and finite differences otherwise.
    double get_dp_drho_e(double rho, double e, const double *Xi) const
    {
        double X = get_target_X(Xi);
        double T = get_temperature(rho, e, Xi);
        if (is_out_of_bounds(std::log10(rho), std::log10(T), X))
        {
            return e * (fallback_gamma() - 1.0); // Ideal-gas (dP/drho)_e.
        }

        if (uses_free_energy)
            return free_energy_state(rho, T, X).dp_drho_e;
        if (table_dP_drho)
            return interpolate_3d(table_dP_drho, rho, T, X);

        double drho = rho * 0.001;
        return (interpolate_3d(table_P, rho + drho, T, X) -
                interpolate_3d(table_P, rho - drho, T, X)) /
               (2.0 * drho);
    }

    double get_dp_de_rho(double rho, double e, const double *Xi) const
    {
        double X = get_target_X(Xi);
        double T = get_temperature(rho, e, Xi);
        if (is_out_of_bounds(std::log10(rho), std::log10(T), X))
        {
            return rho * (fallback_gamma() - 1.0); // Ideal-gas (dP/de)_rho.
        }
        if (uses_free_energy)
            return free_energy_state(rho, T, X).dp_de_rho;
        if (table_dP_dT && table_cv) {
            double dp_dT = interpolate_3d(table_dP_dT, rho, T, X);
            double cv = interpolate_3d(table_cv, rho, T, X);
            if (cv > 0.0) return dp_dT / cv;
        }

        double de = e * 0.001;
        double T_plus = get_temperature(rho, e + de, Xi);
        double T_minus = get_temperature(rho, e - de, Xi);
        return (interpolate_3d(table_P, rho, T_plus, X) -
                interpolate_3d(table_P, rho, T_minus, X)) /
               (2.0 * de);
    }
    // Damped Newton inversion from pressure to total-energy density.
    double get_total_energy_primitive(double rho, double u, double v, double w, double p, const double *Xi) const
    {
        return eos_utils::solve_total_energy(*this, rho, u, v, w, p, Xi);
    }

    double get_eta(double rho, double T, const double* Xi) const { return 0.0; }

    // Pipeline: evaluate_state
    void evaluate_state(eos_state_t& state) const {
        // 1. Core Thermodynamics (P, E, cv)
        state.P = get_pressure_from_rho_T(state.rho, state.T, state.Xi);
        state.E = get_eint_from_T(state.rho, state.T, state.Xi);
        state.cv = get_cv(state.rho, state.T, state.Xi);

        // 2. Derivatives and Sound Speed
        state.sound_speed = get_sound_speed_from_rho_T(state.rho, state.T, state.Xi);
        state.dp_drho = get_dp_drho_e(state.rho, state.E, state.Xi);
        const double X = get_target_X(state.Xi);
        const bool use_fallback = is_out_of_bounds(
            std::log10(state.rho), std::log10(state.T), X);
        if (use_fallback) {
            const double Abar = (specs && specs->count() > 0)
                ? specs->calc_Abar(state.Xi) : 1.0;
            const double R_spec = k_B_cgs / (Abar * m_u_cgs);
            state.dp_dT = state.rho * R_spec;
        } else if (uses_free_energy) {
            state.dp_dT =
                free_energy_state(state.rho, state.T, X).dp_dT;
        } else if (table_dP_dT) {
            state.dp_dT = interpolate_3d(table_dP_dT, state.rho, state.T, X);
        } else {
            const double dT = std::max(std::abs(state.T) * 1.0e-4, 1.0e-8);
            const double table_T_min = std::pow(10.0, log_T_min);
            const double table_T_max = std::pow(10.0, log_T_max);
            const double lower_T = std::max(state.T - dT, table_T_min);
            const double upper_T = std::min(state.T + dT, table_T_max);
            const double lower_P = interpolate_3d(
                table_P, state.rho, lower_T, X);
            const double upper_P = interpolate_3d(
                table_P, state.rho, upper_T, X);
            state.dp_dT = (upper_P - lower_P) / (upper_T - lower_T);
        }

        // 3. Deep Physical Variables (Unused in Tabular)
        state.pele = 0.0;
        state.xne = 0.0;
        state.eta = 0.0;
    }

    const SpeciesManager* get_species_manager() const { return specs; }
};

// Host owner for HDF5 loading and table lifetime; never used in cell kernels.
struct Tabular3DEOS : public EOSBase
{
private:
    std::string table_path;
    std::vector<double> h_table_P;
    std::vector<double> h_table_E;
    std::vector<double> h_table_cs;
    std::vector<double> h_table_cv;

    // Reserved host derivative arrays matching the view's optional pointers.
    std::vector<double> h_table_dP_drho;
    std::vector<double> h_table_dP_dT;

    std::array<std::vector<double>, tabular_eos::FieldCount> h_free_energy_fields;

    Tabular3DEOSView view;

public:
    Tabular3DEOS(const std::string &h5_filename, const SpeciesManager *specs_ptr = nullptr);

    // Solver dispatch receives only the lightweight non-owning view.
    Tabular3DEOSView get_view() const { return view; }

    const SpeciesManager* get_species_manager() const { return view.specs; }
};
