/**
 * @file case.cpp
 * @brief Class-registered uniform gas with user physical and gravity boundaries.
 *
 * Workflow:
 * 1. Setup() validates the geometry, registers one explicit neutral species
 *    and checks the manufactured uniform state for a finite positive temperature.
 * 2. Init() fills rho=1, p=1, zero velocity and X=1 for every cell.
 * 3. Sibling physical_boundary.cpp and gravity_boundary.cpp publish class-form
 *    callbacks for the same registered problem name.
 *
 * The interior state is deliberately uniform so the example can be read in one
 * pass: it demonstrates registration, native coordinates and the two user
 * boundary channels, not gravity or evolution physics.
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

/** Native first-axis coordinate in the geometry's own basis. */
double NativeX1(const PointCoords &point, const std::string &geometry)
{
    if (geometry == "cylindrical")
        return point.r_cy;
    if (geometry == "spherical")
        return point.r;
    return point.x;
}
} // namespace

/** Class-form case: REGISTER_PROBLEM_CLASS publishes Setup() and Init(). */
class UserBoundaryExample
{
public:
    void Setup(SimConfig &config, SpeciesManager &specs)
    {
        geometry_ = config.grid.geometry;
        if (geometry_ != "cartesian" && geometry_ != "cylindrical" && geometry_ != "spherical")
            throw std::invalid_argument(
                "UserBoundary supports cartesian, cylindrical and spherical geometry.");

        // Setup validates the same optional CGS heat control the boundary reads.
        // Inspection records this access; missing input intentionally means zero flux.
        const double heat_flux = config.Get<double>("user_boundary_heat_flux", 0.0);
        if (!std::isfinite(heat_flux))
            throw std::invalid_argument("user_boundary_heat_flux must be finite in erg/(cm^2 s).");

        const double gamma = config.physics.gamma;
        if (!std::isfinite(gamma) || !(gamma > 1.0))
            throw std::invalid_argument(
                "UserBoundary requires a finite ideal-gas gamma greater than one.");

        // One explicit neutral species: A=1, Z=0, gamma from the active EOS
        // configuration and a finite positive Cv, following the explicit
        // species registration used by the other nonreacting initializers.
        gas_id_ = specs.add_species("UserBoundaryGas", 1.0, 0.0, gamma, kSpecificHeatCv);

        // Explicit consistency of the manufactured state with the ideal-gas
        // relation p = (gamma - 1) * rho * Cv * T.
        const double temperature = kPressure / (kDensity * kSpecificHeatCv * (gamma - 1.0));
        if (!std::isfinite(temperature) || !(temperature > 0.0))
            throw std::invalid_argument(
                "UserBoundary requires a finite positive interior temperature.");

        std::cout << "[Problem] UserBoundary uniform gas: rho=" << kDensity
                  << ", p=" << kPressure << ", T=" << temperature << " K, geometry="
                  << geometry_ << "\n";
    }

    void Init(const PointCoords &point, PrimitiveData &out) const
    {
        const double native_x1 = NativeX1(point, geometry_);
        // Core validates the physical domain. Ghost prefill can use a signed
        // radial chart across r=0 before the regularity join replaces it.
        if (!std::isfinite(native_x1))
            throw std::invalid_argument(
                "UserBoundary received a non-finite native coordinate.");

        out.rho = kDensity;
        out.p = kPressure;
        out.u = 0.0;
        out.v = 0.0;
        out.w = 0.0;
        out.SetMassFraction(gas_id_, 1.0);
    }

private:
    int gas_id_ = 0; ///< Index of the single species registered in Setup.
    std::string geometry_ = "cartesian";
};

REGISTER_PROBLEM_CLASS("UserBoundary", UserBoundaryExample)
