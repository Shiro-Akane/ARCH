/**
 * @file Tabular4DEOS.h
 * @brief 4D Tabular Equation of State reading from HDF5 (rho, T, A_bar, Z_bar).
 * Host and device views share interpolation and thermodynamic closure.
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

// Keep the two large interpolation routines as explicit device call
// boundaries.  Force-inlining them into every reconstruction/flux query makes
// NVCC materialize the same 4-D/free-energy expression graph many times in a
// single Hydro kernel and can exceed a 16 GiB Debug-build worker.  The formula
// remains shared by Host and CUDA; only the CUDA compilation boundary differs.
// ARCH_HEAVY_INLINE is owned by core/ArchPortability.h, shared with other EOS.

// One mathematical implementation parameterized by host or device species metadata.
template <class SpeciesView>
struct BasicTabular4DEOSView
{
    // Table dimensions and coordinate bounds for rho, temperature, Abar, and Zbar.
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

    const double *table_dP_drho;
    const double *table_dP_dT;

    bool uses_free_energy = false;
    std::array<const double*, tabular_eos::FieldCount> free_energy_fields{};
    SpeciesView specs{};

    // Optional non-owning device error latch, bound only on a per-launch view
    // copy.  Owners and Host views leave this null; Host failures still throw.
    int* device_error_status = nullptr;

    static constexpr double k_B_cgs = arch::constants::statistical::cgs::boltzmann;
    static constexpr double m_u_cgs = arch::constants::atomic::cgs::atomic_mass_unit;

    // Domain check and analytic ideal-gas fallback.
    ARCH_INLINE bool is_out_of_bounds(double log_rho, double log_T, double A, double Z) const
    {
        return (log_rho < log_rho_min || log_rho > log_rho_max ||
                log_T < log_T_min || log_T > log_T_max ||
                A < A_min || A > A_max ||
                Z < Z_min || Z > Z_max);
    }

    // Monatomic ideal-gas ratio used only outside the tabulated domain.
    ARCH_INLINE double fallback_gamma() const { return 5.0 / 3.0; }

    ARCH_INLINE double fallback_pressure(double rho, double e) const
    {
        return rho * e * (fallback_gamma() - 1.0);
    }

    ARCH_INLINE double fallback_temperature(double e, double Abar) const
    {
        double R_spec = k_B_cgs / (Abar * m_u_cgs);
        return e * (fallback_gamma() - 1.0) / R_spec;
    }

    ARCH_INLINE double fallback_sound_speed(double rho, double e) const
    {
        double p = fallback_pressure(rho, e);
        return std::sqrt(fallback_gamma() * p / rho);
    }

    // Quadrilinear interpolation.
    ARCH_HEAVY_INLINE double interpolate_4d(
        const double *table, double rho, double T, double A, double Z,
        eos_utils::LinearCompositionDerivatives* derivatives = nullptr) const
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

        // First evaluate rho/T at the four composition corners. Values and
        // analytic derivatives then use this same bilinear composition patch.
        double value[2][2], thermal[2][2];
        for (int da = 0; da < 2; ++da) {
            for (int dz = 0; dz < 2; ++dz) {
                const double lower = table[free_energy_index(i, j, k + da, l + dz)] * (1.0 - tx)
                    + table[free_energy_index(i + 1, j, k + da, l + dz)] * tx;
                const double upper = table[free_energy_index(i, j + 1, k + da, l + dz)] * (1.0 - tx)
                    + table[free_energy_index(i + 1, j + 1, k + da, l + dz)] * tx;
                value[da][dz] = lower * (1.0 - ty) + upper * ty;
                if (derivatives) thermal[da][dz] = (upper - lower) / (std::log(10.0) * dlog_T * T);
            }
        }
        const auto patch = eos_utils::bilinear_value(value[0][0], value[0][1], value[1][0], value[1][1], tu, tv, dA, dZ);
        if (derivatives) {
            const auto derivative = eos_utils::bilinear_value(thermal[0][0], thermal[0][1], thermal[1][0], thermal[1][1], tu, tv, dA, dZ);
            *derivatives = {};
            derivatives->energy = {patch.first, patch.second};
            derivatives->energy_hessian[1] = patch.mixed;
            derivatives->energy_temperature = {derivative.first, derivative.second};
            // For a cv-table call these same field derivatives become cv_X/cv_T.
            derivatives->cv_temperature = derivative.value;
        }
        return patch.value;
    }

    ARCH_INLINE std::size_t free_energy_index(
        int irho, int itemperature, int ia, int iz) const
    {
        const std::size_t composition_count =
            static_cast<std::size_t>(n_A) * n_Z;
        return static_cast<std::size_t>(irho) *
                   static_cast<std::size_t>(n_T) * composition_count +
               static_cast<std::size_t>(itemperature) * composition_count +
               static_cast<std::size_t>(ia) * n_Z +
               static_cast<std::size_t>(iz);
    }

    ARCH_HEAVY_INLINE tabular_eos::FreeEnergyState interpolate_free_energy(
        double rho, double T, double A, double Z,
        eos_utils::LinearCompositionDerivatives* derivatives = nullptr) const
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
        const double hx = std::log(10.0) * dlog_rho;
        const double hy = std::log(10.0) * dlog_T;
        const std::array<std::size_t, 4> corners_00{
            free_energy_index(i, j, k, l),
            free_energy_index(i, j + 1, k, l),
            free_energy_index(i + 1, j, k, l),
            free_energy_index(i + 1, j + 1, k, l)};
        const std::array<std::size_t, 4> corners_01{
            free_energy_index(i, j, k, l + 1),
            free_energy_index(i, j + 1, k, l + 1),
            free_energy_index(i + 1, j, k, l + 1),
            free_energy_index(i + 1, j + 1, k, l + 1)};
        const std::array<std::size_t, 4> corners_10{
            free_energy_index(i, j, k + 1, l),
            free_energy_index(i, j + 1, k + 1, l),
            free_energy_index(i + 1, j, k + 1, l),
            free_energy_index(i + 1, j + 1, k + 1, l)};
        const std::array<std::size_t, 4> corners_11{
            free_energy_index(i, j, k + 1, l + 1),
            free_energy_index(i, j + 1, k + 1, l + 1),
            free_energy_index(i + 1, j, k + 1, l + 1),
            free_energy_index(i + 1, j + 1, k + 1, l + 1)};
        const auto state_00 = tabular_eos::interpolate_biquintic(
            free_energy_fields, corners_00, tx, ty, hx, hy, derivatives != nullptr);
        const auto state_01 = tabular_eos::interpolate_biquintic(
            free_energy_fields, corners_01, tx, ty, hx, hy, derivatives != nullptr);
        const auto state_10 = tabular_eos::interpolate_biquintic(
            free_energy_fields, corners_10, tx, ty, hx, hy, derivatives != nullptr);
        const auto state_11 = tabular_eos::interpolate_biquintic(
            free_energy_fields, corners_11, tx, ty, hx, hy, derivatives != nullptr);
        const auto lower_A = tabular_eos::blend(state_00, state_01, tz);
        const auto upper_A = tabular_eos::blend(state_10, state_11, tz);
        const auto state = tabular_eos::blend(lower_A, upper_A, ta);
        if (derivatives) {
            const auto energy = eos_utils::bilinear_value(
                state_00.a - state_00.ay, state_01.a - state_01.ay,
                state_10.a - state_10.ay, state_11.a - state_11.ay, ta, tz, dA, dZ);
            const auto capacity = eos_utils::bilinear_value(
                (state_00.ay - state_00.ayy) / T, (state_01.ay - state_01.ayy) / T,
                (state_10.ay - state_10.ayy) / T, (state_11.ay - state_11.ayy) / T, ta, tz, dA, dZ);
            *derivatives = {};
            derivatives->energy = {energy.first, energy.second};
            derivatives->energy_hessian[1] = energy.mixed;
            derivatives->cv = {capacity.first, capacity.second};
            derivatives->energy_temperature = derivatives->cv;
            derivatives->cv_temperature = (2.0 * state.ayy - state.ay - state.ayyy) / (T * T);
        }
        return state;
    }

    ARCH_INLINE tabular_eos::FreeEnergyResult free_energy_result(
        double rho, double T, double A, double Z) const
    {
        return tabular_eos::evaluate_thermodynamics(
            interpolate_free_energy(rho, T, A, Z), rho, T);
    }

    ARCH_HEAVY_INLINE tabular_eos::ThermodynamicState free_energy_state(
        double rho, double T, double A, double Z) const
    {
        const auto result = free_energy_result(rho, T, A, Z);
        return tabular_eos::checked_thermodynamics(result, device_error_status);
    }

    // Composition coordinates Abar and Zbar.

    ARCH_INLINE double get_Abar(const double *Xi) const
    {
        if (specs.count > 0)
            return specs.calc_Abar(Xi);
        return 14.0; // Pure-nitrogen fallback when species metadata is absent.
    }

    ARCH_INLINE double get_Zbar(const double *Xi) const
    {
        if (specs.count > 0)
            return specs.calc_Zbar(Xi);
        return 7.0; // Matches the pure-nitrogen Abar fallback above.
    }

    // Linear composition coordinates y=sum(X/A), z=Ye. Table coordinates
    // Abar=1/y and Zbar=z/y are differentiated here, before the species map.
    ARCH_INLINE std::array<double, 2> composition_weights(int species) const
    {
        if (species >= specs.count) return {};
        const double inverse_a = 1.0 / specs.get_A(species);
        return {inverse_a, specs.get_Z(species) * inverse_a};
    }

    ARCH_HEAVY_INLINE eos_utils::LinearCompositionDerivatives composition_derivatives(
        double rho, double T, const double* Xi) const
    {
        eos_utils::LinearCompositionDerivatives d{};
        if (rho <= 1e-12 || T <= 1e-12) return d;
        const double A = get_Abar(Xi), Z = get_Zbar(Xi);
        double y = 0.0;
        for (int i = 0; i < specs.count; ++i) y += Xi[i] / specs.get_A(i);
        if (is_out_of_bounds(std::log10(rho), std::log10(T), A, Z)) {
            if (y > 1e-16) {
                d.cv[0] = k_B_cgs / (m_u_cgs * (fallback_gamma() - 1.0));
                d.energy[0] = T * d.cv[0];
                d.energy_temperature[0] = d.cv[0];
            }
            return d;
        }
        if (uses_free_energy) {
            const auto f = interpolate_free_energy(rho, T, A, Z, &d);
            const auto state = tabular_eos::checked_thermodynamics(
                tabular_eos::evaluate_thermodynamics(f, rho, T), device_error_status);
            if (!std::isfinite(state.energy)) d.energy.fill(state.energy);
        } else {
            interpolate_4d(table_E, rho, T, A, Z, &d);
            eos_utils::LinearCompositionDerivatives capacity{};
            interpolate_4d(table_cv, rho, T, A, Z, &capacity);
            d.cv = capacity.energy;
            d.cv_temperature = capacity.cv_temperature;
        }
        const auto table = d;
        const double a_y = y > 1e-16 ? -A * A : 0.0;
        const double z_y = y > 1e-16 ? -A * Z : 0.0;
        d.energy = {table.energy[0] * a_y + table.energy[1] * z_y, table.energy[1] * A};
        d.energy_temperature = {table.energy_temperature[0] * a_y + table.energy_temperature[1] * z_y,
                                table.energy_temperature[1] * A};
        d.cv = {table.cv[0] * a_y + table.cv[1] * z_y, table.cv[1] * A};
        d.energy_hessian = {};
        if (y > 1e-16) {
            d.energy_hessian[0] = 2.0 * A * A * (A * table.energy[0] + Z * table.energy[1])
                                + 2.0 * table.energy_hessian[1] * a_y * z_y;
            d.energy_hessian[1] = -A * A * (table.energy[1] + A * table.energy_hessian[1]);
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
        double A = get_Abar(Xi), Z = get_Zbar(Xi);
        if (is_out_of_bounds(std::log10(rho), std::log10(T), A, Z))
        {
            double e = get_eint_from_T(rho, T, Xi);
            return fallback_pressure(rho, e);
        }
        return uses_free_energy ? free_energy_state(rho, T, A, Z).pressure :
               interpolate_4d(table_P, rho, T, A, Z);
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

    ARCH_INLINE double get_cv(double rho, double T_target, const double *Xi) const
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

    ARCH_HEAVY_INLINE double get_temperature(
        double rho, double e, const double *Xi) const
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
        const double tol = 1e-14;

        for (int i = 0; i < max_iters; ++i) {
            T_guess = std::max(T_min, std::min(T_guess, T_max));

            double e_eval, cv_eval;
            if (uses_free_energy) {
                const auto thermal = free_energy_state(rho, T_guess, A, Z);
                e_eval = thermal.energy;
                cv_eval = thermal.cv;
            } else {
                e_eval = interpolate_4d(table_E, rho, T_guess, A, Z);
                cv_eval = interpolate_4d(table_cv, rho, T_guess, A, Z);
            }

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

        double A = get_Abar(Xi), Z = get_Zbar(Xi);
        double T = get_temperature(U.rho, e_int, Xi);
        if (is_out_of_bounds(std::log10(U.rho), std::log10(T), A, Z))
        {
            return fallback_sound_speed(U.rho, e_int);
        }
        return uses_free_energy ? free_energy_state(U.rho, T, A, Z).sound_speed :
               interpolate_4d(table_cs, U.rho, T, A, Z);
    }

    ARCH_INLINE double get_gamma(const double *Xi, double rho = 0.0, double e = 0.0) const
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

    ARCH_INLINE double get_sound_speed_from_rho_T(double rho, double T, const double *Xi) const
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

    ARCH_INLINE double get_dp_drho_e(double rho, double e, const double *Xi) const
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

    ARCH_INLINE double get_dp_de_rho(double rho, double e, const double *Xi) const
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

};




struct Tabular4DEOSHostView : BasicTabular4DEOSView<SpeciesHostView>
{
    std::array<std::size_t, 6> table_extents{};
    std::array<std::size_t, tabular_eos::FieldCount> free_energy_extents{};
    const SpeciesManager *get_species_manager() const { return specs.host_owner; }
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
    const SpeciesManager *specs_owner = nullptr;
    Tabular4DEOSHostView view;

public:
    Tabular4DEOS(const std::string &h5_filename, const SpeciesManager *specs_ptr = nullptr);

    Tabular4DEOSHostView get_view() const
    {
        Tabular4DEOSHostView rebound = view;
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
