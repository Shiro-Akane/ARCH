/**
 * @file TabularFreeEnergy.h
 * @brief Thermodynamically consistent interpolation of specific Helmholtz free energy.
 */
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

#include "../../core/ArchPortability.h"

namespace tabular_eos {

enum FreeEnergyField : int {
    F = 0, Fx, Fy, Fxx, Fxy, Fyy, Fxxy, Fxyy, Fxxyy, FieldCount
};

struct FreeEnergyState {
    double a = 0.0;
    double ax = 0.0;
    double ay = 0.0;
    double axx = 0.0;
    double axy = 0.0;
    double ayy = 0.0;
};

struct ThermodynamicState {
    double pressure = 0.0;
    double energy = 0.0;
    double cv = 0.0;
    double dp_drho_e = 0.0;
    double dp_de_rho = 0.0;
    double dp_dT = 0.0;
    double sound_speed = 0.0;
    double gamma1 = 0.0;
};

enum class FreeEnergyStatus : int {
    success = 0,
    invalid_pressure_or_energy,
    invalid_heat_capacity,
    invalid_derivatives,
    invalid_sound_speed
};

struct FreeEnergyResult {
    ThermodynamicState state{};
    FreeEnergyStatus status = FreeEnergyStatus::success;
};

ARCH_INLINE ThermodynamicState invalid_thermodynamic_state()
{
    const double value = std::numeric_limits<double>::quiet_NaN();
    return {value, value, value, value, value, value, value, value};
}

ARCH_INLINE FreeEnergyResult free_energy_failure(FreeEnergyStatus status)
{
    return {invalid_thermodynamic_state(), status};
}

ARCH_INLINE int field_for_orders(int dx, int dy)
{
    if (dx == 0) return dy == 0 ? F : (dy == 1 ? Fy : Fyy);
    if (dx == 1) return dy == 0 ? Fx : (dy == 1 ? Fxy : Fxyy);
    return dy == 0 ? Fxx : (dy == 1 ? Fxxy : Fxxyy);
}

ARCH_INLINE double quintic_coefficient(int endpoint, int derivative_order,
                                       int degree)
{
    if (endpoint == 0) {
        if (derivative_order == 0) {
            const double values[6]{1.0, 0.0, 0.0, -10.0, 15.0, -6.0};
            return values[degree];
        }
        if (derivative_order == 1) {
            const double values[6]{0.0, 1.0, 0.0, -6.0, 8.0, -3.0};
            return values[degree];
        }
        const double values[6]{0.0, 0.0, 0.5, -1.5, 1.5, -0.5};
        return values[degree];
    }
    if (derivative_order == 0) {
        const double values[6]{0.0, 0.0, 0.0, 10.0, -15.0, 6.0};
        return values[degree];
    }
    if (derivative_order == 1) {
        const double values[6]{0.0, 0.0, 0.0, -4.0, 7.0, -3.0};
        return values[degree];
    }
    const double values[6]{0.0, 0.0, 0.0, 0.5, -1.0, 0.5};
    return values[degree];
}

ARCH_INLINE double polynomial_derivative(int endpoint, int derivative_order,
                                         double t, int order)
{
    double result = 0.0;
    for (int degree = order; degree <= 5; ++degree) {
        double factor = 1.0;
        for (int k = 0; k < order; ++k) factor *= degree - k;
        result += quintic_coefficient(endpoint, derivative_order, degree) *
                  factor * std::pow(t, degree - order);
    }
    return result;
}

ARCH_INLINE double quintic_basis(int endpoint, int derivative_order,
                                 double t, int query_derivative)
{
    return polynomial_derivative(endpoint, derivative_order, t,
                                 query_derivative);
}

ARCH_INLINE double scaled_basis(int endpoint, int stored_derivative,
                                int requested_derivative, double t,
                                double spacing)
{
    return std::pow(spacing, stored_derivative - requested_derivative) *
           quintic_basis(endpoint, stored_derivative, t,
                         requested_derivative);
}

ARCH_INLINE FreeEnergyState interpolate_biquintic(
    const std::array<const double*, FieldCount>& fields,
    const std::array<std::size_t, 4>& corners,
    double tx, double ty, double hx, double hy)
{
    FreeEnergyState state{};
    double* outputs[] = {
        &state.a, &state.ax, &state.ay,
        &state.axx, &state.axy, &state.ayy
    };
    for (int output = 0; output < 6; ++output) {
        const int qx = output == 1 ? 1 : (output == 3 ? 2 :
                       (output == 4 ? 1 : 0));
        const int qy = output == 2 ? 1 : (output == 4 ? 1 :
                       (output == 5 ? 2 : 0));
        double value = 0.0;
        for (int ex = 0; ex < 2; ++ex) {
            for (int ey = 0; ey < 2; ++ey) {
                const std::size_t corner = corners[2 * ex + ey];
                for (int dx = 0; dx <= 2; ++dx) {
                    const double bx =
                        scaled_basis(ex, dx, qx, tx, hx);
                    for (int dy = 0; dy <= 2; ++dy) {
                        const double by =
                            scaled_basis(ey, dy, qy, ty, hy);
                        value += bx * by *
                                 fields[field_for_orders(dx, dy)][corner];
                    }
                }
            }
        }
        *outputs[output] = value;
    }
    return state;
}

ARCH_INLINE FreeEnergyState blend(const FreeEnergyState& lower,
                                  const FreeEnergyState& upper,
                                  double fraction)
{
    FreeEnergyState out{};
    out.a = lower.a + fraction * (upper.a - lower.a);
    out.ax = lower.ax + fraction * (upper.ax - lower.ax);
    out.ay = lower.ay + fraction * (upper.ay - lower.ay);
    out.axx = lower.axx + fraction * (upper.axx - lower.axx);
    out.axy = lower.axy + fraction * (upper.axy - lower.axy);
    out.ayy = lower.ayy + fraction * (upper.ayy - lower.ayy);
    return out;
}

ARCH_INLINE FreeEnergyResult evaluate_thermodynamics(
    const FreeEnergyState& f, double rho, double temperature)
{
    FreeEnergyResult result{};
    ThermodynamicState& state = result.state;
    state.pressure = rho * f.ax;
    state.energy = f.a - f.ay;
    if (!(state.pressure > 0.0) || !std::isfinite(state.pressure) ||
        !(state.energy > 0.0) || !std::isfinite(state.energy)) {
        return free_energy_failure(
            FreeEnergyStatus::invalid_pressure_or_energy);
    }
    state.cv = (f.ay - f.ayy) / temperature;
    if (!(state.cv > 0.0) || !std::isfinite(state.cv)) {
        return free_energy_failure(FreeEnergyStatus::invalid_heat_capacity);
    }

    state.dp_dT = rho * f.axy / temperature;
    const double dp_drho_T = f.ax + f.axx;
    const double de_drho_T = (f.ax - f.axy) / rho;
    state.dp_drho_e =
        dp_drho_T - state.dp_dT * de_drho_T / state.cv;
    state.dp_de_rho = state.dp_dT / state.cv;
    if (!std::isfinite(state.dp_dT) || !std::isfinite(dp_drho_T) ||
        !std::isfinite(de_drho_T) || !std::isfinite(state.dp_drho_e) ||
        !std::isfinite(state.dp_de_rho)) {
        return free_energy_failure(FreeEnergyStatus::invalid_derivatives);
    }

    const double sound_speed_squared =
        state.dp_drho_e +
        state.dp_de_rho * state.pressure / (rho * rho);
    if (!(sound_speed_squared > 0.0) ||
        !std::isfinite(sound_speed_squared)) {
        return free_energy_failure(FreeEnergyStatus::invalid_sound_speed);
    }
    state.sound_speed = std::sqrt(sound_speed_squared);
    state.gamma1 = rho * sound_speed_squared / state.pressure;
    return result;
}

inline ThermodynamicState require_thermodynamics(const FreeEnergyResult& result)
{
    switch (result.status) {
    case FreeEnergyStatus::success:
        return result.state;
    case FreeEnergyStatus::invalid_pressure_or_energy:
        throw std::runtime_error(
            "Tabular EOS free energy produced non-positive or non-finite pressure or energy");
    case FreeEnergyStatus::invalid_heat_capacity:
        throw std::runtime_error(
            "Tabular EOS free energy produced non-positive or non-finite cv");
    case FreeEnergyStatus::invalid_derivatives:
        throw std::runtime_error(
            "Tabular EOS free energy produced non-finite derivatives");
    case FreeEnergyStatus::invalid_sound_speed:
        throw std::runtime_error(
            "Tabular EOS free energy produced non-positive sound speed squared");
    }
    throw std::runtime_error("Tabular EOS free energy produced an unknown error");
}

inline ThermodynamicState to_thermodynamics(const FreeEnergyState& f,
                                            double rho, double temperature)
{
    return require_thermodynamics(evaluate_thermodynamics(f, rho, temperature));
}

inline double derivative_sample(const std::vector<double>& input,
                                std::size_t base, std::size_t stride,
                                int index, int count, double spacing)
{
    auto at = [&](int i) -> double {
        return input[base + static_cast<std::size_t>(i) * stride];
    };
    if (count < 5) {
        throw std::runtime_error(
            "Free-energy tables require at least five rho and temperature points");
    }
    if (index == 0) {
        return (-25.0 * at(0) + 48.0 * at(1) - 36.0 * at(2) +
                16.0 * at(3) - 3.0 * at(4)) / (12.0 * spacing);
    }
    if (index == 1) {
        return (-3.0 * at(0) - 10.0 * at(1) + 18.0 * at(2) -
                6.0 * at(3) + at(4)) / (12.0 * spacing);
    }
    if (index == count - 2) {
        return (3.0 * at(count - 1) + 10.0 * at(count - 2) -
                18.0 * at(count - 3) + 6.0 * at(count - 4) -
                at(count - 5)) / (12.0 * spacing);
    }
    if (index == count - 1) {
        return (25.0 * at(count - 1) - 48.0 * at(count - 2) +
                36.0 * at(count - 3) - 16.0 * at(count - 4) +
                3.0 * at(count - 5)) / (12.0 * spacing);
    }
    return (-at(index + 2) + 8.0 * at(index + 1) -
            8.0 * at(index - 1) + at(index - 2)) /
           (12.0 * spacing);
}

inline std::vector<double> differentiate(
    const std::vector<double>& input, int n_rho, int n_temperature,
    int n_composition, bool density_axis, double spacing)
{
    std::vector<double> output(input.size(), 0.0);
    if (density_axis) {
        const std::size_t stride =
            static_cast<std::size_t>(n_temperature) * n_composition;
        for (int j = 0; j < n_temperature; ++j) {
            for (int k = 0; k < n_composition; ++k) {
                const std::size_t base =
                    static_cast<std::size_t>(j) * n_composition + k;
                for (int i = 0; i < n_rho; ++i) {
                    output[base + static_cast<std::size_t>(i) * stride] =
                        derivative_sample(input, base, stride, i, n_rho, spacing);
                }
            }
        }
    } else {
        const std::size_t stride = static_cast<std::size_t>(n_composition);
        for (int i = 0; i < n_rho; ++i) {
            for (int k = 0; k < n_composition; ++k) {
                const std::size_t base =
                    static_cast<std::size_t>(i) * n_temperature *
                    n_composition + k;
                for (int j = 0; j < n_temperature; ++j) {
                    output[base + static_cast<std::size_t>(j) * stride] =
                        derivative_sample(input, base, stride, j,
                                          n_temperature, spacing);
                }
            }
        }
    }
    return output;
}

inline std::array<std::vector<double>, FieldCount>
build_derivative_fields(const std::vector<double>& free_energy,
                        int n_rho, int n_temperature, int n_composition,
                        double d_ln_rho, double d_ln_temperature)
{
    std::array<std::vector<double>, FieldCount> fields;
    fields[F] = free_energy;
    fields[Fx] = differentiate(fields[F], n_rho, n_temperature,
                               n_composition, true, d_ln_rho);
    fields[Fy] = differentiate(fields[F], n_rho, n_temperature,
                               n_composition, false, d_ln_temperature);
    fields[Fxx] = differentiate(fields[Fx], n_rho, n_temperature,
                                n_composition, true, d_ln_rho);
    fields[Fxy] = differentiate(fields[Fx], n_rho, n_temperature,
                                n_composition, false, d_ln_temperature);
    fields[Fyy] = differentiate(fields[Fy], n_rho, n_temperature,
                                n_composition, false, d_ln_temperature);
    fields[Fxxy] = differentiate(fields[Fxx], n_rho, n_temperature,
                                 n_composition, false, d_ln_temperature);
    fields[Fxyy] = differentiate(fields[Fxy], n_rho, n_temperature,
                                 n_composition, false, d_ln_temperature);
    fields[Fxxyy] = differentiate(fields[Fxxy], n_rho, n_temperature,
                                  n_composition, false, d_ln_temperature);
    return fields;
}

} // namespace tabular_eos
