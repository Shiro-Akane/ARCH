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
 * 3. Keep host and device views consistent through one shared mathematical implementation.
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
#include "../constant/PhysicalConstants.h"

// Heavy shared leaves use the portability authority's compilation boundary.

// One mathematical implementation parameterized by host or device species metadata.
template <class SpeciesView>
struct BasicTabular3DEOSView
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

    const double *table_dP_drho; // Optional precomputed pressure-density derivative.
    const double *table_dP_dT;   // Optional precomputed pressure-temperature derivative.

    bool uses_free_energy = false;
    std::array<const double*, tabular_eos::FieldCount> free_energy_fields{};
    SpeciesView specs{};

    int target_species_id;

    // Optional non-owning device error latch, bound only on a per-launch view
    // copy.  Owners and Host views leave this null; Host failures still throw.
    int* device_error_status = nullptr;

    // Boltzmann constant in CGS units, erg/K.
    static constexpr double k_B_cgs = arch::constants::statistical::cgs::boltzmann;
    static constexpr double m_u_cgs = arch::constants::atomic::cgs::atomic_mass_unit;

    // Domain checks and analytic ideal-gas fallback.

    ARCH_INLINE bool is_out_of_bounds(double log_rho, double log_T, double X) const
    {
        return (log_rho < log_rho_min || log_rho > log_rho_max ||
                log_T < log_T_min || log_T > log_T_max ||
                X < X_min || X > X_max);
    }

    ARCH_INLINE double fallback_gamma() const { return 5.0 / 3.0; } // Monatomic ideal-gas fallback.

    ARCH_INLINE double fallback_pressure(double rho, double e) const
    {
        return rho * e * (fallback_gamma() - 1.0);
    }

    ARCH_INLINE double fallback_temperature(double e, const double *Xi) const
    {
        // Abar=1 represents pure hydrogen when no SpeciesManager is attached.
        double Abar = (specs.count > 0) ? specs.calc_Abar(Xi) : 1.0;
        double R_spec = k_B_cgs / (Abar * m_u_cgs);
        return e * (fallback_gamma() - 1.0) / R_spec;
    }

    ARCH_INLINE double fallback_sound_speed(double rho, double e) const
    {
        double p = fallback_pressure(rho, e);
        return std::sqrt(fallback_gamma() * p / rho);
    }

    // Trilinear interpolation in log(rho), log(T), and composition coordinate.
    ARCH_HEAVY_INLINE double interpolate_3d(
        const double *table, double rho, double T, double X,
        double* composition_slope = nullptr, double* temperature_slope = nullptr,
        double* mixed_slope = nullptr) const
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

        if (composition_slope) *composition_slope = (c1 - c0) / dX;
        if (temperature_slope || mixed_slope) {
            const double inverse_temperature_spacing = 1.0 / (std::log(10.0) * dlog_T * T);
            if (temperature_slope) *temperature_slope =
                ((c10 - c00) * (1.0 - tz) + (c11 - c01) * tz) * inverse_temperature_spacing;
            if (mixed_slope) *mixed_slope =
                ((c11 - c01) - (c10 - c00)) * inverse_temperature_spacing / dX;
        }

        // Interpolate along composition to obtain the final value.
        return c0 * (1.0 - tz) + c1 * tz;
    }

    ARCH_INLINE std::size_t free_energy_index(
        int irho, int itemperature, int icomposition) const
    {
        return static_cast<std::size_t>(irho) *
                   static_cast<std::size_t>(n_T) * n_X +
               static_cast<std::size_t>(itemperature) * n_X +
               static_cast<std::size_t>(icomposition);
    }

    ARCH_HEAVY_INLINE tabular_eos::FreeEnergyState interpolate_free_energy(
        double rho, double T, double composition,
        eos_utils::LinearCompositionDerivatives* derivatives = nullptr) const
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
        const std::array<std::size_t, 4> lower_corners{
            free_energy_index(i, j, k),
            free_energy_index(i, j + 1, k),
            free_energy_index(i + 1, j, k),
            free_energy_index(i + 1, j + 1, k)};
        const std::array<std::size_t, 4> upper_corners{
            free_energy_index(i, j, k + 1),
            free_energy_index(i, j + 1, k + 1),
            free_energy_index(i + 1, j, k + 1),
            free_energy_index(i + 1, j + 1, k + 1)};
        const double hx = std::log(10.0) * dlog_rho;
        const double hy = std::log(10.0) * dlog_T;
        const auto lower = tabular_eos::interpolate_biquintic(
            free_energy_fields, lower_corners, tx, ty, hx, hy, derivatives != nullptr);
        const auto upper = tabular_eos::interpolate_biquintic(
            free_energy_fields, upper_corners, tx, ty, hx, hy, derivatives != nullptr);
        const auto state = tabular_eos::blend(lower, upper, tc);
        if (derivatives) {
            *derivatives = {};
            derivatives->energy[0] = ((upper.a - upper.ay) - (lower.a - lower.ay)) / dX;
            derivatives->cv[0] = ((upper.ay - upper.ayy) - (lower.ay - lower.ayy)) / (T * dX);
            derivatives->energy_temperature[0] = derivatives->cv[0];
            derivatives->cv_temperature = (2.0 * state.ayy - state.ay - state.ayyy) / (T * T);
        }
        return state;
    }

    ARCH_INLINE tabular_eos::FreeEnergyResult free_energy_result(
        double rho, double T, double composition) const
    {
        return tabular_eos::evaluate_thermodynamics(
            interpolate_free_energy(rho, T, composition), rho, T);
    }

    ARCH_HEAVY_INLINE tabular_eos::ThermodynamicState free_energy_state(
        double rho, double T, double composition) const
    {
        const auto result = free_energy_result(rho, T, composition);
        return tabular_eos::checked_thermodynamics(result, device_error_status);
    }

    // Composition-coordinate query used by generated network interfaces.

    ARCH_INLINE double get_target_X(const double *Xi) const
    {
        // An explicit target species selects its mass fraction directly.
        if (target_species_id >= 0)
        {
            return Xi[target_species_id];
        }

        // Otherwise derive electron fraction Ye when species metadata is available.
        if (specs.count > 0)
        {
            return specs.calc_Ye(Xi);
        }

        // Ye=0.5 is the neutral symmetric-matter fallback without metadata.
        return 0.5;
    }

    // Linear coordinates are the selected table composition and sum(X/A).
    // The latter supplies the declared out-of-table ideal-gas closure.
    ARCH_INLINE std::array<double, 2> composition_weights(int species) const
    {
        const double inverse_a = species < specs.count ? 1.0 / specs.get_A(species) : 0.0;
        const double table_weight = target_species_id >= 0 ? double(species == target_species_id)
            : (species < specs.count ? specs.get_Z(species) * inverse_a : 0.0);
        return {table_weight, inverse_a};
    }

    ARCH_HEAVY_INLINE eos_utils::LinearCompositionDerivatives composition_derivatives(
        double rho, double T, const double* Xi) const
    {
        eos_utils::LinearCompositionDerivatives d{};
        if (rho <= 1e-12 || T <= 1e-12) return d;
        const double X = get_target_X(Xi);
        if (is_out_of_bounds(std::log10(rho), std::log10(T), X)) {
            double y = 0.0;
            for (int i = 0; i < specs.count; ++i) y += Xi[i] / specs.get_A(i);
            if (y > 1e-16) {
                d.cv[1] = k_B_cgs / (m_u_cgs * (fallback_gamma() - 1.0));
                d.energy[1] = T * d.cv[1];
                d.energy_temperature[1] = d.cv[1];
            }
        } else if (uses_free_energy) {
            const auto f = interpolate_free_energy(rho, T, X, &d);
            const auto state = tabular_eos::checked_thermodynamics(
                tabular_eos::evaluate_thermodynamics(f, rho, T), device_error_status);
            if (!std::isfinite(state.energy)) d.energy.fill(state.energy);
        } else {
            interpolate_3d(table_E, rho, T, X, &d.energy[0], nullptr, &d.energy_temperature[0]);
            interpolate_3d(table_cv, rho, T, X, &d.cv[0], &d.cv_temperature);
        }
        return d;
    }

    template <int Equations>
    ARCH_INLINE void get_energy_composition_gradient(double rho, double T, const double* X, double* result) const
    { eos_utils::composition_derivative_query<Equations, eos_utils::CompositionQuery::energy_gradient>(*this, rho, T, X, result); }
    template <int Equations>
    ARCH_INLINE void get_cv_gradient(double rho, double T, const double* X, double* result) const
    { eos_utils::composition_derivative_query<Equations, eos_utils::CompositionQuery::cv_gradient>(*this, rho, T, X, result); }
    template <int Equations>
    ARCH_INLINE void get_energy_composition_hessian_action(double rho, double T, const double* X, const double* flow, double* result) const
    { eos_utils::composition_derivative_query<Equations, eos_utils::CompositionQuery::energy_hessian_action>(*this, rho, T, X, result, flow); }

    ARCH_INLINE double get_pressure_from_rho_T(double rho, double T, const double *Xi) const
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

    ARCH_INLINE double get_pressure_from_rho_e(double rho, double e, const double *Xi) const
    {
        if (rho <= 1e-12 || e <= 1e-12)
            return 0.0;
        double T = get_temperature(rho, e, Xi);
        return get_pressure_from_rho_T(rho, T, Xi);
    }

    ARCH_INLINE double get_eint_from_T(double rho, double T_target, const double *Xi) const
    {
        if (rho <= 1e-12 || T_target <= 1e-12)
            return 0.0;

        double X = get_target_X(Xi);

        if (is_out_of_bounds(std::log10(rho), std::log10(T_target), X))
        {
            double Abar = (specs.count > 0) ? specs.calc_Abar(Xi) : 1.0;
            double R_spec = k_B_cgs / (Abar * m_u_cgs);
            return T_target * R_spec / (fallback_gamma() - 1.0);
        }

        return uses_free_energy ? free_energy_state(rho, T_target, X).energy :
               interpolate_3d(table_E, rho, T_target, X);
    }

    ARCH_INLINE double get_cv(double rho, double T_target, const double *Xi) const
    {
        if (rho <= 1e-12 || T_target <= 1e-12)
            return 0.0;

        double X = get_target_X(Xi);

        if (is_out_of_bounds(std::log10(rho), std::log10(T_target), X))
        {
            double Abar = (specs.count > 0) ? specs.calc_Abar(Xi) : 1.0;
            double R_spec = k_B_cgs / (Abar * m_u_cgs);
            return R_spec / (fallback_gamma() - 1.0);
        }

        return uses_free_energy ? free_energy_state(rho, T_target, X).cv :
               interpolate_3d(table_cv, rho, T_target, X);
    }

    ARCH_HEAVY_INLINE double get_temperature(
        double rho, double e, const double *Xi) const
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
        const double tol = 1e-14;

        for (int i = 0; i < max_iters; ++i) {
            T_guess = std::max(T_min, std::min(T_guess, T_max));

            double e_eval, cv_eval;
            if (uses_free_energy) {
                const auto thermal = free_energy_state(rho, T_guess, X);
                e_eval = thermal.energy;
                cv_eval = thermal.cv;
            } else {
                e_eval = interpolate_3d(table_E, rho, T_guess, X);
                cv_eval = interpolate_3d(table_cv, rho, T_guess, X);
            }

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

            if (std::abs(dT) <= tol * std::max(std::abs(T_guess), 1.0)) break;
        }

        return T_guess;
    }

    ARCH_INLINE double get_pressure(const FluidVector &U, const double *Xi) const
    {
        double e_int = eos_utils::extract_specific_internal_energy(U);
        return get_pressure_from_rho_e(U.rho, e_int, Xi);
    }

    ARCH_INLINE double get_sound_speed(const FluidVector &U, double p, const double *Xi) const
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

    ARCH_INLINE double get_gamma(const double *Xi, double rho = 0.0, double e = 0.0) const
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

    ARCH_INLINE double get_sound_speed_from_rho_T(double rho, double T, const double *Xi) const
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
    ARCH_INLINE double get_dp_drho_e(double rho, double e, const double *Xi) const
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

        return eos_utils::finite_difference_dp_drho_e(
            *this, rho, e, Xi, std::pow(10.0, log_rho_min),
            std::pow(10.0, log_rho_max));
    }

    ARCH_INLINE double get_dp_de_rho(double rho, double e, const double *Xi) const
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
    ARCH_INLINE double get_total_energy_primitive(double rho, double u, double v, double w, double p, const double *Xi) const
    {
        return eos_utils::solve_total_energy(*this, rho, u, v, w, p, Xi);
    }

    ARCH_INLINE double get_eta(double rho, double T, const double* Xi) const { return 0.0; }

    // Pipeline: evaluate_state
    ARCH_INLINE void evaluate_state(eos_state_t& state) const {
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
            const double Abar = (specs.count > 0)
                ? specs.calc_Abar(state.Xi) : 1.0;
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

};




struct Tabular3DEOSHostView : BasicTabular3DEOSView<SpeciesHostView>
{
    std::array<std::size_t, 6> table_extents{};
    std::array<std::size_t, tabular_eos::FieldCount> free_energy_extents{};
    const SpeciesManager *get_species_manager() const { return specs.host_owner; }
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
    const SpeciesManager *specs_owner = nullptr;
    Tabular3DEOSHostView view;

public:
    Tabular3DEOS(const std::string &h5_filename, const SpeciesManager *specs_ptr = nullptr);

    // Solver dispatch receives only the lightweight non-owning view.
    Tabular3DEOSHostView get_view() const
    {
        Tabular3DEOSHostView rebound = view;
        rebound.table_P = h_table_P.empty() ? nullptr : h_table_P.data();
        rebound.table_E = h_table_E.empty() ? nullptr : h_table_E.data();
        rebound.table_cs = h_table_cs.empty() ? nullptr : h_table_cs.data();
        rebound.table_cv = h_table_cv.empty() ? nullptr : h_table_cv.data();
        rebound.table_dP_drho = h_table_dP_drho.empty()
            ? nullptr : h_table_dP_drho.data();
        rebound.table_dP_dT = h_table_dP_dT.empty()
            ? nullptr : h_table_dP_dT.data();
        for (int field = 0; field < tabular_eos::FieldCount; ++field) {
            rebound.free_energy_fields[field] =
                h_free_energy_fields[field].empty()
                    ? nullptr : h_free_energy_fields[field].data();
            rebound.free_energy_extents[field] =
                h_free_energy_fields[field].size();
        }
        rebound.table_extents = {
            h_table_P.size(), h_table_E.size(), h_table_cs.size(), h_table_cv.size(),
            h_table_dP_drho.size(), h_table_dP_dT.size()};
        rebound.specs = specs_owner ? specs_owner->get_host_view() : SpeciesHostView{};
        return rebound;
    }

    const SpeciesManager* get_species_manager() const { return specs_owner; }
};
