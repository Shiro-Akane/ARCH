/**
 * @file Tabular4DEOS.h
 * @brief 4D Tabular Equation of State reading from HDF5 (rho, e, A_bar, Z_bar).
 * Designed specifically for Non-NSE astrophysical environments (e.g., Helmholtz EOS).
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

// Non-owning view for 16-vertex quadrilinear interpolation.
struct Tabular4DEOSView
{
    // Table dimensions and coordinate bounds for rho, energy, Abar, and Zbar.
    int n_rho, n_T, n_A, n_Z;
    double log_rho_min, log_rho_max, dlog_rho;
    double log_T_min, log_T_max, dlog_T;
    double A_min, A_max, dA;
    double Z_min, Z_max, dZ;

    // Non-owning table pointers.
    const double *table_P;
    const double *table_E;
    const double *table_cs;
    const double *table_cv;

    double *table_dP_drho;
    double *table_dP_dT;

    bool uses_free_energy = false;
    std::array<const double*, tabular_eos::FieldCount> free_energy_fields{};

    const SpeciesManager *specs;

    static constexpr double k_B_cgs = 1.380649e-16; // erg/K
    static constexpr double m_u_cgs = 1.660539e-24; // g

    // Domain check and analytic ideal-gas fallback.
    bool is_out_of_bounds(double log_rho, double log_T, double A, double Z) const
    {
        return (log_rho < log_rho_min || log_rho > log_rho_max ||
                log_T < log_T_min || log_T > log_T_max ||
                A < A_min || A > A_max ||
                Z < Z_min || Z > Z_max);
    }

    // Monatomic ideal-gas ratio used only outside the tabulated domain.
    double fallback_gamma() const { return 5.0 / 3.0; }

    double fallback_pressure(double rho, double e) const
    {
        return rho * e * (fallback_gamma() - 1.0);
    }

    double fallback_temperature(double e, double Abar) const
    {
        double R_spec = k_B_cgs / (Abar * m_u_cgs);
        return e * (fallback_gamma() - 1.0) / R_spec;
    }

    double fallback_sound_speed(double rho, double e) const
    {
        double p = fallback_pressure(rho, e);
        return std::sqrt(fallback_gamma() * p / rho);
    }

    // Quadrilinear interpolation.
    double interpolate_4d(const double *table, double rho, double T, double A, double Z) const
    {
        if (rho <= 1e-12 || T <= 1e-12)
            return 0.0;

        double x = log10(rho);
        double y = log10(T);
        double u = A;
        double v = Z;

        // The public query checks bounds; this kernel requires x, y, u, and v
        // to lie inside the table.
        int i = static_cast<int>((x - log_rho_min) / dlog_rho);
        int j = static_cast<int>((y - log_T_min) / dlog_T);
        int k = static_cast<int>((u - A_min) / dA);
        int l = static_cast<int>((v - Z_min) / dZ);

        // Clamp lower vertices so roundoff cannot invalidate a +1 neighbor.
        i = std::max(0, std::min(i, n_rho - 2));
        j = std::max(0, std::min(j, n_T - 2));
        k = std::max(0, std::min(k, n_A - 2));
        l = std::max(0, std::min(l, n_Z - 2));

        // Fractional offsets within the enclosing four-dimensional cell.
        double tx = (x - (log_rho_min + i * dlog_rho)) / dlog_rho;
        double ty = (y - (log_T_min + j * dlog_T)) / dlog_T;
        double tu = (u - (A_min + k * dA)) / dA;
        double tv = (v - (Z_min + l * dZ)) / dZ;

// Flatten (i,j,k,l) as i*(n_T*n_A*n_Z)+j*(n_A*n_Z)+k*n_Z+l.
#define IDX(ii, jj, kk, ll) ((ii) * n_T * n_A * n_Z + (jj) * n_A * n_Z + (kk) * n_Z + (ll))

        // Collapse 16 vertices to 8 along Zbar.
        double c000 = table[IDX(i, j, k, l)] * (1.0 - tv) + table[IDX(i, j, k, l + 1)] * tv;
        double c100 = table[IDX(i + 1, j, k, l)] * (1.0 - tv) + table[IDX(i + 1, j, k, l + 1)] * tv;
        double c010 = table[IDX(i, j + 1, k, l)] * (1.0 - tv) + table[IDX(i, j + 1, k, l + 1)] * tv;
        double c110 = table[IDX(i + 1, j + 1, k, l)] * (1.0 - tv) + table[IDX(i + 1, j + 1, k, l + 1)] * tv;
        double c001 = table[IDX(i, j, k + 1, l)] * (1.0 - tv) + table[IDX(i, j, k + 1, l + 1)] * tv;
        double c101 = table[IDX(i + 1, j, k + 1, l)] * (1.0 - tv) + table[IDX(i + 1, j, k + 1, l + 1)] * tv;
        double c011 = table[IDX(i, j + 1, k + 1, l)] * (1.0 - tv) + table[IDX(i, j + 1, k + 1, l + 1)] * tv;
        double c111 = table[IDX(i + 1, j + 1, k + 1, l)] * (1.0 - tv) + table[IDX(i + 1, j + 1, k + 1, l + 1)] * tv;
#undef IDX

        // Collapse 8 values to 4 along Abar.
        double c00 = c000 * (1.0 - tu) + c001 * tu;
        double c10 = c100 * (1.0 - tu) + c101 * tu;
        double c01 = c010 * (1.0 - tu) + c011 * tu;
        double c11 = c110 * (1.0 - tu) + c111 * tu;

        // Collapse 4 values to 2 along specific internal energy.
        double c0 = c00 * (1.0 - ty) + c01 * ty;
        double c1 = c10 * (1.0 - ty) + c11 * ty;

        // Collapse the final pair along density.
        return c0 * (1.0 - tx) + c1 * tx;
    }

    tabular_eos::FreeEnergyState interpolate_free_energy(
        double rho, double T, double A, double Z) const
    {
        const double log_rho = std::log10(rho);
        const double log_temperature = std::log10(T);
        int i = static_cast<int>((log_rho - log_rho_min) / dlog_rho);
        int j = static_cast<int>((log_temperature - log_T_min) / dlog_T);
        int k = static_cast<int>((A - A_min) / dA);
        int l = static_cast<int>((Z - Z_min) / dZ);
        i = std::max(0, std::min(i, n_rho - 2));
        j = std::max(0, std::min(j, n_T - 2));
        k = std::max(0, std::min(k, n_A - 2));
        l = std::max(0, std::min(l, n_Z - 2));

        const double tx =
            (log_rho - (log_rho_min + i * dlog_rho)) / dlog_rho;
        const double ty =
            (log_temperature - (log_T_min + j * dlog_T)) / dlog_T;
        const double ta = (A - (A_min + k * dA)) / dA;
        const double tz = (Z - (Z_min + l * dZ)) / dZ;
        const std::size_t composition_count =
            static_cast<std::size_t>(n_A) * n_Z;
        const std::size_t rho_stride =
            static_cast<std::size_t>(n_T) * composition_count;
        auto index = [&](int irho, int itemperature, int ia, int iz) {
            return static_cast<std::size_t>(irho) * rho_stride +
                   static_cast<std::size_t>(itemperature) * composition_count +
                   static_cast<std::size_t>(ia) * n_Z + iz;
        };
        auto at_composition = [&](int ka, int kz) {
            const std::array<std::size_t, 4> corners{
                index(i, j, ka, kz), index(i, j + 1, ka, kz),
                index(i + 1, j, ka, kz), index(i + 1, j + 1, ka, kz)
            };
            return tabular_eos::interpolate_biquintic(
                free_energy_fields, corners, tx, ty,
                std::log(10.0) * dlog_rho,
                std::log(10.0) * dlog_T);
        };
        const auto lower_A = tabular_eos::blend(
            at_composition(k, l), at_composition(k, l + 1), tz);
        const auto upper_A = tabular_eos::blend(
            at_composition(k + 1, l), at_composition(k + 1, l + 1), tz);
        return tabular_eos::blend(lower_A, upper_A, ta);
    }

    tabular_eos::ThermodynamicState free_energy_state(
        double rho, double T, double A, double Z) const
    {
        return tabular_eos::to_thermodynamics(
            interpolate_free_energy(rho, T, A, Z), rho, T);
    }

    // Composition coordinates Abar and Zbar.

    double get_Abar(const double *Xi) const
    {
        if (specs && specs->count() > 0)
            return specs->calc_Abar(Xi);
        return 14.0; // Pure-nitrogen fallback when species metadata is absent.
    }

    double get_Zbar(const double *Xi) const
    {
        if (specs && specs->count() > 0)
            return specs->calc_Zbar(Xi);
        return 7.0; // Matches the pure-nitrogen Abar fallback above.
    }

    double get_pressure_from_rho_T(double rho, double T, const double *Xi) const
    {
        if (rho <= 1e-12 || T <= 1e-12)
            return 0.0;
        double A = get_Abar(Xi), Z = get_Zbar(Xi);
        if (is_out_of_bounds(std::log10(rho), std::log10(T), A, Z))
        {
            double e = get_eint_from_T(rho, T, Xi);
            return fallback_pressure(rho, e);
        }
        return uses_free_energy ? free_energy_state(rho, T, A, Z).pressure :
               interpolate_4d(table_P, rho, T, A, Z);
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

        double A = get_Abar(Xi);
        double Z = get_Zbar(Xi);

        if (is_out_of_bounds(std::log10(rho), std::log10(T_target), A, Z))
        {
            double R_spec = k_B_cgs / (A * m_u_cgs);
            return T_target * R_spec / (fallback_gamma() - 1.0);
        }

        return uses_free_energy ? free_energy_state(rho, T_target, A, Z).energy :
               interpolate_4d(table_E, rho, T_target, A, Z);
    }

    double get_cv(double rho, double T_target, const double *Xi) const
    {
        if (rho <= 1e-12 || T_target <= 1e-12)
            return 0.0;

        double A = get_Abar(Xi);
        double Z = get_Zbar(Xi);

        if (is_out_of_bounds(std::log10(rho), std::log10(T_target), A, Z))
        {
            double R_spec = k_B_cgs / (A * m_u_cgs);
            return R_spec / (fallback_gamma() - 1.0);
        }

        return uses_free_energy ? free_energy_state(rho, T_target, A, Z).cv :
               interpolate_4d(table_cv, rho, T_target, A, Z);
    }

    double get_temperature(double rho, double e, const double *Xi) const
    {
        if (rho <= 1e-12 || e <= 1e-12)
            return 0.0;

        double A = get_Abar(Xi), Z = get_Zbar(Xi);
        double T_min = std::pow(10, log_T_min);
        double T_max = std::pow(10, log_T_max);

        // Out of bounds check for density or composition
        if (std::log10(rho) < log_rho_min || std::log10(rho) > log_rho_max ||
            A < A_min || A > A_max || Z < Z_min || Z > Z_max)
        {
            return fallback_temperature(e, A);
        }

        // Fast boundary check: if e is below the minimum table energy, return T_min
        double e_min_table = uses_free_energy ? free_energy_state(rho, T_min, A, Z).energy :
                             interpolate_4d(table_E, rho, T_min, A, Z);
        if (e <= e_min_table) {
            return T_min;
        }

        // Newton-Raphson iteration
        double T_guess = 1e8; // reasonable astrophysics start
        const int max_iters = 20;
        const double tol = 1e-6;

        for (int i = 0; i < max_iters; ++i) {
            T_guess = std::max(T_min, std::min(T_guess, T_max));

            double e_eval = uses_free_energy ? free_energy_state(rho, T_guess, A, Z).energy :
                            interpolate_4d(table_E, rho, T_guess, A, Z);
            double cv_eval = uses_free_energy ? free_energy_state(rho, T_guess, A, Z).cv :
                             interpolate_4d(table_cv, rho, T_guess, A, Z);

            if (cv_eval <= 0.0) {
                // Finite difference fallback
                double dT_fd = T_guess * 0.01;
                double e_plus = uses_free_energy ?
                    free_energy_state(rho, T_guess + dT_fd, A, Z).energy :
                    interpolate_4d(table_E, rho, T_guess + dT_fd, A, Z);
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

        double A = get_Abar(Xi), Z = get_Zbar(Xi);
        double T = get_temperature(U.rho, e_int, Xi);
        if (is_out_of_bounds(std::log10(U.rho), std::log10(T), A, Z))
        {
            return fallback_sound_speed(U.rho, e_int);
        }
        return uses_free_energy ? free_energy_state(U.rho, T, A, Z).sound_speed :
               interpolate_4d(table_cs, U.rho, T, A, Z);
    }

    double get_gamma(const double *Xi, double rho = 0.0, double e = 0.0) const
    {
        if (rho < 1e-12 || e < 1e-12)
            return fallback_gamma();
        double p = get_pressure_from_rho_e(rho, e, Xi);
        if (p < 1e-12)
            return fallback_gamma();

        double A = get_Abar(Xi), Z = get_Zbar(Xi);
        double T = get_temperature(rho, e, Xi);
        const double sound_speed = get_sound_speed_from_rho_T(rho, T, Xi);
        return rho * sound_speed * sound_speed / p;
    }

    double get_sound_speed_from_rho_T(double rho, double T, const double *Xi) const
    {
        double A = get_Abar(Xi), Z = get_Zbar(Xi);
        if (is_out_of_bounds(std::log10(rho), std::log10(T), A, Z))
        {
            double e = get_eint_from_T(rho, T, Xi);
            return fallback_sound_speed(rho, e);
        }
        return uses_free_energy ? free_energy_state(rho, T, A, Z).sound_speed :
               interpolate_4d(table_cs, rho, T, A, Z);
    }

    double get_dp_drho_e(double rho, double e, const double *Xi) const
    {
        double A = get_Abar(Xi), Z = get_Zbar(Xi);
        double T = get_temperature(rho, e, Xi);
        if (is_out_of_bounds(std::log10(rho), std::log10(T), A, Z))
        {
            return e * (fallback_gamma() - 1.0); // Ideal-gas (dP/drho)_e.
        }

        if (uses_free_energy)
            return free_energy_state(rho, T, A, Z).dp_drho_e;
        if (table_dP_drho)
            return interpolate_4d(table_dP_drho, rho, T, A, Z);

        return eos_utils::finite_difference_dp_drho_e(
            *this, rho, e, Xi, std::pow(10.0, log_rho_min),
            std::pow(10.0, log_rho_max));
    }

    double get_dp_de_rho(double rho, double e, const double *Xi) const
    {
        double A = get_Abar(Xi), Z = get_Zbar(Xi);
        double T = get_temperature(rho, e, Xi);
        if (is_out_of_bounds(std::log10(rho), std::log10(T), A, Z))
        {
            return rho * (fallback_gamma() - 1.0); // Ideal-gas (dP/de)_rho.
        }
        if (uses_free_energy)
            return free_energy_state(rho, T, A, Z).dp_de_rho;
        if (table_dP_dT && table_cv) {
            double dp_dT = interpolate_4d(table_dP_dT, rho, T, A, Z);
            double cv = interpolate_4d(table_cv, rho, T, A, Z);
            if (cv > 0.0) return dp_dT / cv;
        }

        double de = e * 0.001;
        double T_plus = get_temperature(rho, e + de, Xi);
        double T_minus = get_temperature(rho, e - de, Xi);
        return (interpolate_4d(table_P, rho, T_plus, A, Z) -
                interpolate_4d(table_P, rho, T_minus, A, Z)) /
               (2.0 * de);
    }

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
        const double A = get_Abar(state.Xi);
        const double Z = get_Zbar(state.Xi);
        const bool use_fallback = is_out_of_bounds(
            std::log10(state.rho), std::log10(state.T), A, Z);
        if (use_fallback) {
            const double R_spec = k_B_cgs / (A * m_u_cgs);
            state.dp_dT = state.rho * R_spec;
        } else if (uses_free_energy) {
            state.dp_dT =
                free_energy_state(state.rho, state.T, A, Z).dp_dT;
        } else if (table_dP_dT) {
            state.dp_dT = interpolate_4d(table_dP_dT, state.rho, state.T, A, Z);
        } else {
            const double dT = std::max(std::abs(state.T) * 1.0e-4, 1.0e-8);
            const double table_T_min = std::pow(10.0, log_T_min);
            const double table_T_max = std::pow(10.0, log_T_max);
            const double lower_T = std::max(state.T - dT, table_T_min);
            const double upper_T = std::min(state.T + dT, table_T_max);
            const double lower_P = interpolate_4d(
                table_P, state.rho, lower_T, A, Z);
            const double upper_P = interpolate_4d(
                table_P, state.rho, upper_T, A, Z);
            state.dp_dT = (upper_P - lower_P) / (upper_T - lower_T);
        }

        // 3. Deep Physical Variables (Unused in Tabular)
        state.pele = 0.0;
        state.xne = 0.0;
        state.eta = 0.0;
    }

    const SpeciesManager* get_species_manager() const { return specs; }
};

// Host owner for HDF5 loading and table lifetime.
struct Tabular4DEOS : public EOSBase
{
private:
    std::string table_path;
    std::vector<double> h_table_P;
    std::vector<double> h_table_E;
    std::vector<double> h_table_cs;
    std::vector<double> h_table_cv;
    std::vector<double> h_table_dP_drho;
    std::vector<double> h_table_dP_dT;

    std::array<std::vector<double>, tabular_eos::FieldCount> h_free_energy_fields;

    Tabular4DEOSView view;

public:
    Tabular4DEOS(const std::string &h5_filename, const SpeciesManager *specs_ptr = nullptr);

    Tabular4DEOSView get_view() const { return view; }
    const SpeciesManager* get_species_manager() const { return view.specs; }
};
