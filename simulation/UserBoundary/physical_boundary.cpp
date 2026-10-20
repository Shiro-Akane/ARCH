/**
 * @file physical_boundary.cpp
 * @brief Class-form user physical boundary for the UserBoundary example.
 *
 * Workflow:
 * 1. Core calls the callback with the face coordinates, the read-only config,
 *    the read-only species table and its purpose.
 * 2. A Hydro request answers with the complete uniform interior primitive.
 * 3. A Diffusion request answers through the temperature channel only, either
 *    as a desired Value or as an outward heat flux; the other channels stay
 *    unset so hydro and diffusion never share a request.
 *
 * The class is default-constructible and its call operator is const, so the
 * callback holds no mutable case state. CMake compiles this file with
 * ARCH_BOUNDARY_SOURCE_SHA256, which the registration macro publishes.
 */

#include <cmath>
#include <stdexcept>

#include <UserInterface.h>
#include <GlobalDefs.h>

namespace
{
/** Uniform interior state of the UserBoundary example, in CGS units. */
constexpr double kDensity = 1.0;  // g/cm^3
constexpr double kPressure = 1.0; // erg/cm^3

/** Optional input key: outward heat flux in erg/(cm^2 s); 0 keeps a Value. */
constexpr const char *kHeatFluxKey = "user_boundary_heat_flux";

/** Interior temperature of the uniform state from the read-only EOS inputs. */
double InteriorTemperature(const arch::boundary::PhysicalBoundaryContext &ctx)
{
    const double gamma_minus_one = ctx.config.physics.gamma - 1.0;
    if (!std::isfinite(gamma_minus_one) || !(gamma_minus_one > 0.0))
        throw std::invalid_argument(
            "UserBoundary physical callback requires an ideal-gas gamma greater than one.");

    const double cv = ctx.species.get_Cv_ref(0);
    if (!std::isfinite(cv) || !(cv > 0.0))
        throw std::invalid_argument(
            "UserBoundary physical callback requires a finite positive species Cv.");

    const double temperature = kPressure / (kDensity * cv * gamma_minus_one);
    if (!std::isfinite(temperature) || !(temperature > 0.0))
        throw std::invalid_argument(
            "UserBoundary physical callback requires a finite positive temperature.");
    return temperature;
}
} // namespace

/** Class-form physical callback registered with the "UserBoundary" name. */
class UserBoundaryPhysical
{
public:
    arch::boundary::PhysicalBoundaryData
    operator()(const arch::boundary::PhysicalBoundaryContext &ctx) const
    {
        if (ctx.species.count() < 1)
            throw std::invalid_argument(
                "UserBoundary physical callback requires one registered species.");

        arch::boundary::PhysicalBoundaryData data;

        // The request purpose selects one channel; every other channel keeps
        // the base condition instead of being silently overwritten.
        if (ctx.purpose == arch::boundary::BoundaryPurpose::Diffusion)
        {
            const double amplitude = ctx.config.Get<double>(kHeatFluxKey, 0.0);
            if (!std::isfinite(amplitude))
                throw std::invalid_argument(
                    "user_boundary_heat_flux must be a finite value in erg/(cm^2 s).");

            data.temperature = amplitude == 0.0
                ? arch::boundary::ScalarBoundaryCondition{arch::boundary::ScalarBoundaryKind::Value,
                                                          InteriorTemperature(ctx)}
                : arch::boundary::ScalarBoundaryCondition{arch::boundary::ScalarBoundaryKind::OutwardFlux,
                                                          amplitude};
            return data;
        }

        if (ctx.purpose != arch::boundary::BoundaryPurpose::Hydro)
            return data;

        // A hydro face receives the complete interior primitive, not a partial
        // edit: density, all three native velocities, pressure and composition.
        PrimitiveData hydro;
        hydro.rho = kDensity;
        hydro.p = kPressure;
        hydro.u = 0.0;
        hydro.v = 0.0;
        hydro.w = 0.0;
        hydro.SetMassFraction(0, 1.0);
        data.hydro = hydro;
        return data;
    }
};

REGISTER_PHYSICAL_BOUNDARY_CLASS("UserBoundary", UserBoundaryPhysical)
