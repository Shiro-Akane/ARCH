/**
 * @file BoundaryTypes.h
 * @brief Typed, read-only case boundary requests and returned physical data.
 *
 * Workflow:
 * 1. Core constructs face coordinates, stage time and a copied interior state.
 *    The explicit native RZ chart maps (r,z) onto the meridional phi=0 plane;
 *    coordinate selection alone does not change callback state conversion.
 *    Cylindrical2D is canonical RZ here, so its retired polar normal is gone
 *    and an explicit Existing chart for that grid is refused.
 * 2. A case callback returns primitive or scalar boundary data in CGS units.
 * 3. Core validates the result and applies it through the selected EOS/operator.
 *
 * Callbacks never receive an AMR block, writable state or device pointer.
 * Hydro and diffusion data belong to physical_boundary.cpp; potential data
 * belong to gravity_boundary.cpp. Native velocities use the Grid basis.
 */
#pragma once

#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "amr/exchange/BoundaryPlan.h"
#include "data/UserTypes.h"
#include "grid/Grid.h"
#include "grid/GridMetrics.h"
#include "physics/boundary/BoundaryFlux.h"
#include "physics/species/Species.h"

namespace arch::boundary {
/** A physical face/ghost request, independent of backend storage and AMR ids. */
struct BoundaryCoordinates {
    BoundaryAxis axis = BoundaryAxis::X1;
    BoundarySide side = BoundarySide::Lower;
    BoundaryPurpose purpose = BoundaryPurpose::Hydro;
    int dimension = 1;
    int ghost_depth = 0;
    double time = 0.;
    double physical_distance = 0.;
    std::array<double, 3> native_position{};
    std::array<double, 3> cartesian_normal{};
    PointCoords point{};
    PointCoords ghost_point{};
};

/** Outward Cartesian unit normal from the existing native orthonormal basis.
 * Cartesian e_i; cylindrical e_R/e_z/e_phi; spherical e_r/e_theta/e_phi.
 * Grid remains the coordinate authority, including the current 2D conventions.
 * An explicitly selected native RZ chart instead uses e_r=e_x and e_z at
 * phi=0. This is a coordinate helper, not an EOS or native-moment conversion.
 *
 * Guard: retired computational cylindrical2D polar (r,phi) has no Existing
 * chart, so an explicit Existing selection for a 2-dimensional cylindrical
 * grid is refused here. Callers that only need the canonical chart use the
 * no-chart overload, which resolves RZ from the actual grid instead.
 */
inline std::array<double, 3> BoundaryCartesianNormal(const Grid& grid, const PointCoords& face,
    BoundaryAxis axis, BoundarySide side, GridMetrics::GeometrySemantics semantics)
{
    if (semantics == GridMetrics::GeometrySemantics::Existing
        && grid.geometry == "cylindrical" && grid.dim == 2)
        throw std::invalid_argument(
            "physical boundary normal for cylindrical2D requires the canonical RZ chart");
    const auto view = GridMetrics::make_geometry_view(grid, semantics);
    const GridMetrics::Geometry geometry = view.geometry;
    if (geometry == GridMetrics::Geometry::Unsupported)
        throw std::invalid_argument("Unsupported boundary coordinate geometry");
    const int direction = static_cast<int>(axis);
    if (direction < 0 || direction >= grid.dim)
        throw std::invalid_argument("physical boundary axis must be an active grid direction");
    if (side != BoundarySide::Lower && side != BoundarySide::Upper)
        throw std::invalid_argument("physical boundary side must be Lower or Upper");
    const double sign = (side == BoundarySide::Lower) ? -1.0 : 1.0;
    std::array<double, 3> normal{0.0, 0.0, 0.0};
    if (view.semantics == GridMetrics::GeometrySemantics::AxisymmetricRz) {
        // Native (r,z) is a meridional length chart: n_r=(1,0,0),
        // n_z=(0,0,1). Its second coordinate is never an azimuthal angle.
        normal[direction == 0 ? 0 : 2] = 1.0;
    } else switch (geometry) {
    case GridMetrics::Geometry::Cartesian:
        normal[direction] = 1.0;
        break;
    case GridMetrics::Geometry::Cylindrical: {
        const double phi = face.phi_cy;
        const double cosine = std::cos(phi);
        const double sine = std::sin(phi);
        if (direction == 0) normal = {cosine, sine, 0.0};
        else if (direction == 1) normal = {0.0, 0.0, 1.0};
        else normal = {-sine, cosine, 0.0};
        // The retired cylindrical2D polar azimuthal normal (dim == 2) is gone;
        // a cylindrical grid reaching this branch is 1D e_R or 3D e_R/e_z/e_phi.
        break;
    }
    case GridMetrics::Geometry::Spherical: {
        const double theta = face.theta;
        const double phi = face.phi;
        const double sin_theta = std::sin(theta);
        const double cos_theta = std::cos(theta);
        const double cosine = std::cos(phi);
        const double sine = std::sin(phi);
        if (direction == 0)
            normal = {sin_theta * cosine, sin_theta * sine, cos_theta};
        else if (grid.dim == 2)
            normal = {-sine, cosine, 0.0};
        else if (direction == 1)
            normal = {cos_theta * cosine, cos_theta * sine, -sin_theta};
        else
            normal = {-sine, cosine, 0.0};
        break;
    }
    case GridMetrics::Geometry::Unsupported:
        break;
    }
    for (double& component : normal) component *= sign;
    const double magnitude = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1]
                                     + normal[2] * normal[2]);
    if (!std::isfinite(magnitude) || std::abs(magnitude - 1.0) > 64.0 * std::numeric_limits<double>::epsilon())
        throw std::invalid_argument("physical boundary normal is not a finite unit direction");
    return normal;
}

/** No-chart normal: resolve the canonical chart from the actual grid.
 * Cylindrical2D commits to axisymmetric RZ; every other supported geometry
 * keeps its Existing angular/native chart. Use the explicit-semantics overload
 * when a specific chart must be asserted rather than inferred.
 */
inline std::array<double, 3> BoundaryCartesianNormal(const Grid& grid, const PointCoords& face,
    BoundaryAxis axis, BoundarySide side)
{
    const bool rz = grid.geometry == "cylindrical" && grid.dim == 2;
    return BoundaryCartesianNormal(grid, face, axis, side,
        rz ? GridMetrics::GeometrySemantics::AxisymmetricRz
           : GridMetrics::GeometrySemantics::Existing);
}

/** Snapshot passed only to the physical boundary callback. */
struct PhysicalBoundaryContext : BoundaryCoordinates {
    const SimConfig& config;
    const SpeciesManager& species;
    PrimitiveData interior;
    PhysicalBoundaryContext(BoundaryCoordinates coordinates, const SimConfig& c,
                            const SpeciesManager& s, PrimitiveData value)
        : BoundaryCoordinates(coordinates), config(c), species(s),
          interior(std::move(value)) {}
};

/** Separate primitive and diffusion controls; missing controls keep the base BC. */
struct PhysicalBoundaryData {
    std::optional<PrimitiveData> hydro;
    ScalarBoundaryCondition temperature;
    std::array<ScalarBoundaryCondition, 3> velocity{};
    std::vector<ScalarBoundaryCondition> species;
};

/** Snapshot passed only to a potential boundary callback. */
struct GravityBoundaryContext : BoundaryCoordinates {
    const SimConfig& config;
    const SpeciesManager& species;
    GravityBoundaryContext(BoundaryCoordinates coordinates, const SimConfig& c,
                           const SpeciesManager& s)
        : BoundaryCoordinates(coordinates), config(c), species(s) {}
};

enum class GravityBoundaryCondition { Dirichlet, Neumann, Robin, Periodic };

/** Potential law a*Phi + b*dPhi/dn = c; n is the outward physical normal. */
struct GravityBoundaryData {
    GravityBoundaryCondition kind = GravityBoundaryCondition::Dirichlet;
    double a = 1., b = 0., c = 0.;
    /** Prescribe potential in cm^2/s^2 at the physical face. */
    static GravityBoundaryData Dirichlet(double value) {
        return {GravityBoundaryCondition::Dirichlet, 1., 0., value};
    }
    /** Prescribe outward dPhi/dn in cm/s^2 at the physical face. */
    static GravityBoundaryData Neumann(double value) {
        return {GravityBoundaryCondition::Neumann, 0., 1., value};
    }
    /** Prescribe a finite, coercive linear mixed potential condition. */
    static GravityBoundaryData Robin(double a, double b, double c) {
        return {GravityBoundaryCondition::Robin, a, b, c};
    }
    /** Pair this face with the opposite periodic face; no scalar data apply. */
    static GravityBoundaryData Periodic() {
        return {GravityBoundaryCondition::Periodic, 0., 0., 0.};
    }
};
} // namespace arch::boundary
