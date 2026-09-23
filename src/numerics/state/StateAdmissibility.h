/**
 * @file StateAdmissibility.h
 * @brief Check and minimally repair conserved states using the configured physical floors.
 *
 * Workflow:
 * 1. Receive an explicit mesh/operator and signed cell-centered fields.
 * 2. Check and minimally repair conserved states using the configured physical floors.
 * 3. Return corrections or fluxes through the shared numerical contract.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include "data/FluidState.h"

// State recovery is independent of a density scale. Physical floors belong to
// NumericsConfig; EOS validity belongs to the selected thermodynamic model.
namespace arch::state {
enum class Status : unsigned char {
    valid, repaired, nonfinite, nonpositive_density, unresolved_energy,
    energy_ceiling, invalid_composition, invalid_thermodynamics
};

// Borrowed numeric limits lowered from NumericsConfig, never extra user controls.
struct Bounds {
    double density = 0.0;
    double internal_min = 0.0;
    double internal_max = std::numeric_limits<double>::max();
};

struct Kinematics {
    double u{}, v{}, w{}, kinetic{}, internal{};
    Status status = Status::valid;
};

ARCH_INLINE double invalid() { return std::numeric_limits<double>::quiet_NaN(); }

ARCH_INLINE Kinematics recover(const FluidVector& state)
{
    Kinematics result;
    if (!std::isfinite(state.rho) || !std::isfinite(state.eng)
        || !std::isfinite(state.mom_u) || !std::isfinite(state.mom_v)
        || !std::isfinite(state.mom_w)) result.status = Status::nonfinite;
    else if (!(state.rho > 0.0)) result.status = Status::nonpositive_density;
    if (result.status != Status::valid) {
        result.u = result.v = result.w = result.internal = invalid();
        return result;
    }
    result.u = state.mom_u / state.rho;
    result.v = state.mom_v / state.rho;
    result.w = state.mom_w / state.rho;
    // m dot v avoids squaring tiny momenta or tiny density. Half each product
    // before adding to avoid an unnecessary intermediate factor-of-two overflow.
    // e_kin density = (rho*u^2 + rho*v^2 + rho*w^2)/2.
    result.kinetic = (0.5 * state.mom_u) * result.u
                   + (0.5 * state.mom_v) * result.v
                   + (0.5 * state.mom_w) * result.w;
    const double thermal = state.eng - result.kinetic;
    result.internal = thermal / state.rho;
    if (!std::isfinite(result.u) || !std::isfinite(result.v)
        || !std::isfinite(result.w) || !std::isfinite(result.kinetic)
        || !std::isfinite(result.internal)) result.status = Status::nonfinite;
    // A cancellation-dominated residual cannot be turned into a measured
    // temperature by applying an energy floor. The bound is in energy density.
    else if (!(thermal > 0.0) || thermal <= 8.0 * std::numeric_limits<double>::epsilon()
             * std::max(std::abs(state.eng), result.kinetic))
        result.status = Status::unresolved_energy;
    if (result.status != Status::valid)
        result.u = result.v = result.w = result.internal = invalid();
    return result;
}

struct Repair {
    Status status = Status::valid;
    FluidVector delta{};
};

ARCH_INLINE Repair apply_bounds(FluidVector& state, double density_floor,
                               double energy_floor, double energy_ceiling)
{
    Repair report;
    const auto k = recover(state);
    report.status = k.status;
    if (k.status != Status::valid) return report;
    if (k.internal > energy_ceiling) {
        report.status = Status::energy_ceiling;
        return report;
    }
    if (state.rho >= density_floor && k.internal >= energy_floor) return report;
    const auto before = state;
    state.rho = std::max(state.rho, density_floor);
    state.mom_u = state.rho * k.u;
    state.mom_v = state.rho * k.v;
    state.mom_w = state.rho * k.w;
    // E_new = rho_new*max(e_int,e_floor) + |momentum_new|^2/(2*rho_new).
    state.eng = state.rho * std::max(k.internal, energy_floor)
              + (0.5 * state.mom_u) * k.u + (0.5 * state.mom_v) * k.v
              + (0.5 * state.mom_w) * k.w;
    if (!std::isfinite(state.eng)) { state = before; report.status = Status::nonfinite; return report; }
    report.delta = state - before;
    report.status = Status::repaired;
    return report;
}

// Validation after conservative transfers/reflux never replaces an average.
// These operations may fail; a hidden projection would break conservation.
ARCH_INLINE Status validate(const FluidVector& fluid, const double* fractions,
                            int species, int stride, double density_floor,
                            double energy_floor, double energy_ceiling)
{
    const auto k=recover(fluid);
    if (k.status!=Status::valid) return k.status;
    if (k.internal>energy_ceiling) return Status::energy_ceiling;
    if (fluid.rho<density_floor || k.internal<energy_floor) return Status::invalid_thermodynamics;
    double sum=0.0;
    for (int i=0;i<species;++i) {
        const double x=fractions[i*stride];
        if (!std::isfinite(x) || x<0.0) return Status::invalid_composition;
        sum+=x;
    }
    if (species && std::abs(sum-1.0)>512.0*species*std::numeric_limits<double>::epsilon())
        return Status::invalid_composition;
    return Status::valid;
}

ARCH_INLINE bool accepted(Status status)
{ return status == Status::valid || status == Status::repaired; }

// Xi are mass fractions, not molar abundances. Project only onto the requested
// simplex; zero species are allowed when floor=0. Invalid input never invents
// a uniform mixture. The residual species absorbs only final rounding error.
ARCH_INLINE bool normalize_composition(double* fractions, int count, int stride = 1,
                                       double floor = 0.0)
{
    if (count == 0) return true;
    if (!(floor >= 0.0) || !(count * floor < 1.0)) return false;
    double sum = 0.0;
    int largest = 0;
    for (int i = 0; i < count; ++i) {
        const double x = fractions[i * stride];
        if (!std::isfinite(x) || x < -64.0 * std::numeric_limits<double>::epsilon()) return false;
        sum += std::max(x, 0.0);
        if (x > fractions[largest * stride]) largest = i;
    }
    if (!(sum > 0.0) || !std::isfinite(sum)) return false;
    double deficit = 0.0, surplus = 0.0;
    for (int i = 0; i < count; ++i) {
        fractions[i * stride] = std::max(fractions[i * stride], 0.0) / sum;
        deficit += std::max(floor - fractions[i * stride], 0.0);
        surplus += std::max(fractions[i * stride] - floor, 0.0);
    }
    // Restore the requested floor after normalization by drawing only from
    // abundance above that floor. No extra seed is added to valid trace species.
    if (deficit > 0.0 && !(surplus > deficit)) return false;
    const double retained = deficit > 0.0 ? 1.0 - deficit / surplus : 1.0;
    double normalized = 0.0;
    for (int i = 0; i < count; ++i) {
        if (deficit > 0.0)
            fractions[i * stride] = floor + retained * std::max(fractions[i * stride] - floor, 0.0);
        normalized += fractions[i * stride];
    }
    fractions[largest * stride] += 1.0 - normalized;
    return fractions[largest * stride] >= floor;
}
} // namespace arch::state
