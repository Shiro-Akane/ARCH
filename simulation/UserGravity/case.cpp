/**
 * @file case.cpp
 * @brief Function-registered uniform gas with user physical and gravity boundaries.
 *
 * Workflow:
 * 1. UserGravitySetup validates the geometry, registers one explicit
 *    neutral species and checks the manufactured uniform state.
 * 2. UserGravityInit fills rho=1, p=1, zero velocity and X=1 for every cell.
 * 3. Sibling physical_boundary.cpp and gravity_boundary.cpp publish free
 *    functions, so this example demonstrates the function callback form that
 *    the class-form UserBoundary example complements.
 *
 * The free initializer receives only a point, so the geometry validation lives
 * in Setup and the state itself is geometry-independent.
 */

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include <UserInterface.h>
#include <GlobalDefs.h>

namespace
{
/** Uniform interior state of this example, in CGS units. */
constexpr double kDensity = 1.0;        // g/cm^3
constexpr double kPressure = 1.0;       // erg/cm^3
constexpr double kSpecificHeatCv = 1.0; // erg/(g K); finite positive by construction

/** Registers the case species and validates the configured geometry. */
void UserGravitySetup(SimConfig &config, SpeciesManager &specs)
{
    const std::string &geometry = config.grid.geometry;
    if (geometry != "cartesian" && geometry != "cylindrical" && geometry != "spherical")
        throw std::invalid_argument(
            "UserGravity supports cartesian, cylindrical and spherical geometry.");

    // Setup validates the same optional CGS heat control the boundary reads.
    // Inspection records this access; missing input intentionally means zero flux.
    const double heat_flux = config.Get<double>("user_boundary_heat_flux", 0.0);
    if (!std::isfinite(heat_flux))
        throw std::invalid_argument("user_boundary_heat_flux must be finite in erg/(cm^2 s).");

    const double gamma = config.physics.gamma;
    if (!std::isfinite(gamma) || !(gamma > 1.0))
        throw std::invalid_argument(
            "UserGravity requires a finite ideal-gas gamma greater than one.");

    // One explicit neutral species: A=1, Z=0, gamma from the active EOS
    // configuration and a finite positive Cv.
    specs.add_species("UserGravityGas", 1.0, 0.0, gamma, kSpecificHeatCv);

    // Explicit consistency of the manufactured state with the ideal-gas
    // relation p = (gamma - 1) * rho * Cv * T.
    const double temperature = kPressure / (kDensity * kSpecificHeatCv * (gamma - 1.0));
    if (!std::isfinite(temperature) || !(temperature > 0.0))
        throw std::invalid_argument(
            "UserGravity requires a finite positive interior temperature.");

    std::cout << "[Problem] UserGravity uniform gas: rho=" << kDensity
              << ", p=" << kPressure << ", T=" << temperature << " K, geometry="
              << geometry << "\n";
}

/** Fills the uniform interior primitive at one point. */
void UserGravityInit(const PointCoords &point, PrimitiveData &out)
{
    // PointCoords carries every supported geometry in one view: r is the
    // native/converted radius fields. Signed radial ghost charts can occur
    // during prefill; Core validates the physical domain and owns the join.
    if (!std::isfinite(point.x) || !std::isfinite(point.r) || !std::isfinite(point.r_cy))
        throw std::invalid_argument(
            "UserGravity received a non-finite coordinate.");

    out.rho = kDensity;
    out.p = kPressure;
    out.u = 0.0;
    out.v = 0.0;
    out.w = 0.0;
    out.SetMassFraction(0, 1.0); // the single neutral species registered in Setup
}
} // namespace

REGISTER_PROBLEM("UserGravity", UserGravitySetup, UserGravityInit)
