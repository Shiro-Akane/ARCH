/**
 * @file NuclearEnergy.h
 * @brief Shared network-mass contraction for composition increments.
 *
 * Network energy weights and their conversion retain the reaction package's
 * mass convention. Escaping-neutrino and other nonconservative sources have
 * separate time-integrated contributions; isotope changes cannot supply them.
 */
#pragma once

#include "../../core/ArchPortability.h"
#include "../../core/CompensatedSum.h"

namespace arch::network_energy {

template <class Network, class Increment>
ARCH_INLINE double composition_increment_energy(const Increment& increment)
{
    arch::math::CompensatedSum nuclear_mass_delta;
    for (int i = 0; i < Network::NUM_SPECIES; ++i) {
        const double molar_delta = increment[i] / Network::aion(i);
        nuclear_mass_delta.add_product(molar_delta, Network::energy_weight(i));
    }
    return Network::ENERGY_CONVERSION * nuclear_mass_delta.value();
}

struct StateDifference
{
    const double* after;
    const double* before;
    ARCH_INLINE double operator[](int i) const { return after[i] - before[i]; }
};

template <class Network>
ARCH_INLINE double integrated_composition_energy(const double* after,
                                                 const double* before)
{
    return composition_increment_energy<Network>(StateDifference{after, before});
}

} // namespace arch::network_energy
