/**
 * @file TabularInversion.h
 * @brief Bounded all-root temperature inversion of the shared Hermite potential.
 *
 * Polynomial derivative roots isolate every monotone interval, including
 * turning points inside one table cell. Physical brackets and final residuals
 * are evaluated on the original interpolant; no extrapolation or ideal-gas
 * fallback is permitted. Invalid-mask cells are never bridged.
 */
#pragma once

#include <algorithm>
#include "TabularFreeEnergy.h"

namespace tabular_eos {

ARCH_INLINE double polynomial_value(const ThermalPolynomial& p, int degree, double x)
{
    double value = p[degree];
    for (int k = degree - 1; k >= 0; --k) value = value * x + p[k];
    return value;
}

// Build the derivative hierarchy from the top down, then isolate roots from
// the linear polynomial up. No recursion, allocation, or device-only solver.
ARCH_INLINE int stationary_points(const ThermalPolynomial& p, double* roots)
{
    ThermalPolynomial derivatives[5]{};
    for (int k = 1; k < 6; ++k) derivatives[0][k - 1] = k * p[k];
    int degree = 4;
    while (degree > 0 && derivatives[0][degree] == 0.0) --degree;
    for (int k = 0; k <= degree; ++k)
        if (!std::isfinite(derivatives[0][k])) return -1;
    if (degree == 0) return 0;
    for (int row = 1; row < degree; ++row)
        for (int k = 1; k <= degree - row + 1; ++k)
            derivatives[row][k - 1] = k * derivatives[row - 1][k];
    double previous[5]{};
    int previous_count = 0;
    for (int row = degree - 1; row >= 0; --row) {
        const int order = degree - row;
        const auto& poly = derivatives[row];
        double scale = 0.0;
        for (int k = 0; k <= order; ++k) {
            if (!std::isfinite(poly[k])) return -1;
            scale += std::abs(poly[k]);
        }
        if (!std::isfinite(scale)) return -1;
        const double tolerance = 64.0 * std::numeric_limits<double>::epsilon() * scale;
        double cuts[7]{};
        cuts[0] = 0.0;
        for (int k = 0; k < previous_count; ++k) cuts[k + 1] = previous[k];
        cuts[previous_count + 1] = 1.0;
        double current[5]{};
        int count = 0;
        auto append = [&](double root) {
            if (!(root > 0.0 && root < 1.0)) return;
            if (count > 0 && root == current[count - 1]) return;
            if (count < 5) current[count++] = root;
        };
        for (int k = 0; k <= previous_count; ++k) {
            double lo = cuts[k], hi = cuts[k + 1];
            double flo = polynomial_value(poly, order, lo);
            double fhi = polynomial_value(poly, order, hi);
            if (!std::isfinite(flo) || !std::isfinite(fhi)) return -1;
            if (std::abs(flo) <= tolerance) append(lo);
            if ((flo < 0.0 && fhi > 0.0) || (flo > 0.0 && fhi < 0.0)) {
                for (int iteration = 0; iteration < 64; ++iteration) {
                    const double mid = lo + 0.5 * (hi - lo);
                    if (mid == lo || mid == hi) break;
                    const double fm = polynomial_value(poly, order, mid);
                    if (!std::isfinite(fm)) return -1;
                    if (fm == 0.0) { lo = hi = mid; break; }
                    if ((flo < 0.0) == (fm < 0.0)) { lo = mid; flo = fm; }
                    else hi = mid;
                }
                append(lo + 0.5 * (hi - lo));
            }
        }
        if (count > order) return -1;
        previous_count = count;
        for (int k = 0; k < count; ++k) previous[k] = current[k];
    }
    for (int k = 0; k < previous_count; ++k) roots[k] = previous[k];
    return previous_count;
}

struct TemperatureInverse {
    double temperature = std::numeric_limits<double>::quiet_NaN();
    FreeEnergyStatus status = FreeEnergyStatus::invalid_temperature_inversion;
};

template <class PatchReader, class ValueReader, class StateReader>
ARCH_HEAVY_INLINE TemperatureInverse invert_free_energy_temperature(
    int n_temperature, double log_temperature_min, double dlog_temperature,
    double target, PatchReader patch, ValueReader value, StateReader state,
    double log_temperature_max = std::numeric_limits<double>::quiet_NaN())
{
    TemperatureInverse result{};
    if (!(target > 0.0) || !std::isfinite(target)) return result;
    int roots_found = 0;
    double last_temperature = -1.0;
    for (int j = 0; j < n_temperature - 1; ++j) {
        ThermalPolynomial polynomial{};
        if (!patch(j, polynomial)) continue;
        double cuts[7]{};
        const int count = stationary_points(polynomial, cuts + 1);
        if (count < 0) return result;
        cuts[0] = 0.0;
        cuts[count + 1] = 1.0;
        auto temperature_at = [&](double coordinate) {
            // Exact knot spelling also deduplicates a root shared by two cells.
            const double log_temperature = coordinate == 1.0
                ? ((j == n_temperature - 2 && std::isfinite(log_temperature_max))
                    ? log_temperature_max : log_temperature_min + (j + 1) * dlog_temperature)
                : log_temperature_min + (j + coordinate) * dlog_temperature;
            return std::pow(10.0, log_temperature);
        };
        for (int segment = 0; segment <= count; ++segment) {
            double lo = temperature_at(cuts[segment]);
            double hi = temperature_at(cuts[segment + 1]);
            double vlo = value(lo), vhi = value(hi);
            if (!std::isfinite(vlo) || !std::isfinite(vhi)) return result;
            // Strict physical bracket: nextafter outside an endpoint cannot be
            // pulled back into the table by a polynomial/log rounding tolerance.
            if (target < std::min(vlo, vhi) || target > std::max(vlo, vhi)) continue;
            if (vlo == vhi) {
                result.status = FreeEnergyStatus::ambiguous_temperature_inversion;
                return result;
            }
            double temperature = lo;
            if (target == vhi) temperature = hi;
            else if (target != vlo) {
                const bool increasing = vhi > vlo;
                for (int iteration = 0; iteration < 80; ++iteration) {
                    const double mid = lo + 0.5 * (hi - lo);
                    if (mid == lo || mid == hi) break;
                    const double vmid = value(mid);
                    if (!std::isfinite(vmid)) return result;
                    if (vmid == target) { lo = hi = mid; break; }
                    if ((vmid < target) == increasing) lo = mid;
                    else hi = mid;
                }
                temperature = lo + 0.5 * (hi - lo);
            }
            const auto physical = state(temperature);
            if (physical.status != FreeEnergyStatus::success) continue;
            const double recovered = value(temperature);
            if (!std::isfinite(recovered) || std::abs(recovered - target) >
                2.0e-12 * std::max(std::abs(target), 1.0)) continue;
            if (temperature == last_temperature) continue;
            last_temperature = temperature;
            result.temperature = temperature;
            if (++roots_found > 1) {
                result.status = FreeEnergyStatus::ambiguous_temperature_inversion;
                return result;
            }
        }
    }
    if (roots_found == 1) result.status = FreeEnergyStatus::success;
    return result;
}

} // namespace tabular_eos
