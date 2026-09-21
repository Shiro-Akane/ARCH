#pragma once

#include <cmath>
#include <stdexcept>

#include "data/FluidState.h"
#include "data/UserTypes.h"

namespace ProblemHelper::detail {

// Shared by mesh population and application preview. Keep the exact energy
// construction here so temperature-based cases have one initialization path.
template <class Eos>
FluidVector InitialConservedState(const PrimitiveData &data, const Eos &eos)
{
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
    return state;
}

} // namespace ProblemHelper::detail
