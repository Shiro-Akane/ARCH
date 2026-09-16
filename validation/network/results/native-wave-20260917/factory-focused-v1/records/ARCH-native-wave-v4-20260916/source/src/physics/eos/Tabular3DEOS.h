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
#include "TabularInversion.h"
#include "TabularInterpolation.h"

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
    bool strict_domain = false;
    double energy_reference_shift = 0.0;
    std::array<const double*, tabular_eos::FieldCount> free_energy_fields{};
    SpeciesView specs{};

    int target_species_id;

    // Native products retain their axes and encoded fields in this same view.
    // The source energy shift is the fixed conserved-energy reference, not a
    // temperature/composition-dependent positivity correction.
    bool native_direct = false;
    std::array<const double*, 3> axis_nodes{};
    tabular_eos::FieldTransform pressure_transform{}, energy_transform{};
    double source_energy_shift = 0.0;
    const double* table_dP_de = nullptr;
    const double* table_valid = nullptr;

    // Optional non-owning device error latch, bound only on a per-launch view
    // copy.  Owners and Host views leave this null; Host failures still throw.
    int* device_error_status = nullptr;

    // Boltzmann constant in CGS units, erg/K.
    static constexpr double k_B_cgs = arch::constants::statistical::cgs::boltzmann;
    static constexpr double m_u_cgs = arch::constants::atomic::cgs::atomic_mass_unit;

    // Domain checks and analytic ideal-gas fallback.

    ARCH_INLINE bool is_out_of_bounds(double log_rho, double log_T, double X) const
    {
        const bool outside = (log_rho < log_rho_min || log_rho > log_rho_max ||
                log_T < log_T_min || log_T > log_T_max ||
                X < X_min || X > X_max);
        if (native_direct && (outside || !std::isfinite(log_rho)
                || !std::isfinite(log_T) || !std::isfinite(X))) {
            native_failure(tabular_eos::FreeEnergyStatus::invalid_native_domain);
            return true;
        }
        return outside;
    }

    ARCH_INLINE double native_failure(tabular_eos::FreeEnergyStatus status) const
    {
        return tabular_eos::checked_thermodynamics(
            tabular_eos::free_energy_failure(status), device_error_status).energy;
    }

    ARCH_INLINE bool valid_cell(int i, int j, int k) const
    {
        if (!table_valid) return true;
        for (int dr = 0; dr < 2; ++dr)
            for (int dt = 0; dt < 2; ++dt)
                for (int dc = 0; dc < 2; ++dc)
                    if (table_valid[free_energy_index(i + dr, j + dt, k + dc)] != 1.0)
                        return false;
        return true;
    }

    ARCH_HEAVY_INLINE tabular_eos::ThermodynamicState native_state(
        double rho, double T, const double* Xi) const
    {
        const double x = get_target_X(Xi);
        tabular_eos::ThermodynamicState state{};
        double pressure_rho = 0.0, energy_rho = 0.0;
        state.pressure = interpolate_3d(table_P, rho, T, x,
            nullptr, &state.dp_dT, nullptr, nullptr, &pressure_rho);
        state.energy = interpolate_3d(table_E, rho, T, x,
            nullptr, &state.cv, nullptr, nullptr, &energy_rho);
        state.dp_de_rho = state.dp_dT / state.cv;
        state.dp_drho_e = pressure_rho - state.dp_de_rho * energy_rho;
        const double cs2 = state.dp_drho_e
            + state.dp_de_rho * state.pressure / (rho * rho);
        state.sound_speed = std::sqrt(cs2);
        state.gamma1 = rho * cs2 / state.pressure;
        if (!(state.pressure > 0.0) || !(state.energy > 0.0)
            || !(state.cv > 0.0) || !(cs2 > 0.0)
            || !std::isfinite(state.pressure) || !std::isfinite(state.energy)
            || !std::isfinite(state.cv) || !std::isfinite(cs2)
            || !std::isfinite(state.dp_dT) || !std::isfinite(state.dp_de_rho)
            || !std::isfinite(state.dp_drho_e) || !std::isfinite(state.gamma1)
            || !std::isfinite(state.sound_speed)) {
            native_failure(tabular_eos::FreeEnergyStatus::invalid_native_cell);
            return tabular_eos::invalid_thermodynamic_state();
        }
        return state;
    }

    // Encoded energy is linear in log(T) within each native cell. Inverting
    // that very interpolant gives an exact cell root without imposing native
    // dedt as the derivative of a separately interpolated energy field. Search
    // all valid intervals so a nonmonotone source cannot select an arbitrary
    // thermal branch. This does not extrapolate or cross an invalid interval.
    ARCH_HEAVY_INLINE double native_temperature(
        double rho, double energy, const double* Xi, bool from_pressure = false) const
    {
        const double x = get_target_X(Xi);
        if (!(rho > 0.0) || !(energy > 0.0) || !std::isfinite(energy)
            || is_out_of_bounds(std::log10(rho), log_T_min, x))
            return native_failure(tabular_eos::FreeEnergyStatus::invalid_native_domain);
        const auto ar = tabular_eos::locate_axis(std::log10(rho), n_rho,
            log_rho_min, dlog_rho, axis_nodes[0]);
        const auto ac = tabular_eos::locate_axis(x, n_X, X_min, dX, axis_nodes[2]);
        const auto transform = from_pressure ? pressure_transform : energy_transform;
        const double* field = from_pressure ? table_P : table_E;
        const double target = transform.logarithmic
            ? std::log10(energy - transform.offset) : energy - transform.offset;
        if (!std::isfinite(target))
            return native_failure(tabular_eos::FreeEnergyStatus::invalid_temperature_inversion);
        double root = std::numeric_limits<double>::quiet_NaN();
        for (int j = 0; j + 1 < n_T; ++j) {
            if (!valid_cell(ar.lower, j, ac.lower)) continue;
            double q[2]{};
            for (int dt = 0; dt < 2; ++dt) {
                double along_x[2];
                for (int dc = 0; dc < 2; ++dc)
                    along_x[dc] = field[free_energy_index(ar.lower, j + dt, ac.lower + dc)]
                        * (1.0 - ar.fraction)
                        + field[free_energy_index(ar.lower + 1, j + dt, ac.lower + dc)]
                        * ar.fraction;
                q[dt] = along_x[0] * (1.0 - ac.fraction) + along_x[1] * ac.fraction;
            }
            // log10(pow(10,q)) need not reproduce q exactly. Recognize an
            // exactly matching decoded endpoint without widening the domain
            // or introducing a tolerance for physically out-of-range inputs.
            const double value0 = transform.decode(q[0]);
            const double value1 = transform.decode(q[1]);
            if (energy < std::min(value0, value1) || energy > std::max(value0, value1)) continue;
            double cell_target = std::max(std::min(q[0], q[1]),
                std::min(std::max(q[0], q[1]), target));
            if (energy == value0) cell_target = q[0];
            else if (energy == value1) cell_target = q[1];
            if (q[1] == q[0]) {
                if (cell_target == q[0])
                    return native_failure(tabular_eos::FreeEnergyStatus::ambiguous_temperature_inversion);
                continue;
            }
            // Pressure need not increase with temperature. Include decreasing
            // branches when checking uniqueness instead of silently choosing
            // a different positive-slope root of the same native interpolant.
            if (cell_target < std::min(q[0], q[1]) || cell_target > std::max(q[0], q[1])) continue;
            const double fraction = (cell_target - q[0]) / (q[1] - q[0]);
            const double lt = axis_nodes[1][j]
                + fraction * (axis_nodes[1][j + 1] - axis_nodes[1][j]);
            if (std::isfinite(root)
                && std::abs(lt - root) > 32.0 * std::numeric_limits<double>::epsilon()
                    * std::max(1.0, std::abs(root)))
                return native_failure(tabular_eos::FreeEnergyStatus::ambiguous_temperature_inversion);
            root = lt;
        }
        if (!std::isfinite(root))
            return native_failure(tabular_eos::FreeEnergyStatus::invalid_temperature_inversion);
        const double temperature = std::pow(10.0, root);
        const double recovered = interpolate_3d(field, rho, temperature, x);
        if (!std::isfinite(recovered)
            || std::abs(recovered - energy) > 2.0e-12 * std::max(std::abs(energy), 1.0))
            return native_failure(tabular_eos::FreeEnergyStatus::invalid_temperature_inversion);
        return temperature;
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
        double* mixed_slope = nullptr, double* composition_curvature = nullptr,
        double* density_slope = nullptr) const
    {
        if (native_direct && (!(rho > 0.0) || !(T > 0.0)))
            return native_failure(tabular_eos::FreeEnergyStatus::invalid_native_domain);
        if (!native_direct && (rho <= 1e-12 || T <= 1e-12))
            return 0.0;

        double x = log10(rho);
        double y = log10(T);
        double z = X;

        if (native_direct && is_out_of_bounds(x, y, z))
            return std::numeric_limits<double>::quiet_NaN();
        const auto ar = tabular_eos::locate_axis(x, n_rho, log_rho_min, dlog_rho, axis_nodes[0]);
        const auto at = tabular_eos::locate_axis(y, n_T, log_T_min, dlog_T, axis_nodes[1]);
        const auto ac = tabular_eos::locate_axis(z, n_X, X_min, dX, axis_nodes[2]);
        const int i = ar.lower, j = at.lower, k = ac.lower;
        const double tx = ar.fraction, ty = at.fraction, tz = ac.fraction;
        if (native_direct && !valid_cell(i, j, k))
            return native_failure(tabular_eos::FreeEnergyStatus::invalid_native_cell);

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

        double dc = (c1 - c0) / ac.width, dt = 0.0, mixed = 0.0;
        if (temperature_slope || mixed_slope) {
            const double inverse_temperature_spacing = 1.0 / (std::log(10.0) * at.width * T);
            dt =
                ((c10 - c00) * (1.0 - tz) + (c11 - c01) * tz) * inverse_temperature_spacing;
            mixed = ((c11 - c01) - (c10 - c00)) * inverse_temperature_spacing / ac.width;
        }
        double curvature = 0.0;
        const auto transform = table == table_P ? pressure_transform
            : (table == table_E ? energy_transform : tabular_eos::FieldTransform{});
        const double encoded_value = c0 * (1.0 - tz) + c1 * tz;
        const double value = transform.decode(encoded_value,
            &dc, &dt, &mixed, &curvature);
        if (density_slope) {
            const double lower = (c100 - c000) * (1.0 - ty) + (c110 - c010) * ty;
            const double upper = (c101 - c001) * (1.0 - ty) + (c111 - c011) * ty;
            *density_slope = (lower * (1.0 - tz) + upper * tz)
                / (std::log(10.0) * ar.width * rho);
            if (transform.logarithmic)
                *density_slope *= std::log(10.0) * std::pow(10.0, encoded_value);
        }
        if (composition_slope) *composition_slope = dc;
        if (temperature_slope) *temperature_slope = dt;
        if (mixed_slope) *mixed_slope = mixed;
        if (composition_curvature) *composition_curvature = curvature;
        return value;
    }

    ARCH_INLINE std::size_t free_energy_index(
        int irho, int itemperature, int icomposition) const
    {
        return static_cast<std::size_t>(irho) *
                   static_cast<std::size_t>(n_T) * n_X +
               static_cast<std::size_t>(itemperature) * n_X +
               static_cast<std::size_t>(icomposition);
    }

    ARCH_INLINE tabular_eos::FreeEnergyStatus strict_support(
        double rho, double T, double composition) const
    {
        using Status = tabular_eos::FreeEnergyStatus;
        if (!uses_free_energy || n_rho < 2 || n_T < 2 || n_X < 2 ||
            !(dlog_rho > 0.0) || !(dlog_T > 0.0) || !(dX > 0.0) ||
            !std::isfinite(rho) || !std::isfinite(T) || !std::isfinite(composition) ||
            !(rho > 0.0) || !(T > 0.0) ||
            rho < std::pow(10.0, log_rho_min) || rho > std::pow(10.0, log_rho_max) ||
            T < std::pow(10.0, log_T_min) || T > std::pow(10.0, log_T_max) ||
            composition < X_min || composition > X_max)
            return Status::invalid_native_domain;
        for (int field = 0; field < tabular_eos::FieldCount; ++field)
            if (!free_energy_fields[field]) return Status::invalid_native_cell;
        const auto ar = tabular_eos::locate_axis(std::log10(rho), n_rho, log_rho_min, dlog_rho, nullptr);
        const auto at = tabular_eos::locate_axis(std::log10(T), n_T, log_T_min, dlog_T, nullptr);
        const auto ac = tabular_eos::locate_axis(composition, n_X, X_min, dX, axis_nodes[2]);
        return valid_cell(ar.lower, at.lower, ac.lower)
            ? Status::success : Status::invalid_native_cell;
    }

    ARCH_HEAVY_INLINE bool strict_thermal_polynomial(
        double rho, double composition, int j, bool pressure,
        tabular_eos::ThermalPolynomial& out) const
    {
        const auto ar = tabular_eos::locate_axis(std::log10(rho), n_rho, log_rho_min, dlog_rho, nullptr);
        const auto ac = tabular_eos::locate_axis(composition, n_X, X_min, dX, axis_nodes[2]);
        const int i = ar.lower, k = ac.lower;
        if (!valid_cell(i, j, k)) return false;
        const double hx = std::log(10.0) * dlog_rho, hy = std::log(10.0) * dlog_T;
        tabular_eos::ThermalPolynomial composition_patches[2];
        for (int c = 0; c < 2; ++c) {
            const std::array<std::size_t, 4> corners{
                free_energy_index(i, j, k + c), free_energy_index(i, j + 1, k + c),
                free_energy_index(i + 1, j, k + c), free_energy_index(i + 1, j + 1, k + c)};
            composition_patches[c] = tabular_eos::thermal_polynomial(
                free_energy_fields, corners, ar.fraction, hx, hy, pressure ? 1 : 0);
        }
        out = tabular_eos::blend(composition_patches[0], composition_patches[1], ac.fraction);
        if (pressure) {
            for (int degree = 0; degree < 6; ++degree) out[degree] *= rho;
        } else {
            const auto free_energy = out;
            for (int degree = 0; degree < 5; ++degree)
                out[degree] -= (degree + 1) * free_energy[degree + 1] / hy;
            if (energy_reference_shift != 0.0) out[0] += energy_reference_shift;
        }
        return true;
    }

    ARCH_HEAVY_INLINE double strict_temperature(
        double rho, double target, const double* Xi, bool pressure = false) const
    {
        const double composition = get_target_X(Xi);
        // Domain validation is independent of a possibly invalid lowest cell.
        const auto support = strict_support(rho, std::pow(10.0, log_T_min), composition);
        if (support == tabular_eos::FreeEnergyStatus::invalid_native_domain)
            return native_failure(support);
        for (int field = 0; field < tabular_eos::FieldCount; ++field)
            if (!free_energy_fields[field]) return native_failure(tabular_eos::FreeEnergyStatus::invalid_native_cell);
        const auto inverse = tabular_eos::invert_free_energy_temperature(
            n_T, log_T_min, dlog_T, target,
            [&](int j, tabular_eos::ThermalPolynomial& polynomial) {
                return strict_thermal_polynomial(rho, composition, j, pressure, polynomial);
            },
            [&](double T) {
                const auto f = interpolate_free_energy(rho, T, composition);
                return pressure ? rho * f.ax : tabular_eos::specific_energy(f, energy_reference_shift);
            },
            [&](double T) { return free_energy_result(rho, T, composition); }, log_T_max);
        if (inverse.status != tabular_eos::FreeEnergyStatus::success)
            return native_failure(inverse.status);
        return inverse.temperature;
    }

    ARCH_HEAVY_INLINE tabular_eos::FreeEnergyState interpolate_free_energy(
        double rho, double T, double composition,
        eos_utils::LinearCompositionDerivatives* derivatives = nullptr) const
    {
        const double log_rho = strict_domain
            ? std::max(log_rho_min, std::min(std::log10(rho), log_rho_max)) : std::log10(rho);
        const double log_temperature = strict_domain
            ? std::max(log_T_min, std::min(std::log10(T), log_T_max)) : std::log10(T);
        int i = static_cast<int>((log_rho - log_rho_min) / dlog_rho);
        int j = static_cast<int>((log_temperature - log_T_min) / dlog_T);
        int k = static_cast<int>((composition - X_min) / dX);
        i = std::max(0, std::min(i, n_rho - 2));
        j = std::max(0, std::min(j, n_T - 2));
        k = std::max(0, std::min(k, n_X - 2));
        const auto composition_axis = tabular_eos::locate_axis(
            composition, n_X, X_min, dX, strict_domain ? axis_nodes[2] : nullptr);
        if (strict_domain) k = composition_axis.lower;
        const double composition_width = strict_domain ? composition_axis.width : dX;

        const double tx =
            (log_rho - (log_rho_min + i * dlog_rho)) / dlog_rho;
        const double ty =
            (log_temperature - (log_T_min + j * dlog_T)) / dlog_T;
        const double tc = strict_domain ? composition_axis.fraction
            : (composition - (X_min + k * dX)) / dX;
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
            derivatives->energy[0] = ((upper.a - upper.ay) - (lower.a - lower.ay)) / composition_width;
            derivatives->cv[0] = ((upper.ay - upper.ayy) - (lower.ay - lower.ayy)) / (T * composition_width);
            derivatives->energy_temperature[0] = derivatives->cv[0];
            derivatives->cv_temperature = (2.0 * state.ayy - state.ay - state.ayyy) / (T * T);
        }
        return state;
    }

    ARCH_INLINE tabular_eos::FreeEnergyResult free_energy_result(
        double rho, double T, double composition) const
    {
        if (strict_domain) {
            const auto support = strict_support(rho, T, composition);
            if (support != tabular_eos::FreeEnergyStatus::success)
                return tabular_eos::free_energy_failure(support);
        }
        return tabular_eos::evaluate_thermodynamics(
            interpolate_free_energy(rho, T, composition), rho, T,
            strict_domain ? energy_reference_shift : 0.0);
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
        if (strict_domain && !native_direct) {
            const auto state = free_energy_state(rho, T, get_target_X(Xi));
            if (!std::isfinite(state.energy)) {
                d.energy.fill(state.energy); d.energy_temperature.fill(state.energy);
                d.cv.fill(state.energy); d.energy_hessian.fill(state.energy);
                d.cv_temperature = state.energy;
                return d;
            }
            interpolate_free_energy(rho, T, get_target_X(Xi), &d);
            return d;
        }
        if (native_direct) {
            const auto state = native_state(rho, T, Xi);
            if (!std::isfinite(state.energy)) {
                d.energy.fill(state.energy);
                d.energy_temperature.fill(state.energy);
                d.cv.fill(state.energy);
                d.energy_hessian.fill(state.energy);
                d.cv_temperature = state.energy;
                return d;
            }
        } else if (rho <= 1e-12 || T <= 1e-12) return d;
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
            double capacity = 0.0;
            const double energy = interpolate_3d(table_E, rho, T, X,
                &d.energy[0], &capacity,
                &d.energy_temperature[0], &d.energy_hessian[0]);
            if (native_direct) {
                d.cv[0] = d.energy_temperature[0];
                d.cv_temperature = capacity
                    * (capacity / (energy - energy_transform.offset) - 1.0 / T);
            } else {
                interpolate_3d(table_cv, rho, T, X, &d.cv[0], &d.cv_temperature);
            }
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
if (native_direct) return native_state(rho, T, Xi).pressure;
        if (strict_domain) return free_energy_state(rho, T, get_target_X(Xi)).pressure;
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
        if (native_direct)
return native_state(rho, native_temperature(rho, e, Xi), Xi).pressure;
        if (strict_domain) return free_energy_state(rho, strict_temperature(rho, e, Xi), get_target_X(Xi)).pressure;
        if (rho <= 1e-12 || e <= 1e-12)
            return 0.0;
        double T = get_temperature(rho, e, Xi);
        return get_pressure_from_rho_T(rho, T, Xi);
    }

    ARCH_INLINE double get_eint_from_T(double rho, double T_target, const double *Xi) const
    {
if (native_direct) return native_state(rho, T_target, Xi).energy;
        if (strict_domain) return free_energy_state(rho, T_target, get_target_X(Xi)).energy;
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
if (native_direct) return native_state(rho, T_target, Xi).cv;
        if (strict_domain) return free_energy_state(rho, T_target, get_target_X(Xi)).cv;
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
if (native_direct) return native_temperature(rho, e, Xi);
        if (strict_domain) return strict_temperature(rho, e, Xi);
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
        if (native_direct)
return native_state(U.rho, native_temperature(U.rho, e_int, Xi), Xi).sound_speed;
        if (strict_domain) return free_energy_state(U.rho, strict_temperature(U.rho, e_int, Xi), get_target_X(Xi)).sound_speed;
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
        if (native_direct)
return native_state(rho, native_temperature(rho, e, Xi), Xi).gamma1;
        if (strict_domain) return free_energy_state(rho, strict_temperature(rho, e, Xi), get_target_X(Xi)).gamma1;
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
if (native_direct) return native_state(rho, T, Xi).sound_speed;
        if (strict_domain) return free_energy_state(rho, T, get_target_X(Xi)).sound_speed;
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
        if (native_direct)
return native_state(rho, native_temperature(rho, e, Xi), Xi).dp_drho_e;
        if (strict_domain) return free_energy_state(rho, strict_temperature(rho, e, Xi), get_target_X(Xi)).dp_drho_e;
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
        if (native_direct)
return native_state(rho, native_temperature(rho, e, Xi), Xi).dp_de_rho;
        if (strict_domain) return free_energy_state(rho, strict_temperature(rho, e, Xi), get_target_X(Xi)).dp_de_rho;
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
        if (native_direct) {
            const double temperature = native_temperature(rho, p, Xi, true);
            return rho * (get_eint_from_T(rho, temperature, Xi)
                + 0.5 * (u*u + v*v + w*w));
        }
        if (strict_domain) {
            const double T = strict_temperature(rho, p, Xi, true);
            return rho * (get_eint_from_T(rho, T, Xi) + 0.5 * (u*u + v*v + w*w));
        }
        return eos_utils::solve_total_energy(*this, rho, u, v, w, p, Xi);
    }

    ARCH_INLINE double get_eta(double rho, double T, const double* Xi) const
    {
        if (strict_domain && !native_direct) {
            const auto state = free_energy_state(rho, T, get_target_X(Xi));
            if (!std::isfinite(state.energy)) return state.energy;
        }
        return 0.0;
    }

    // Pipeline: evaluate_state
    ARCH_INLINE void evaluate_state(eos_state_t& state) const {
        if (native_direct) {
            const auto native = native_state(state.rho, state.T, state.Xi);
            state.P = native.pressure;
            state.E = native.energy;
            state.cv = native.cv;
            state.sound_speed = native.sound_speed;
            state.dp_drho = native.dp_drho_e;
            state.dp_dT = native.dp_dT;
            state.pele = state.xne = state.eta = 0.0;
            return;
        }
        if (strict_domain) {
            const auto thermal = free_energy_state(state.rho, state.T, get_target_X(state.Xi));
            state.P = thermal.pressure; state.E = thermal.energy; state.cv = thermal.cv;
            state.sound_speed = thermal.sound_speed; state.dp_drho = thermal.dp_drho_e;
            state.dp_dT = thermal.dp_dT;
            state.pele = state.xne = state.eta = 0.0;
            return;
        }
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
    std::array<std::size_t, 3> axis_extents{};
    std::size_t dp_de_extent = 0, valid_extent = 0;
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
    std::vector<double> h_table_dP_de, h_table_valid;
    std::array<std::vector<double>, 3> h_axis_nodes;

    std::array<std::vector<double>, tabular_eos::FieldCount> h_free_energy_fields;
    const SpeciesManager *specs_owner = nullptr;
    Tabular3DEOSHostView view;
    void load_eosdriver(const std::string& path, const SpeciesManager* species);
    void load_baryon_ascii(const std::string& path, const SpeciesManager* species,
                           const std::string& helm_path);

public:
    Tabular3DEOS(const std::string &h5_filename, const SpeciesManager *specs_ptr = nullptr,
                const std::string& helm_path = "EOS_toolkit/tables/helmholtz/helm_table.dat");

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
        rebound.table_dP_de = h_table_dP_de.empty() ? nullptr : h_table_dP_de.data();
        rebound.table_valid = h_table_valid.empty() ? nullptr : h_table_valid.data();
        rebound.dp_de_extent = h_table_dP_de.size();
        rebound.valid_extent = h_table_valid.size();
        for (int axis = 0; axis < 3; ++axis) {
            rebound.axis_nodes[axis] = h_axis_nodes[axis].empty()
                ? nullptr : h_axis_nodes[axis].data();
            rebound.axis_extents[axis] = h_axis_nodes[axis].size();
        }
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
