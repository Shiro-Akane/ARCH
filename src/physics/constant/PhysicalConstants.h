/**
 * @file PhysicalConstants.h
 * @brief One CPU/CUDA authority, organized by discipline with explicit units.
 *
 * SI defining constants and CODATA 2022 measured central values, converted to
 * cgs. Precision means the published value, not the number of printed digits.
 * See README.md for sources, uncertainties and external-data boundaries.
 */
#pragma once

#include <numbers>

namespace arch::constants {

namespace math {
inline constexpr double pi = std::numbers::pi_v<double>;
inline constexpr double two_pi = 2.0 * pi;
}

namespace units {
inline constexpr double erg_per_ev = 1.602176634e-12; // erg/eV; SI definition
inline constexpr double erg_per_mev = 1.0e6 * erg_per_ev;
}

namespace relativity::cgs {
inline constexpr double speed_of_light = 2.99792458e10; // cm/s; exact
}

namespace gravity::cgs {
inline constexpr double gravitational_constant = 6.67430e-8; // cm^3/(g s^2)
}

namespace statistical {
inline constexpr double avogadro = 6.02214076e23; // 1/mol; exact
namespace cgs {
inline constexpr double boltzmann = 1.380649e-16; // erg/K; exact
}
}

namespace quantum::cgs {
inline constexpr double planck = 6.62607015e-27; // erg s; exact
inline constexpr double reduced_planck = planck / math::two_pi;
}

namespace atomic::cgs {
inline constexpr double atomic_mass_unit = 1.66053906892e-24; // g
inline constexpr double proton_mass = 1.67262192595e-24; // g
}

namespace electromagnetic {
inline constexpr double fine_structure = 7.2973525643e-3; // dimensionless
namespace cgs {
// Gaussian charge: e^2 = alpha hbar c, in erg cm (statC^2). Do not confuse
// this with the SI elementary charge in coulombs. Coulomb terms need e^2,
// so no separately rounded square root or host-only sqrt is necessary.
inline constexpr double elementary_charge_squared = fine_structure
    * quantum::cgs::reduced_planck * relativity::cgs::speed_of_light;
}
}

namespace radiation::cgs {
// sigma = 2 pi^5 k_B^4 / (15 h^3 c^2); derived, not another numerical authority.
inline constexpr double stefan_boltzmann =
    (2.0 / 15.0) * math::pi * math::pi * math::pi * math::pi * math::pi
    * statistical::cgs::boltzmann * statistical::cgs::boltzmann
    * statistical::cgs::boltzmann * statistical::cgs::boltzmann
    / (quantum::cgs::planck * quantum::cgs::planck * quantum::cgs::planck
       * relativity::cgs::speed_of_light * relativity::cgs::speed_of_light);
// sigma: erg/(cm^2 s K^4); a: erg/(cm^3 K^4).
inline constexpr double energy_density =
    4.0 * stefan_boltzmann / relativity::cgs::speed_of_light;
}

} // namespace arch::constants
