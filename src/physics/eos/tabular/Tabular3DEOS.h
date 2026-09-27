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

#include "physics/eos/eos.h"
#include "physics/eos/eos_Utils.h"
#include "physics/eos/tabular/TabularFreeEnergy.h"
#include "physics/eos/tabular/TabularInversion.h"
#include "physics/eos/tabular/TabularInterpolation.h"

#include "physics/species/Species.h"
#include "physics/constant/PhysicalConstants.h"

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

    // Strict domains apply to both native and free-energy sources.

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
            + (state.dp_de_rho / rho) * (state.pressure / rho);
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
            || std::abs(recovered - energy) > 2.0e-12 * std::max(std::abs(energy), std::abs(recovered)))
            return native_failure(tabular_eos::FreeEnergyStatus::invalid_temperature_inversion);
        return temperature;
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
        if (native_direct || n_rho < 2 || n_T < 2 || n_X < 2 ||
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
        const double log_rho = std::max(log_rho_min, std::min(std::log10(rho), log_rho_max));
        const double log_temperature = std::max(log_T_min, std::min(std::log10(T), log_T_max));
        int i = static_cast<int>((log_rho - log_rho_min) / dlog_rho);
        int j = static_cast<int>((log_temperature - log_T_min) / dlog_T);
        int k = static_cast<int>((composition - X_min) / dX);
        i = std::max(0, std::min(i, n_rho - 2));
        j = std::max(0, std::min(j, n_T - 2));
        k = std::max(0, std::min(k, n_X - 2));
        const auto composition_axis = tabular_eos::locate_axis(
            composition, n_X, X_min, dX, axis_nodes[2]);
        k = composition_axis.lower;
        const double composition_width = composition_axis.width;

        const double tx =
            (log_rho - (log_rho_min + i * dlog_rho)) / dlog_rho;
        const double ty =
            (log_temperature - (log_T_min + j * dlog_T)) / dlog_T;
        const double tc = composition_axis.fraction;
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
        {
            const auto support = strict_support(rho, T, composition);
            if (support != tabular_eos::FreeEnergyStatus::success)
                return tabular_eos::free_energy_failure(support);
        }
        return tabular_eos::evaluate_thermodynamics(
            interpolate_free_energy(rho, T, composition), rho, T,
            energy_reference_shift);
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
            if (!Xi || target_species_id >= specs.count)
                return native_failure(tabular_eos::FreeEnergyStatus::invalid_native_domain);
            return Xi[target_species_id];
        }

        // Otherwise derive electron fraction Ye when species metadata is available.
        if (specs.count > 0)
        {
            return specs.calc_Ye(Xi);
        }

        // Composition metadata is required; no neutral-matter value is guessed.
        return native_failure(tabular_eos::FreeEnergyStatus::invalid_native_domain);
    }

    // Linear composition coordinates and their species chain rule.
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
        if (!native_direct) {
            const auto state = free_energy_state(rho, T, get_target_X(Xi));
            if (!std::isfinite(state.energy)) {
                d.energy.fill(state.energy); d.energy_temperature.fill(state.energy);
                d.cv.fill(state.energy); d.energy_hessian.fill(state.energy);
                d.cv_temperature=state.energy;
                return d;
            }
            interpolate_free_energy(rho, T, get_target_X(Xi), &d);
        } else {
            const auto state = native_state(rho, T, Xi);
            if (!std::isfinite(state.energy)) {
                d.energy.fill(state.energy); d.energy_temperature.fill(state.energy);
                d.cv.fill(state.energy); d.energy_hessian.fill(state.energy);
                d.cv_temperature=state.energy;
                return d;
            }
            double capacity=0.0;
            const double energy=interpolate_3d(table_E,rho,T,get_target_X(Xi),
                &d.energy[0],&capacity,&d.energy_temperature[0],&d.energy_hessian[0]);
            d.cv[0]=d.energy_temperature[0];
            d.cv_temperature=capacity*(capacity/(energy-energy_transform.offset)-1.0/T);
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
        return free_energy_state(rho, T, get_target_X(Xi)).pressure;
    }

    ARCH_INLINE double get_pressure_from_rho_e(double rho, double e, const double *Xi) const
    {
        if (native_direct) return native_state(rho, native_temperature(rho, e, Xi), Xi).pressure;
        return free_energy_state(rho, strict_temperature(rho, e, Xi), get_target_X(Xi)).pressure;
    }

    ARCH_INLINE double get_eint_from_T(double rho, double T_target, const double *Xi) const
    {
        if (native_direct) return native_state(rho, T_target, Xi).energy;
        return free_energy_state(rho, T_target, get_target_X(Xi)).energy;
    }

    ARCH_INLINE double get_cv(double rho, double T_target, const double *Xi) const
    {
        if (native_direct) return native_state(rho, T_target, Xi).cv;
        return free_energy_state(rho, T_target, get_target_X(Xi)).cv;
    }

    ARCH_HEAVY_INLINE double get_temperature(
        double rho, double e, const double *Xi) const
    {
        if (native_direct) return native_temperature(rho, e, Xi);
        return strict_temperature(rho, e, Xi);
    }

    ARCH_INLINE double get_pressure(const FluidVector &U, const double *Xi) const
    {
        double e_int = eos_utils::extract_specific_internal_energy(U);
        return get_pressure_from_rho_e(U.rho, e_int, Xi);
    }

    ARCH_INLINE double get_sound_speed(const FluidVector &U, double p, const double *Xi) const
    {
        const double e = eos_utils::extract_specific_internal_energy(U);
        if (native_direct) return native_state(U.rho, native_temperature(U.rho, e, Xi), Xi).sound_speed;
        return free_energy_state(U.rho, strict_temperature(U.rho, e, Xi), get_target_X(Xi)).sound_speed;
    }

    ARCH_INLINE double get_gamma(const double *Xi, double rho = 0.0, double e = 0.0) const
    {
        if (native_direct) return native_state(rho, native_temperature(rho, e, Xi), Xi).gamma1;
        return free_energy_state(rho, strict_temperature(rho, e, Xi), get_target_X(Xi)).gamma1;
    }

    ARCH_INLINE double get_sound_speed_from_rho_T(double rho, double T, const double *Xi) const
    {
        if (native_direct) return native_state(rho, T, Xi).sound_speed;
        return free_energy_state(rho, T, get_target_X(Xi)).sound_speed;
    }

    // Derivative interface using table data when present and finite differences otherwise.
    ARCH_INLINE double get_dp_drho_e(double rho, double e, const double *Xi) const
    {
        if (native_direct) return native_state(rho, native_temperature(rho, e, Xi), Xi).dp_drho_e;
        return free_energy_state(rho, strict_temperature(rho, e, Xi), get_target_X(Xi)).dp_drho_e;
    }

    ARCH_INLINE double get_dp_de_rho(double rho, double e, const double *Xi) const
    {
        if (native_direct) return native_state(rho, native_temperature(rho, e, Xi), Xi).dp_de_rho;
        return free_energy_state(rho, strict_temperature(rho, e, Xi), get_target_X(Xi)).dp_de_rho;
    }
    // Damped Newton inversion from pressure to total-energy density.
    ARCH_INLINE double get_total_energy_primitive(double rho, double u, double v, double w, double p, const double *Xi) const
    {
        const double T = native_direct ? native_temperature(rho, p, Xi, true)
                                        : strict_temperature(rho, p, Xi, true);
        return rho * get_eint_from_T(rho, T, Xi) + eos_utils::calc_kinetic_energy(rho, u, v, w);
    }

    ARCH_INLINE double get_eta(double rho, double T, const double* Xi) const
    {
        if (!native_direct) {
            const auto state = free_energy_state(rho, T, get_target_X(Xi));
            if (!std::isfinite(state.energy)) return state.energy;
        }
        return 0.0;
    }

    // Pipeline: evaluate_state
    ARCH_INLINE void evaluate_state(eos_state_t& state) const {
        if (native_direct) {
            const auto t = native_state(state.rho, state.T, state.Xi);
            state.P=t.pressure; state.E=t.energy; state.cv=t.cv;
            state.sound_speed=t.sound_speed; state.dp_drho=t.dp_drho_e; state.dp_dT=t.dp_dT;
        } else {
            const auto t = free_energy_state(state.rho, state.T, get_target_X(state.Xi));
            state.P=t.pressure; state.E=t.energy; state.cv=t.cv;
            state.sound_speed=t.sound_speed; state.dp_drho=t.dp_drho_e; state.dp_dT=t.dp_dT;
        }
        state.pele = state.xne = state.eta = 0.0;
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
