/**
 * @file gravity_boundary.cpp
 * @brief Class-form user potential boundary for the UserBoundary example.
 *
 * Workflow:
 * 1. Core calls the callback for the physical potential faces with the face
 *    coordinates and the read-only configuration.
 * 2. The callback returns per-side Dirichlet values of the analytic
 *    uniform-density field of this example.
 * 3. Natural regularity faces (for example the r=0 join) are skipped by Core;
 *    the same formula is still consistent there because dPhi/dr = 0 at r = 0.
 *
 * The field is an initial condition for a smoke run. Nothing here claims a
 * manufactured self-consistent solution or a converged gravity result.
 */

#include <cmath>
#include <stdexcept>
#include <string>

#include <UserInterface.h>
#include <GlobalDefs.h>

namespace
{
/** Uniform density of this example in g/cm^3; matches case.cpp. */
constexpr double kDensity = 1.0;

/** CGS gravitational constant from the public PhysicalConstants header. */
constexpr double kGravity = arch::constants::gravity::cgs::gravitational_constant; // cm^3/(g s^2)

/** Reduced radial dimension of the geometry: slab 1, cylinder 2, sphere 3. */
int GeometryDimension(const std::string &geometry)
{
    if (geometry == "cartesian")
        return 1;
    if (geometry == "cylindrical")
        return 2;
    if (geometry == "spherical")
        return 3;
    throw std::invalid_argument(
        "UserBoundary gravity callback supports cartesian, cylindrical and spherical geometry.");
}
} // namespace

/** Class-form potential callback registered with the "UserBoundary" name. */
class UserBoundaryGravity
{
public:
    arch::boundary::GravityBoundaryData
    operator()(const arch::boundary::GravityBoundaryContext &ctx) const
    {
        // Potential periodicity must agree with the same AMR chart direction.
        const std::string faces[]{ctx.config.grid.x1l_boundary_type, ctx.config.grid.x1r_boundary_type,
            ctx.config.grid.x2l_boundary_type, ctx.config.grid.x2r_boundary_type,
            ctx.config.grid.x3l_boundary_type, ctx.config.grid.x3r_boundary_type};
        if (faces[2 * static_cast<int>(ctx.axis) + static_cast<int>(ctx.side)] == "periodic")
            return arch::boundary::GravityBoundaryData::Periodic();
        // Both this callback and the solver use the immutable CGS constant.
        // The common input validator rejects the retired gravity_G key.

        const int dimension = ctx.config.grid.geometry == "spherical" && ctx.dimension == 2
            ? 2 : GeometryDimension(ctx.config.grid.geometry);
        const double radius = ctx.native_position[0]; // native radius/x of the face
        if (!std::isfinite(radius) || (dimension > 1 && radius < 0.0))
            throw std::invalid_argument(
                "UserBoundary gravity callback requires a finite native radius/x position.");

        // Uniform density solves the reduced radial Poisson equation
        // lap(Phi) = 4 pi G rho with Phi(0) = 0, giving
        // Phi(r) = 2 pi G rho r^2 / dimension for slab, cylinder and sphere.
        const double potential =
            2.0 * arch::constants::math::pi * kGravity * kDensity * radius * radius /
            static_cast<double>(dimension);
        return arch::boundary::GravityBoundaryData::Dirichlet(potential);
    }
};

REGISTER_GRAVITY_BOUNDARY_CLASS("UserBoundary", UserBoundaryGravity)
