/**
 * @file test_physical_constants.cpp
 * @brief Check the shared physical constants at compile time.
 *
 * Assertions compare published definitions, units and derived identities;
 * the empty main exists only to provide an executable test target.
 */
#include "physics/constant/PhysicalConstants.h"
#include <limits>
#include <numbers>

using namespace arch::constants;
// Published SI definitions / CODATA 2022 central values, in the stated units.
static_assert(math::pi == std::numbers::pi_v<double>);
static_assert(math::two_pi == 6.283185307179586476925286766559);
static_assert(relativity::cgs::speed_of_light == 29979245800.0);
static_assert(gravity::cgs::gravitational_constant == 6.67430e-8);
static_assert(statistical::cgs::boltzmann == 1.380649e-16);
static_assert(statistical::avogadro == 6.02214076e23);
static_assert(quantum::cgs::planck == 6.62607015e-27);
static_assert(atomic::cgs::atomic_mass_unit == 1.66053906892e-24);
static_assert(atomic::cgs::proton_mass == 1.67262192595e-24);
static_assert(electromagnetic::fine_structure == 7.2973525643e-3);
static_assert(units::erg_per_ev == 1.602176634e-12);

constexpr bool within_roundoff(double value, double reference)
{
    const double error = value > reference ? value - reference : reference - value;
    return error <= 8.0 * std::numeric_limits<double>::epsilon() * reference;
}

// Independent 70-digit evaluation from the decimal SI definitions and pi;
// these references deliberately do not call the production expressions.
static_assert(within_roundoff(units::erg_per_mev, 1.602176634e-6));
static_assert(within_roundoff(quantum::cgs::reduced_planck,
                             1.0545718176461563913e-27));
static_assert(within_roundoff(radiation::cgs::stefan_boltzmann,
                             5.670374419184429454e-5));
static_assert(within_roundoff(radiation::cgs::energy_density,
                             7.565733250280004647e-15));
static_assert(within_roundoff(electromagnetic::cgs::elementary_charge_squared,
                             2.307077550767917596e-19));
int main() {}
