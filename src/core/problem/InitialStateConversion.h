/**
 * @file InitialStateConversion.h
 * @brief Convert case primitive fields to solver conserved state using the shared EOS.
 *
 * Workflow:
 * 1. Read validated configuration or a registered problem request.
 * 2. Convert case primitive fields to solver conserved state using the shared EOS.
 * 3. Return a single resolved value or state with explicit failure on invalid input.
 */

#pragma once

#include <cmath>
#include <stdexcept>

#include "data/FluidState.h"
#include "data/GlobalDefs.h"
#include "data/UserTypes.h"
#include "numerics/state/StateAdmissibility.h"

namespace ProblemHelper::detail {

// Shared by mesh population and application preview. Keep the exact energy
// construction here so temperature-based cases have one initialization path.
template <class Eos>
FluidVector InitialConservedState(const PrimitiveData &data, const Eos &eos,
                                 const NumericsConfig& limits = {}, arch::state::Repair* report = nullptr)
{
    if (!(data.rho > 0.0) || !std::isfinite(data.rho))
        throw std::runtime_error("Initial density must be finite and positive; exact vacuum is unsupported");
    double sum = 0.0;
    for (double x : data.mass_fractions) {
        if (!(x >= 0.0) || !std::isfinite(x)) throw std::runtime_error("Invalid initial composition");
        sum += x;
    }
    if (!data.mass_fractions.empty() && std::abs(sum - 1.0) > 512.0 * data.mass_fractions.size() * std::numeric_limits<double>::epsilon())
        throw std::runtime_error("Initial composition must sum to one");
    FluidVector state;
    state.rho = data.rho;
    state.mom_u = data.rho * data.u;
    state.mom_v = data.rho * data.v;
    state.mom_w = data.rho * data.w;
    if (data.has_temperature) {
        const double specific_internal_energy = eos.get_eint_from_T(
            data.rho, data.temperature, data.mass_fractions.data());
        if (!std::isfinite(specific_internal_energy)) {
            throw std::runtime_error("EOS returned non-finite internal energy for temperature-based initialization.");
        }
        const double kinetic_energy = 0.5 * data.rho *
            (data.u * data.u + data.v * data.v + data.w * data.w);
        state.eng = data.rho * specific_internal_energy + kinetic_energy;
    } else {
        state.eng = eos.get_total_energy_primitive(
            data.rho, data.u, data.v, data.w, data.p,
            data.mass_fractions.data());
    }
    const auto recovery = arch::state::apply_bounds(state,limits.sml_rho,limits.min_eint,limits.max_eint);
    if (!arch::state::accepted(recovery.status))
        throw std::runtime_error("Initial state has invalid or unresolved thermal energy");
    if (recovery.status == arch::state::Status::repaired) {
        // A positive repaired state can still lie outside a tabulated EOS domain.
        // Validate it before either preview or mesh initialization publishes it.
        const auto thermal = arch::state::recover(state);
        const auto* fractions = data.mass_fractions.data();
        const double temperature = eos.get_temperature(state.rho, thermal.internal, fractions);
        const double pressure = eos.get_pressure(state, fractions);
        const double sound_speed = eos.get_sound_speed(state, pressure, fractions);
        if (!(temperature > 0.0) || !std::isfinite(temperature) ||
            !(pressure > 0.0) || !std::isfinite(pressure) ||
            !(sound_speed > 0.0) || !std::isfinite(sound_speed))
            throw std::runtime_error("Initial state repair lies outside the EOS valid domain");
    }
    if (report) *report = recovery;
    return state;
}

} // namespace ProblemHelper::detail
