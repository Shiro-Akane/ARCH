/** @file GravityUserBoundary.cpp
 * @brief Evaluate per-side and per-face user gravity boundary data.
 *
 * Workflow:
 * 1. Convert a native composite face through its actual chart into the shared
 *    read-only callback input; RZ axial length is never an azimuthal angle.
 * 2. Validate the coercive per-side coefficients once per stage.
 * 3. Return the datum c at the actual physical face center.
 */

#include <cmath>
#include <stdexcept>

#include "grid/CoordinateBoundary.h"
#include "grid/Grid.h"
#include "physics/gravity/self/GravityUserBoundary.h"

namespace Physical::Gravity {
namespace {
/** Native Grid geometry name for the shared coordinate expansion. */
const char* native_geometry(arch::elliptic::Geometry geometry) {
    if (geometry == arch::elliptic::Geometry::Cylindrical) return "cylindrical";
    if (geometry == arch::elliptic::Geometry::Spherical) return "spherical";
    return "cartesian";
}

/** Lightweight coordinate authority for the shared boundary normal helper.
 *  Only the active dimension and geometry name are read from it. */
Grid coordinate_authority(const arch::elliptic::EllipticMesh& mesh) {
    Grid grid;
    grid.dim = mesh.dimension;
    grid.geometry = native_geometry(mesh.geometry);
    return grid;
}

/** Expand a face with the same geometry/chart authority as physical boundaries.
 * RZ at phi=0 has (x,y,z)=(r,0,z); spherical polar expansion keeps the
 * existing coordinate rules. No second coordinate conversion is introduced.
 */
PointCoords native_point(const arch::elliptic::EllipticMesh& mesh,
    const std::array<double, 3>& native) {
    return Grid::PhysicalCoordsFromNative(mesh.dimension, native_geometry(mesh.geometry),
        native[0], native[1], native[2], mesh.semantics);
}

/** Outward unit normal of a physical coordinate face. Grid owns the axis and
 *  geometry conventions, so no finite difference or step size is involved. */
std::array<double, 3> outward_normal(const arch::elliptic::EllipticMesh& mesh, int axis, int side,
    const std::array<double, 3>& native) {
    return arch::boundary::BoundaryCartesianNormal(coordinate_authority(mesh),
        native_point(mesh, native), static_cast<arch::boundary::BoundaryAxis>(axis),
        side ? arch::boundary::BoundarySide::Upper : arch::boundary::BoundarySide::Lower,
        mesh.semantics);
}

/** Build the immutable read-only callback input for one native face center. */
arch::boundary::BoundaryCoordinates coordinates(const arch::elliptic::EllipticMesh& mesh, int index,
    const std::array<double, 3>& native, double time) {
    const int axis = index / 2, side = index & 1;
    arch::boundary::BoundaryCoordinates result;
    result.axis = axis == 0 ? arch::boundary::BoundaryAxis::X1
        : axis == 1 ? arch::boundary::BoundaryAxis::X2 : arch::boundary::BoundaryAxis::X3;
    result.side = side ? arch::boundary::BoundarySide::Upper : arch::boundary::BoundarySide::Lower;
    result.purpose = arch::boundary::BoundaryPurpose::Gravity;
    result.dimension = mesh.dimension;
    result.time = time;
    result.physical_distance = 0.;
    result.native_position = native;
    result.cartesian_normal = outward_normal(mesh, axis, side, native);
    result.point = native_point(mesh, native);
    result.ghost_point = result.point;
    return result;
}

/** Representative native midpoint of a domain side. */
std::array<double, 3> side_center(const arch::elliptic::EllipticMesh& mesh, int index) {
    const int axis = index / 2, side = index & 1;
    std::array<double, 3> native{};
    for (int a = 0; a < mesh.dimension; ++a)
        native[a] = mesh.origin[a] + 0.5 * mesh.cells[a] * mesh.spacing[a];
    native[axis] = mesh.origin[axis] + (side ? mesh.cells[axis] * mesh.spacing[axis] : 0.);
    return native;
}

/** Translate the shared enum into the composite operator face kind. */
arch::elliptic::FaceBoundaryKind face_kind(arch::boundary::GravityBoundaryCondition kind) {
    switch (kind) {
    case arch::boundary::GravityBoundaryCondition::Dirichlet: return arch::elliptic::FaceBoundaryKind::Dirichlet;
    case arch::boundary::GravityBoundaryCondition::Neumann: return arch::elliptic::FaceBoundaryKind::Neumann;
    case arch::boundary::GravityBoundaryCondition::Robin: return arch::elliptic::FaceBoundaryKind::Robin;
    case arch::boundary::GravityBoundaryCondition::Periodic: return arch::elliptic::FaceBoundaryKind::Periodic;
    }
    throw std::invalid_argument("User gravity boundary requires a known face kind");
}

/** Reject a side policy that is not finite and coercive. */
void validate_conditions(const arch::elliptic::FaceBoundaryCondition& condition, int index) {
    if (!std::isfinite(condition.a) || !std::isfinite(condition.b))
        throw std::invalid_argument("User gravity boundary coefficients are not finite");
    if (condition.kind == arch::elliptic::FaceBoundaryKind::Dirichlet) {
        if (condition.a != 1. || condition.b != 0.)
            throw std::invalid_argument("User gravity Dirichlet side requires a=1,b=0");
    } else if (condition.kind == arch::elliptic::FaceBoundaryKind::Neumann) {
        if (condition.a != 0. || condition.b != 1.)
            throw std::invalid_argument("User gravity Neumann side requires a=0,b=1");
    } else if (condition.kind == arch::elliptic::FaceBoundaryKind::Robin) {
        if (!(condition.a >= 0.) || !(condition.b > 0.))
            throw std::invalid_argument("User gravity Robin side requires finite a>=0,b>0");
    } else if (condition.kind != arch::elliptic::FaceBoundaryKind::Periodic) {
        throw std::invalid_argument("User gravity boundary requires a known face kind");
    }
    (void)index;
}
} // namespace

bool gravity_regular_side(const arch::elliptic::EllipticMesh& mesh, int index) {
    const int axis = index / 2, side = index & 1;
    const double coordinate=mesh.origin[axis]+(side ? mesh.cells[axis]*mesh.spacing[axis] : 0.);
    return GridMetrics::IsCoordinateJoin(mesh.geometry,mesh.dimension,axis,coordinate);
}

arch::elliptic::CompositeBoundary gravity_homogeneous_boundary(
    const arch::elliptic::EllipticMesh& mesh, const std::array<bool, 3>& periodic,
    arch::elliptic::FaceBoundaryKind kind) {
    arch::elliptic::CompositeBoundary boundary;
    for (int index = 0; index < 6; ++index) {
        const int axis = index / 2;
        if (axis < mesh.dimension && periodic[axis]) {
            boundary.sides[index] = arch::elliptic::FaceBoundaryKind::Periodic;
            boundary.conditions[index] = {arch::elliptic::FaceBoundaryKind::Periodic, 0., 1.};
        } else if (axis >= mesh.dimension || gravity_regular_side(mesh, index)) {
            boundary.sides[index] = arch::elliptic::FaceBoundaryKind::Neumann;
            boundary.conditions[index] = {arch::elliptic::FaceBoundaryKind::Neumann, 0., 1.};
        } else {
            boundary.sides[index] = kind;
            boundary.conditions[index] = kind == arch::elliptic::FaceBoundaryKind::Dirichlet
                ? arch::elliptic::FaceBoundaryCondition{kind, 1., 0.}
                : arch::elliptic::FaceBoundaryCondition{kind, 0., 1.};
        }
    }
    return boundary;
}

arch::boundary::GravityBoundaryData gravity_user_sample(
    const arch::boundary::GravityBoundaryFunction& callback, const SimConfig& config,
    const SpeciesManager& species, const arch::elliptic::EllipticMesh& mesh, int index,
    const std::array<double, 3>& native, double time) {
    if (!callback) throw std::invalid_argument("User gravity boundary requires a resolved callback");
    if (!std::isfinite(time))
        throw std::invalid_argument("User gravity boundary time is not finite");
    const arch::boundary::GravityBoundaryContext context(coordinates(mesh, index, native, time),
        config, species);
    return callback(context);
}

arch::elliptic::CompositeBoundary gravity_user_boundary(
    const arch::boundary::GravityBoundaryFunction& callback, const SimConfig& config,
    const SpeciesManager& species, const arch::elliptic::EllipticMesh& mesh,
    const std::array<bool, 3>& periodic, double time) {
    if (!std::isfinite(time))
        throw std::invalid_argument("User gravity boundary time is not finite");
    arch::elliptic::CompositeBoundary boundary;
    for (int axis = 0; axis < 3; ++axis) for (int side = 0; side < 2; ++side) {
        const int index = 2 * axis + side;
        if (axis >= mesh.dimension || gravity_regular_side(mesh, index)) {
            boundary.sides[index] = arch::elliptic::FaceBoundaryKind::Neumann;
            boundary.conditions[index] = {arch::elliptic::FaceBoundaryKind::Neumann, 0., 1.};
            continue;
        }
        const auto data = gravity_user_sample(callback, config, species, mesh, index,
            side_center(mesh, index), time);
        if (!std::isfinite(data.a) || !std::isfinite(data.b) || !std::isfinite(data.c))
            throw std::invalid_argument("User gravity boundary coefficients are not finite");
        arch::elliptic::FaceBoundaryCondition condition;
        condition.kind = face_kind(data.kind);
        condition.a = data.a;
        condition.b = data.b;
        if (condition.kind == arch::elliptic::FaceBoundaryKind::Periodic) {
            // A periodic side is a topology statement, not a scalar condition:
            // the payload must be exactly zero before the internal pair weight
            // is canonicalized to b=1.
            if (data.a != 0. || data.b != 0. || data.c != 0.)
                throw std::invalid_argument("User gravity periodic side requires a=b=c=0");
            condition.a = 0.;
            condition.b = 1.;
        } else {
            validate_conditions(condition, index);
        }
        boundary.sides[index] = condition.kind;
        boundary.conditions[index] = condition;
    }
    // Periodic pairs are a physical topology property; a callback may neither
    // invent nor drop them.
    for (int axis = 0; axis < mesh.dimension; ++axis) {
        const bool lower = boundary.sides[2 * axis] == arch::elliptic::FaceBoundaryKind::Periodic;
        const bool upper = boundary.sides[2 * axis + 1] == arch::elliptic::FaceBoundaryKind::Periodic;
        if (lower != upper)
            throw std::invalid_argument("User gravity periodic boundary requires paired sides");
        if (lower != periodic[axis])
            throw std::invalid_argument("User gravity periodic sides disagree with the AMR topology");
    }
    return boundary;
}

bool gravity_same_structure(const arch::elliptic::CompositeBoundary& left,
    const arch::elliptic::CompositeBoundary& right) {
    for (int index = 0; index < 6; ++index) {
        if (left.sides[index] != right.sides[index]) return false;
        if (left.conditions[index].kind != right.conditions[index].kind) return false;
        if (left.conditions[index].a != right.conditions[index].a) return false;
        if (left.conditions[index].b != right.conditions[index].b) return false;
    }
    return true;
}
} // namespace Physical::Gravity
