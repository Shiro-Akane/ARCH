/**
 * @file TabularFreeEnergy.h
 * @brief Thermodynamically consistent interpolation of specific Helmholtz free energy.
 */
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

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

inline int field_for_orders(int dx, int dy)
{
    static constexpr int map[3][3] = {
        {F, Fy, Fyy},
        {Fx, Fxy, Fxyy},
        {Fxx, Fxxy, Fxxyy}
    };
    return map[dx][dy];
}

inline double polynomial_derivative(const std::array<double, 6>& coefficients,
                                    double t, int order)
{
    double result = 0.0;
    for (int degree = order; degree <= 5; ++degree) {
        double factor = 1.0;
        for (int k = 0; k < order; ++k) factor *= degree - k;
        result += coefficients[degree] * factor *
                  std::pow(t, degree - order);
    }
    return result;
}

inline double quintic_basis(int endpoint, int derivative_order,
                            double t, int query_derivative)
{
    static constexpr std::array<std::array<std::array<double, 6>, 3>, 2>
        coefficients{{
            {{{1.0, 0.0, 0.0, -10.0, 15.0, -6.0},
              {0.0, 1.0, 0.0, -6.0, 8.0, -3.0},
              {0.0, 0.0, 0.5, -1.5, 1.5, -0.5}}},
            {{{0.0, 0.0, 0.0, 10.0, -15.0, 6.0},
              {0.0, 0.0, 0.0, -4.0, 7.0, -3.0},
              {0.0, 0.0, 0.0, 0.5, -1.0, 0.5}}}
        }};
    return polynomial_derivative(
        coefficients[endpoint][derivative_order], t, query_derivative);
}

inline double scaled_basis(int endpoint, int stored_derivative,
                           int requested_derivative, double t, double spacing)
{
    return std::pow(spacing, stored_derivative - requested_derivative) *
           quintic_basis(endpoint, stored_derivative, t,
                         requested_derivative);
}

inline FreeEnergyState interpolate_biquintic(
    const std::array<const double*, FieldCount>& fields,
    const std::array<std::size_t, 4>& corners,
    double tx, double ty, double hx, double hy)
{
    FreeEnergyState state{};
    double* outputs[] = {
        &state.a, &state.ax, &state.ay,
        &state.axx, &state.axy, &state.ayy
    };
    static constexpr int requested[6][2] = {
        {0, 0}, {1, 0}, {0, 1}, {2, 0}, {1, 1}, {0, 2}
    };

    for (int output = 0; output < 6; ++output) {
        const int qx = requested[output][0];
        const int qy = requested[output][1];
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

inline FreeEnergyState blend(const FreeEnergyState& lower,
                             const FreeEnergyState& upper, double fraction)
{
    FreeEnergyState out{};
    auto mix = [fraction](double lo, double hi) {
        return lo + fraction * (hi - lo);
    };
    out.a = mix(lower.a, upper.a);
    out.ax = mix(lower.ax, upper.ax);
    out.ay = mix(lower.ay, upper.ay);
    out.axx = mix(lower.axx, upper.axx);
    out.axy = mix(lower.axy, upper.axy);
    out.ayy = mix(lower.ayy, upper.ayy);
    return out;
}

inline ThermodynamicState to_thermodynamics(const FreeEnergyState& f,
                                            double rho, double temperature)
{
    ThermodynamicState state{};
    state.pressure = rho * f.ax;
    state.energy = f.a - f.ay;
    if (!(state.pressure > 0.0) || !std::isfinite(state.pressure) ||
        !(state.energy > 0.0) || !std::isfinite(state.energy)) {
        throw std::runtime_error(
            "Tabular EOS free energy produced non-positive or non-finite pressure or energy");
    }
    state.cv = (f.ay - f.ayy) / temperature;
    if (!(state.cv > 0.0) || !std::isfinite(state.cv)) {
        throw std::runtime_error(
            "Tabular EOS free energy produced non-positive or non-finite cv");
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
        throw std::runtime_error(
            "Tabular EOS free energy produced non-finite derivatives");
    }

    const double sound_speed_squared =
        state.dp_drho_e +
        state.dp_de_rho * state.pressure / (rho * rho);
    if (!(sound_speed_squared > 0.0) ||
        !std::isfinite(sound_speed_squared)) {
        throw std::runtime_error(
            "Tabular EOS free energy produced non-positive sound speed squared");
    }
    state.sound_speed = std::sqrt(sound_speed_squared);
    state.gamma1 = rho * sound_speed_squared / state.pressure;
    return state;
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
