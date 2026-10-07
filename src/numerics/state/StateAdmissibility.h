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
// The same trace-sized band is used by Hydro and conservative-stage acceptance.
inline constexpr double composition_roundoff_limit =
    64.0 * std::numeric_limits<double>::epsilon();

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

/** Borrow the optional trial EOS without its required-state failure latch.
 * Workflow: reuse the checked view's existing candidate_view when available;
 * otherwise borrow the unchanged ordinary EOS by const reference. Required
 * donor/baseline validation never uses this selector; only high rays contract.
 */
template <class Eos>
ARCH_INLINE decltype(auto) candidate_eos(const Eos& eos)
{
    if constexpr (requires { eos.candidate_view(); }) return eos.candidate_view();
    else return (eos);
}

/** Validate configured physical limits without interpreting any state. */
ARCH_INLINE bool valid_bounds(const Bounds& bounds)
{
    return std::isfinite(bounds.density) && bounds.density >= 0.0
        && std::isfinite(bounds.internal_min) && bounds.internal_min >= 0.0
        && std::isfinite(bounds.internal_max)
        && bounds.internal_max >= bounds.internal_min;
}

/** Check the shared mass-fraction simplex without energy recovery or repair.
 * X_s >= 0 and |sum X_s - 1| <= 512*N*epsilon. The caller supplies a valid
 * extent; species-major views use their real cell stride. No normalization
 * or abundance floor is applied, including for representable trace species.
 */
ARCH_INLINE Status validate_composition(const double* fractions,int species,int stride)
{
    if (species < 0 || stride <= 0 || (species > 0 && !fractions))
        return Status::invalid_composition;
    double sum = 0.0;
    for (int s = 0; s < species; ++s) {
        const double x = fractions[static_cast<std::size_t>(s) * stride];
        if (!std::isfinite(x) || x < 0.0) return Status::invalid_composition;
        sum += x;
    }
    if (species && std::abs(sum - 1.0)
        > 512.0 * species * std::numeric_limits<double>::epsilon())
        return Status::invalid_composition;
    return Status::valid;
}

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
    return validate_composition(fractions,species,stride);
}

/** Check an actual physical/EOS-input state with contiguous mass fractions.
 * Workflow: validate limits, conservative thermal recovery and composition;
 * then require finite positive T(rho,e,X), P(U,X), and c(U,P,X) from the
 * selected EOS. The original 8*epsilon thermal-resolution rule is retained.
 * EOS exceptions propagate to the transaction owner; no fallback, repair or
 * publication occurs here. A native mixed-measure mean must first be mapped
 * to its effective EOS state by its geometry-specific closure.
 */
template<class Eos>
ARCH_INLINE Status validate_eos(const FluidVector& fluid,
    const double* contiguous_fractions,int species,const Bounds& bounds,const Eos& eos)
{
    if (!valid_bounds(bounds)) return Status::invalid_thermodynamics;
    if (species < 0 || (species > 0 && !contiguous_fractions))
        return Status::invalid_composition;
    const auto status = validate(fluid,contiguous_fractions,species,1,
        bounds.density,bounds.internal_min,bounds.internal_max);
    if (status != Status::valid) return status;
    const auto thermal = recover(fluid);
    const double temperature = eos.get_temperature(fluid.rho,thermal.internal,
        contiguous_fractions);
    const double pressure = eos.get_pressure(fluid,contiguous_fractions);
    const double sound = eos.get_sound_speed(fluid,pressure,contiguous_fractions);
    if (!std::isfinite(temperature) || !(temperature > 0.0)
        || !std::isfinite(pressure) || !(pressure > 0.0)
        || !std::isfinite(sound) || !(sound > 0.0))
        return Status::invalid_thermodynamics;
    return Status::valid;
}

ARCH_INLINE bool accepted(Status status)
{ return status == Status::valid || status == Status::repaired; }

/**
 * Accept only bounded negative traces with an explicit species-mass receipt.
 *
 * Delta M_s = -rho * X_s * V for X_s < 0. No positive fraction, density or
 * energy is changed and no abundance floor is introduced. This is a measured
 * correction policy, not a claim that every small negative is caused by the
 * final floating-point subtraction. Larger negatives remain numerical errors.
 * Preflight the entire cell and every receipt before the first state write.
 */
ARCH_INLINE Status repair_composition_roundoff(
    const FluidVector& fluid, double* fractions, int count, int stride,
    double volume, RepairView receipt, int cell)
{
    if (count < 0 || stride <= 0 || (count > 0 && !fractions))
        return Status::invalid_composition;
    bool needs_repair = false;
    double nonnegative_sum = 0.0;
    for (int s = 0; s < count; ++s) {
        const double x = fractions[s * stride];
        if (!std::isfinite(x) || x < -composition_roundoff_limit)
            return Status::invalid_composition;
        needs_repair = needs_repair || x < 0.0;
        nonnegative_sum += std::max(x, 0.0);
    }
    if (!needs_repair) return Status::valid;
    const auto kinematics = recover(fluid);
    if (kinematics.status != Status::valid) return kinematics.status;
    if (!receipt.values || receipt.species != count
        || !(volume > 0.0) || !std::isfinite(volume)
        || std::abs(nonnegative_sum - 1.0)
            > 512.0 * count * std::numeric_limits<double>::epsilon())
        return Status::invalid_composition;
    for (int s = 0; s < count; ++s) {
        const double x = fractions[s * stride];
        if (x >= 0.0) continue;
        // Multiply the tiny fraction first; rho*V may otherwise overflow even
        // when the correction is representable. An underflowed receipt cannot
        // account for a state change, so reject it rather than silently clip.
        const double mass = (fluid.rho * -x) * volume;
        if (!(mass > 0.0) || !std::isfinite(mass))
            return Status::invalid_composition;
    }
    receipt.event(volume, cell);
    for (int s = 0; s < count; ++s) {
        const double x = fractions[s * stride];
        if (x >= 0.0) continue;
        receipt.species_mass(s, (fluid.rho * -x) * volume);
        fractions[s * stride] = 0.0;
    }
    return Status::repaired;
}

/** Validate physical bounds before any trace repair, then validate composition. */
ARCH_INLINE Status accept_conservative_state(
    const FluidVector& fluid, double* fractions, int count, int stride,
    double density_floor, double energy_floor, double energy_ceiling,
    double volume, RepairView receipt, int cell)
{
    const auto physical = validate(fluid, nullptr, 0, 1,
        density_floor, energy_floor, energy_ceiling);
    if (physical != Status::valid) return physical;
    const auto repair = repair_composition_roundoff(
        fluid, fractions, count, stride, volume, receipt, cell);
    if (!accepted(repair)) return repair;
    const auto final = validate(fluid, fractions, count, stride,
        density_floor, energy_floor, energy_ceiling);
    return final == Status::valid ? repair : final;
}

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
        if (!std::isfinite(x) || x < -composition_roundoff_limit) return false;
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
