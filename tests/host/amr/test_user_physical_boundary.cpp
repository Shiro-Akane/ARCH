/**
 * @file test_user_physical_boundary.cpp
 * @brief Independent checks for the shared EOS-backed physical boundary leaf.
 *
 * Covers the frozen contract: native/curvilinear geometry normals and metric
 * distances, the EOS-backed interior snapshot, hydro energy/composition
 * construction, the diffusion ghost rules (Value/NormalGradient/OutwardFlux),
 * rejected requests and channel misuse. The manager links this file into the
 * existing boundary_plan target; it defines test_user_physical_boundary() and
 * deliberately has no main.
 * Explicit native RZ cases qualify coordinates, actual EOS point-law V/W
 * boundary construction and candidate publication identity. They do not
 * substitute for final whole-domain Runtime ghost/EOS or evolution gates.
 */
#include "amr/AMRControl.h"
#include "amr/exchange/HostBoundaryPlan.h"
#include "driver/runtime/TopologyIdentityRegistry.h"
#include "driver/schedule/StageScheduler.h"
#include "numerics/integrator/HydroBoundaryAuthority.h"
#include "physics/boundary/NativeRzBoundary.h"
#include "physics/boundary/PhysicalBoundary.h"
#include "physics/boundary/PhysicalBoundaryHandler.h"
#include "physics/boundary/UserBoundary.h"
#include "numerics/integrator/TimeIntegratorHelper.h"
#include "physics/eos/IdealGas.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace arch::boundary;

void require(bool condition, std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

template <typename Function>
void require_rejected(Function&& function, std::string_view message)
{
    bool rejected = false;
    try {
        function();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, message);
}

// Relative comparison against an independently recomputed expectation.
void close(double actual, double expected, double tolerance, std::string_view message)
{
    if (!std::isfinite(actual) ||
        std::abs(actual - expected) > tolerance * std::max(1.0, std::abs(expected)))
        throw std::runtime_error(std::string(message));
}

void require_identical(const FluidVector& actual, const FluidVector& expected, std::string_view message)
{
    require(actual.rho == expected.rho && actual.mom_u == expected.mom_u &&
            actual.mom_v == expected.mom_v && actual.mom_w == expected.mom_w &&
            actual.eng == expected.eng, message);
}

SpeciesManager make_species()
{
    SpeciesManager species;
    species.add_species("a", 1.0, 1.0, 5.0 / 3.0, 1.0e8);
    species.add_species("b", 4.0, 2.0, 1.4, 2.0e8);
    return species;
}

// The EOS owns an immutable copy of the registered species, so the registry is
// fully populated before the policy is constructed.
struct Fixture {
    SpeciesManager species = make_species();
    IdealGas eos{1.4, species};
    SimConfig config{};
    Fixture() {
        config.physics.diffusion.use_diffusion = true;
        config.physics.diffusion.use_thermal_diffusion = true;
        config.physics.diffusion.use_viscous_diffusion = true;
        config.physics.diffusion.use_species_diffusion = true;
    }
};

Grid make_grid(int dimension, const std::string& geometry)
{
    Grid grid(4, 0.0, 16.0, 0.0, 2.0, 0.0, 2.0);
    grid.dim = dimension;
    grid.geometry = geometry;
    grid.InitializeTopology();
    return grid;
}

const std::vector<double> kComposition{0.7, 0.3};

PrimitiveData interior_primitive(const std::vector<double>& composition)
{
    PrimitiveData primitive;
    primitive.rho = 0.5;
    primitive.u = 2.0;
    primitive.v = -0.5;
    primitive.w = 0.25;
    primitive.SetTemperature(1000.0);
    primitive.mass_fractions = composition;
    return primitive;
}

FluidVector interior_state(const Fixture& fixture, const std::vector<double>& composition)
{
    return ProblemHelper::detail::InitialConservedState(
        interior_primitive(composition), fixture.eos, fixture.config.numerics);
}

// Expected conserved energy of a resolved ghost primitive: rho * e_int(rho,T,Xi)
// plus the kinetic term, evaluated independently of the boundary code path.
double expected_energy(const Fixture& fixture, double rho, double temperature,
                       double u, double v, double w, const std::vector<double>& composition)
{
    const double eint = fixture.eos.get_eint_from_T(rho, temperature, composition.data());
    return rho * eint + 0.5 * rho * (u * u + v * v + w * w);
}

double vector_magnitude(const std::array<double, 3>& value)
{
    return std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
}

// Independent chord direction of the native mapping: the direction of
// increasing (or decreasing) one native coordinate through the shared
// Grid::PhysicalCoordsFromNative conversion.
std::array<double, 3> chord_direction(const Grid& grid, std::array<double, 3> native,
                                     int axis, double sign)
{
    const double step = 1.0e-4;
    std::array<double, 3> high = native;
    std::array<double, 3> low = native;
    high[axis] += sign * step;
    low[axis] -= sign * step;
    const PointCoords a = Grid::PhysicalCoordsFromNative(
        grid.dim, grid.geometry, high[0], high[1], high[2]);
    const PointCoords b = Grid::PhysicalCoordsFromNative(
        grid.dim, grid.geometry, low[0], low[1], low[2]);
    std::array<double, 3> direction{a.x - b.x, a.y - b.y, a.z - b.z};
    const double magnitude = vector_magnitude(direction);
    require(magnitude > 0.0, "native chord must have a direction");
    for (double& component : direction) component /= magnitude;
    return direction;
}

void test_geometry_normals()
{
    struct GeometryCase {
        int dimension;
        const char* geometry;
        std::array<double, 3> face;
    };
    // Nontrivial angles: theta = 1.0 rad, phi = 0.7 rad.
    const std::array<GeometryCase, 9> cases{{
        {1, "cartesian", {7.3, 0.0, 0.0}},
        {2, "cartesian", {7.3, 1.4, 0.0}},
        {3, "cartesian", {7.3, 1.4, 0.7}},
        {1, "cylindrical", {7.3, 0.0, 0.0}},
        {2, "cylindrical", {7.3, 0.9, 0.0}},
        {3, "cylindrical", {7.3, 1.4, 0.7}},
        {1, "spherical", {7.3, 0.0, 0.0}},
        {2, "spherical", {7.3, 0.9, 0.0}},
        {3, "spherical", {7.3, 1.0, 0.7}},
    }};
    for (const GeometryCase& item : cases) {
        const Grid grid = make_grid(item.dimension, item.geometry);
        for (int axis = 0; axis < item.dimension; ++axis) {
            const auto axis_id = static_cast<BoundaryAxis>(axis);
            const auto lower = MakeBoundaryCoordinates(grid, item.face, axis_id, BoundarySide::Lower, 0.0);
            const auto upper = MakeBoundaryCoordinates(grid, item.face, axis_id, BoundarySide::Upper, 0.0);
            close(vector_magnitude(lower.cartesian_normal), 1.0, 1.0e-12, "unit outward normal (lower)");
            close(vector_magnitude(upper.cartesian_normal), 1.0, 1.0e-12, "unit outward normal (upper)");
            const auto outward = chord_direction(grid, item.face, axis, 1.0);
            const auto inward = chord_direction(grid, item.face, axis, -1.0);
            for (int component = 0; component < 3; ++component) {
                close(upper.cartesian_normal[component], outward[component], 1.0e-9,
                      "upper normal follows the native basis");
                close(lower.cartesian_normal[component], inward[component], 1.0e-9,
                      "lower normal follows the native basis");
                close(lower.cartesian_normal[component] + upper.cartesian_normal[component], 0.0,
                      1.0e-12, "lower and upper normals are opposite");
            }
            // Orthogonality against the remaining native directions.
            for (int other = 0; other < item.dimension; ++other) {
                if (other == axis) continue;
                const auto tangent = chord_direction(grid, item.face, other, 1.0);
                double projection = 0.0;
                for (int component = 0; component < 3; ++component)
                    projection += upper.cartesian_normal[component] * tangent[component];
                close(projection, 0.0, 1.0e-9, "normal is orthogonal to the other native basis vectors");
            }
            if (std::string(item.geometry) == "cartesian") {
                for (int component = 0; component < 3; ++component) {
                    const double expected = component == axis ? 1.0 : 0.0;
                    close(upper.cartesian_normal[component], expected, 1.0e-15,
                          "cartesian upper normal is the exact native basis vector");
                    close(lower.cartesian_normal[component], -expected, 1.0e-15,
                          "cartesian lower normal is the exact negative native basis vector");
                }
            }
            // 1-D radial faces use the equatorial radial mapping.
            if (item.dimension == 1 && std::string(item.geometry) != "cartesian") {
                close(upper.cartesian_normal[0], 1.0, 1.0e-15, "1-D radial normal is +e_x");
                close(upper.cartesian_normal[1], 0.0, 1.0e-15, "1-D radial normal has no y component");
                close(upper.cartesian_normal[2], 0.0, 1.0e-15, "1-D radial normal has no z component");
            }
        }
    }

    // Axis/pole degeneracy: the coordinate-basis direction is still reported,
    // while the metric distance honestly collapses to zero.
    const Grid cylindrical = make_grid(2, "cylindrical");
    const auto axis_face = MakeBoundaryCoordinates(
        cylindrical, {0.0, 0.4, 0.0}, BoundaryAxis::X2, BoundarySide::Upper, 0.2,
        1, BoundaryPurpose::Diffusion, {0.0, 0.5, 0.0});
    close(vector_magnitude(axis_face.cartesian_normal), 1.0, 1.0e-12, "axis normal stays a unit direction");
    close(axis_face.cartesian_normal[0], -std::sin(0.4), 1.0e-15, "axis normal uses the phi basis");
    close(axis_face.cartesian_normal[1], std::cos(0.4), 1.0e-15, "axis normal uses the phi basis");
    close(axis_face.physical_distance, 0.0, 1.0e-15, "radial-axis phi distance is zero");

    const Grid spherical = make_grid(3, "spherical");
    const auto pole_face = MakeBoundaryCoordinates(
        spherical, {2.0, 0.0, 0.7}, BoundaryAxis::X3, BoundarySide::Upper, 0.2,
        1, BoundaryPurpose::Diffusion, {2.0, 0.0, 0.9});
    close(vector_magnitude(pole_face.cartesian_normal), 1.0, 1.0e-12, "pole normal stays a unit direction");
    close(pole_face.physical_distance, 0.0, 1.0e-15, "pole phi distance is zero");

    // Rejected coordinate requests.
    const Grid one_dimensional = make_grid(1, "cartesian");
    require_rejected([&] {
        (void)MakeBoundaryCoordinates(one_dimensional, {1.0, 0.0, 0.0},
            BoundaryAxis::X2, BoundarySide::Upper, 0.0);
    }, "inactive boundary axis was accepted");
    require_rejected([&] {
        (void)MakeBoundaryCoordinates(one_dimensional, {1.0, 0.0, 0.0},
            static_cast<BoundaryAxis>(7), BoundarySide::Upper, 0.0);
    }, "unknown boundary axis was accepted");
    require_rejected([&] {
        (void)MakeBoundaryCoordinates(one_dimensional, {1.0, 0.0, 0.0},
            BoundaryAxis::X1, BoundarySide::Upper, 0.0, -1);
    }, "negative ghost depth was accepted");
    require_rejected([&] {
        (void)MakeBoundaryCoordinates(one_dimensional, {1.0, 0.0, 0.0},
            BoundaryAxis::X1, static_cast<BoundarySide>(5), 0.0);
    }, "unknown boundary side was accepted");
    require_rejected([&] {
        (void)MakeBoundaryCoordinates(one_dimensional,
            {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0},
            BoundaryAxis::X1, BoundarySide::Upper, 0.0);
    }, "non-finite face coordinate was accepted");
    require_rejected([&] {
        (void)MakeBoundaryCoordinates(one_dimensional, {1.0, 0.0, 0.0},
            BoundaryAxis::X1, BoundarySide::Upper,
            std::numeric_limits<double>::infinity());
    }, "non-finite boundary time was accepted");
}

void test_coordinate_dimensions()
{
    // Cartesian: dx1 = 1 and the face-to-ghost normal distance is the native offset.
    const Grid cartesian = make_grid(1, "cartesian");
    const auto depth_two = MakeBoundaryCoordinates(cartesian, {3.0, 0.0, 0.0},
        BoundaryAxis::X1, BoundarySide::Upper, 0.4, 2, BoundaryPurpose::Diffusion, {5.5, 0.0, 0.0});
    require(depth_two.dimension == 1, "grid dimension was not staged");
    require(depth_two.ghost_depth == 2, "ghost depth was not staged");
    require(depth_two.axis == BoundaryAxis::X1 && depth_two.side == BoundarySide::Upper,
            "axis/side were not staged");
    require(depth_two.purpose == BoundaryPurpose::Diffusion, "purpose was not staged");
    require(depth_two.time == 0.4, "boundary time was not staged verbatim");
    require(depth_two.native_position == std::array<double, 3>{3.0, 0.0, 0.0},
            "face native position was not staged");
    close(depth_two.physical_distance, 2.5, 1.0e-15, "cartesian face-to-ghost distance");
    require(depth_two.point.x == 3.0, "face physical coordinate uses the shared mapping");

    // Depth zero evaluates the face itself: the ghost position defaults to it.
    const auto face_only = MakeBoundaryCoordinates(cartesian, {3.0, 0.0, 0.0},
        BoundaryAxis::X1, BoundarySide::Lower, 0.4);
    require(face_only.ghost_depth == 0, "default ghost depth is zero");
    require(face_only.physical_distance == 0.0, "depth-zero distance is zero");
    require(face_only.ghost_point.x == face_only.point.x &&
            face_only.ghost_point.y == face_only.point.y &&
            face_only.ghost_point.z == face_only.point.z,
            "depth-zero ghost point defaults to the face point");
    close(face_only.cartesian_normal[0], -1.0, 1.0e-15, "lower x face points inward");

    // Curvilinear angular steps use the shared metric: arc length, not the chord.
    const Grid cylindrical = make_grid(2, "cylindrical");
    const auto azimuthal = MakeBoundaryCoordinates(cylindrical, {2.0, 0.5, 0.0},
        BoundaryAxis::X2, BoundarySide::Upper, 0.0, 1, BoundaryPurpose::Diffusion, {2.0, 0.75, 0.0});
    close(azimuthal.physical_distance, 2.0 * 0.25, 1.0e-14, "cylindrical r*dphi distance");
    const PointCoords expected_point =
        Grid::PhysicalCoordsFromNative(2, "cylindrical", 2.0, 0.75, 0.0);
    require(azimuthal.ghost_point.x == expected_point.x &&
            azimuthal.ghost_point.y == expected_point.y &&
            azimuthal.ghost_point.z == expected_point.z &&
            azimuthal.ghost_point.phi_cy == expected_point.phi_cy,
            "ghost point uses the shared native mapping");

    const Grid spherical = make_grid(3, "spherical");
    const auto polar = MakeBoundaryCoordinates(spherical, {2.0, 0.5, 0.3},
        BoundaryAxis::X2, BoundarySide::Upper, 0.0, 1, BoundaryPurpose::Diffusion, {2.0, 0.6, 0.3});
    close(polar.physical_distance, 2.0 * 0.1, 1.0e-14, "spherical r*dtheta distance");
    const auto azimuth = MakeBoundaryCoordinates(spherical, {2.0, 0.5, 0.3},
        BoundaryAxis::X3, BoundarySide::Upper, 0.0, 1, BoundaryPurpose::Diffusion, {2.0, 0.5, 0.5});
    close(azimuth.physical_distance, 2.0 * std::sin(0.5) * 0.2, 1.0e-14, "spherical r*sin(theta)*dphi distance");

    const Grid cylindrical_3d = make_grid(3, "cylindrical");
    const auto z_face = MakeBoundaryCoordinates(cylindrical_3d, {1.5, 0.4, 0.3},
        BoundaryAxis::X2, BoundarySide::Upper, 0.0, 1, BoundaryPurpose::Diffusion, {1.5, 0.6, 0.3});
    close(z_face.physical_distance, 0.2, 1.0e-15, "cylindrical z distance is a length");
    for (int component = 0; component < 3; ++component)
        close(z_face.cartesian_normal[component], component == 2 ? 1.0 : 0.0, 1.0e-15,
              "cylindrical z normal is e_z");
}

// Independent meridional chart expectations: x=r, y=0, z=z_native,
// n_r=+/-e_x, n_z=+/-e_z, d=|ghost_axis-face_axis|. These checks never use
// Grid::PhysicalCoordsFromNative or the metric helper to construct the oracle.
void test_native_rz_coordinates()
{
    constexpr auto rz = GridMetrics::GeometrySemantics::AxisymmetricRz;
    Grid grid(4, 0.0, 16.0, -2.0, 6.0, 0.0, 2.0);
    grid.dim = 2;
    grid.geometry = "cylindrical";
    grid.InitializeTopology(rz);
    struct CoordinateCase {
        std::array<double, 3> face;
        std::array<double, 3> ghost;
        BoundaryAxis axis;
        BoundarySide side;
        int depth;
    };
    const std::array<CoordinateCase, 8> cases{{
        {{2.0, -1.5, 0.0}, {1.75, -1.5, 0.0}, BoundaryAxis::X1, BoundarySide::Lower, 1},
        {{2.0, 3.0, 0.0}, {2.75, 3.0, 0.0}, BoundaryAxis::X1, BoundarySide::Upper, 2},
        {{3.0, -2.0, 0.0}, {3.0, -2.5, 0.0}, BoundaryAxis::X2, BoundarySide::Lower, 1},
        {{3.0, 6.0, 0.0}, {3.0, 7.5, 0.0}, BoundaryAxis::X2, BoundarySide::Upper, 2},
        {{0.0, 1.0, 0.0}, {-0.25, 1.0, 0.0}, BoundaryAxis::X1, BoundarySide::Lower, 1},
        {{0.0, -2.0, 0.0}, {-0.25, -2.5, 0.0}, BoundaryAxis::X2, BoundarySide::Lower, 1},
        {{0.0, 6.0, 0.0}, {-0.75, 7.5, 0.0}, BoundaryAxis::X2, BoundarySide::Upper, 2},
        {{2.0, 1.0, 0.0}, {2.0, 1.0, 0.0}, BoundaryAxis::X1, BoundarySide::Upper, 0},
    }};
    for (const auto& item : cases) {
        const auto coordinates = MakeBoundaryCoordinates(grid, item.face,
            item.axis, item.side, .375, item.depth, BoundaryPurpose::Diffusion,
            item.ghost, rz);
        const auto expected_ghost = item.depth == 0 ? item.face : item.ghost;
        const auto check_point = [&](const PointCoords& point, const std::array<double, 3>& native) {
            close(point.x, native[0], 1.e-15, "RZ x is the signed native radial coordinate");
            close(point.y, 0.0, 1.e-15, "RZ meridional y is zero");
            close(point.z, native[1], 1.e-15, "RZ z is an axial length");
            close(point.r_cy, native[0], 1.e-15, "RZ cylindrical radius preserves signed ghost coordinates");
            close(point.z_cy, native[1], 1.e-15, "RZ cylindrical z is an axial length");
            close(point.phi_cy, 0.0, 1.e-15, "RZ cylindrical azimuth is zero");
            close(point.r, std::hypot(native[0], native[1]), 1.e-15, "RZ spherical radius is distinct from native radius");
        };
        check_point(coordinates.point, item.face);
        check_point(coordinates.ghost_point, expected_ghost);
        const int axis = static_cast<int>(item.axis);
        const int cartesian_axis = axis == 0 ? 0 : 2;
        const double sign = item.side == BoundarySide::Lower ? -1.0 : 1.0;
        const auto direct_normal = BoundaryCartesianNormal(grid, coordinates.point, item.axis, item.side, rz);
        const auto wrapped_normal = detail::PhysicalBoundaryNormal(grid, coordinates.point, item.axis, item.side, rz);
        for (int component = 0; component < 3; ++component) {
            const double expected = component == cartesian_axis ? sign : 0.0;
            close(coordinates.cartesian_normal[component], expected, 1.e-15, "RZ face outward Cartesian normal");
            close(direct_normal[component], expected, 1.e-15, "RZ direct outward normal");
            close(wrapped_normal[component], expected, 1.e-15, "RZ wrapped outward normal");
        }
        close(detail::PhysicalBoundaryMetric(grid, coordinates.point, item.axis, rz),
            1.0, 1.e-15, "RZ radial and axial coordinates have unit length metric");
        close(coordinates.physical_distance, std::abs(expected_ghost[axis] - item.face[axis]),
            1.e-15, "RZ face-to-ghost distance does not include a radius factor");
        require(coordinates.native_position == item.face && coordinates.dimension == 2 &&
            coordinates.ghost_depth == item.depth && coordinates.time == .375 &&
            coordinates.purpose == BoundaryPurpose::Diffusion,
            "RZ coordinates lost the actual native request identity");
    }

    // The same native numbers intentionally retain the legacy polar meaning
    // when no explicit chart is supplied. Existing dimensional tests remain.
    const auto legacy = MakeBoundaryCoordinates(grid, {2.0, .4, 0.0},
        BoundaryAxis::X2, BoundarySide::Upper, 0.0, 1,
        BoundaryPurpose::Diffusion, {2.0, .6, 0.0});
    close(legacy.point.x, 2.0 * std::cos(.4), 1.e-15, "default cylindrical coordinates retain polar x");
    close(legacy.point.y, 2.0 * std::sin(.4), 1.e-15, "default cylindrical coordinates retain polar y");
    close(legacy.point.z, 0.0, 1.e-15, "default cylindrical 2D has no axial coordinate");
    close(legacy.physical_distance, .4, 1.e-15, "default cylindrical azimuth retains r*dphi distance");

    const auto reject_chart = [&](const Grid& candidate, GridMetrics::GeometrySemantics semantics) {
        const PointCoords point{};
        require_rejected([&] { (void)BoundaryCartesianNormal(candidate, point,
            BoundaryAxis::X1, BoundarySide::Upper, semantics); }, "invalid native RZ normal chart accepted");
        require_rejected([&] { (void)detail::PhysicalBoundaryNormal(candidate, point,
            BoundaryAxis::X1, BoundarySide::Upper, semantics); }, "invalid native RZ wrapped normal chart accepted");
        require_rejected([&] { (void)detail::PhysicalBoundaryMetric(candidate, point,
            BoundaryAxis::X1, semantics); }, "invalid native RZ metric chart accepted");
        require_rejected([&] { (void)MakeBoundaryCoordinates(candidate, {1.0, 0.0, 0.0},
            BoundaryAxis::X1, BoundarySide::Upper, 0.0, 0, BoundaryPurpose::Hydro,
            {}, semantics); }, "invalid native RZ coordinate chart accepted");
    };
    reject_chart(grid, static_cast<GridMetrics::GeometrySemantics>(255));
    for (const int dimension : {0, 1, 3}) {
        auto invalid = grid;
        invalid.dim = dimension;
        reject_chart(invalid, rz);
    }
    for (const auto* geometry : {"cartesian", "spherical", "unsupported"}) {
        auto invalid = grid;
        invalid.geometry = geometry;
        reject_chart(invalid, rz);
    }
    require_rejected([&] { (void)MakeBoundaryCoordinates(grid, {1.0, 0.0, 0.0},
        BoundaryAxis::X3, BoundarySide::Upper, 0.0, 0, BoundaryPurpose::Hydro, {}, rz); },
        "inactive native RZ third direction was accepted");
}

void test_interior_snapshot(const Fixture& fixture)
{
    // The callback sees a read-only, EOS-backed copy of the interior cell.
    static_assert(std::is_base_of_v<BoundaryCoordinates, PhysicalBoundaryContext>);
    static_assert(std::is_same_v<decltype(PhysicalBoundaryContext::interior), PrimitiveData>);
    static_assert(std::is_same_v<decltype(PhysicalBoundaryContext::config), const SimConfig&>);
    static_assert(std::is_same_v<decltype(PhysicalBoundaryContext::species), const SpeciesManager&>);
    static_assert(std::is_same_v<decltype((std::declval<const PhysicalBoundaryContext&>().interior)),
                                 const PrimitiveData&>);

    const Grid grid = make_grid(1, "cartesian");
    const auto coordinates = MakeBoundaryCoordinates(grid, {3.0, 0.0, 0.0},
        BoundaryAxis::X1, BoundarySide::Upper, 0.75, 1, BoundaryPurpose::Hydro, {4.0, 0.0, 0.0});
    const FluidVector interior = interior_state(fixture, kComposition);
    const FluidVector interior_before = interior;

    bool visited = false;
    PrimitiveData ghost;
    ghost.rho = 0.75;
    ghost.u = -1.5;
    ghost.v = 0.25;
    ghost.w = 0.125;
    ghost.SetTemperature(1500.0);
    ghost.mass_fractions = {0.25, 0.75};
    PhysicalBoundaryData data;
    data.hydro = ghost;

    const auto callback = [&](const PhysicalBoundaryContext& context) {
        visited = true;
        // The context carries the staged coordinates and the copied interior snapshot.
        require(context.time == 0.75, "context lost the boundary time");
        require(context.dimension == grid.dim, "context lost the grid dimension");
        require(context.ghost_depth == 1, "context lost the ghost depth");
        require(context.axis == BoundaryAxis::X1 && context.side == BoundarySide::Upper,
                "context lost axis/side");
        require(context.purpose == BoundaryPurpose::Hydro, "context lost the purpose");
        require(context.physical_distance == 1.0, "context lost the physical distance");
        require(context.interior.rho == 0.5, "context interior density");
        require(context.interior.u == 2.0 && context.interior.v == -0.5 && context.interior.w == 0.25,
                "context interior native velocity");
        require(context.interior.has_temperature && context.interior.temperature == 1000.0,
                "context interior temperature comes from the EOS");
        require(context.interior.mass_fractions.size() == 2 &&
                context.interior.mass_fractions[0] == 0.7 &&
                context.interior.mass_fractions[1] == 0.3,
                "context interior carries the complete composition");
        require(context.interior.p == fixture.eos.get_pressure(
                    interior, kComposition.data()),
                "context interior pressure comes from the EOS");
        return data;
    };

    const auto evaluation = EvaluatePhysicalBoundary(coordinates, fixture.config, fixture.species,
        fixture.eos, callback, interior, std::span<const double>(kComposition));
    require(visited, "the physical boundary callback was not invoked");
    require_identical(interior, interior_before, "the const interior snapshot was modified");
    require(evaluation.mass_fractions.size() == 2, "hydro ghost composition size");
}

void test_hydro_temperature_state(const Fixture& fixture)
{
    const Grid grid = make_grid(1, "cartesian");
    const auto coordinates = MakeBoundaryCoordinates(grid, {3.0, 0.0, 0.0},
        BoundaryAxis::X1, BoundarySide::Upper, 0.5, 1, BoundaryPurpose::Hydro, {4.0, 0.0, 0.0});
    const FluidVector interior = interior_state(fixture, kComposition);

    PrimitiveData ghost;
    ghost.rho = 0.75;
    ghost.u = -1.5;
    ghost.v = 0.25;
    ghost.w = 0.125;
    ghost.SetTemperature(1500.0);
    ghost.mass_fractions = {0.25, 0.75};
    PhysicalBoundaryData data;
    data.hydro = ghost;
    const auto callback = [&data](const PhysicalBoundaryContext&) { return data; };

    const auto evaluation = EvaluatePhysicalBoundary(coordinates, fixture.config, fixture.species,
        fixture.eos, callback, interior, std::span<const double>(kComposition));
    require(evaluation.conserved.rho == 0.75, "hydro ghost density");
    close(evaluation.conserved.mom_u, 0.75 * -1.5, 1.0e-15, "hydro ghost x momentum");
    close(evaluation.conserved.mom_v, 0.75 * 0.25, 1.0e-15, "hydro ghost y momentum");
    close(evaluation.conserved.mom_w, 0.75 * 0.125, 1.0e-15, "hydro ghost z momentum");
    close(evaluation.conserved.eng,
          expected_energy(fixture, 0.75, 1500.0, -1.5, 0.25, 0.125, {0.25, 0.75}),
          1.0e-14, "hydro ghost energy follows the EOS temperature");
    require(evaluation.mass_fractions.size() == 2 &&
            evaluation.mass_fractions[0] == 0.25 && evaluation.mass_fractions[1] == 0.75,
            "hydro ghost composition is copied verbatim");
    require(evaluation.conditions.hydro.has_value(), "hydro request data is returned");

    // Pressure-based ghosts take the same conserved-state authority.
    PrimitiveData pressure_ghost = ghost;
    pressure_ghost.has_temperature = false;
    pressure_ghost.temperature = 0.0;
    pressure_ghost.p = fixture.eos.get_pressure_from_rho_T(
        pressure_ghost.rho, 1500.0, pressure_ghost.mass_fractions.data());
    PhysicalBoundaryData pressure_data;
    pressure_data.hydro = pressure_ghost;
    const auto pressure_callback = [&pressure_data](const PhysicalBoundaryContext&) { return pressure_data; };
    const auto pressure_evaluation = EvaluatePhysicalBoundary(coordinates, fixture.config,
        fixture.species, fixture.eos, pressure_callback, interior, std::span<const double>(kComposition));
    close(pressure_evaluation.conserved.eng, evaluation.conserved.eng, 1.0e-12,
          "pressure and temperature hydro ghosts agree");
}

void test_invalid_hydro_requests(const Fixture& fixture)
{
    const Grid grid = make_grid(1, "cartesian");
    const auto coordinates = MakeBoundaryCoordinates(grid, {3.0, 0.0, 0.0},
        BoundaryAxis::X1, BoundarySide::Upper, 0.5, 1, BoundaryPurpose::Hydro, {4.0, 0.0, 0.0});
    const FluidVector interior = interior_state(fixture, kComposition);

    PrimitiveData ghost;
    ghost.rho = 0.75;
    ghost.u = -1.5;
    ghost.v = 0.25;
    ghost.w = 0.125;
    ghost.SetTemperature(1500.0);
    ghost.mass_fractions = {0.25, 0.75};

    const auto evaluate = [&](const PrimitiveData& value) {
        PhysicalBoundaryData data;
        data.hydro = value;
        const auto callback = [&data](const PhysicalBoundaryContext&) { return data; };
        return EvaluatePhysicalBoundary(coordinates, fixture.config, fixture.species, fixture.eos,
            callback, interior, std::span<const double>(kComposition));
    };

    require_rejected([&] {
        PrimitiveData vacuum = ghost;
        vacuum.rho = 0.0;
        (void)evaluate(vacuum);
    }, "exact vacuum hydro ghost was accepted");
    require_rejected([&] {
        PrimitiveData nonfinite = ghost;
        nonfinite.rho = std::numeric_limits<double>::quiet_NaN();
        (void)evaluate(nonfinite);
    }, "non-finite hydro ghost density was accepted");
    require_rejected([&] {
        PrimitiveData nonfinite = ghost;
        nonfinite.u = std::numeric_limits<double>::infinity();
        (void)evaluate(nonfinite);
    }, "non-finite hydro ghost velocity was accepted");
    require_rejected([&] {
        PrimitiveData nonfinite = ghost;
        nonfinite.p = std::numeric_limits<double>::quiet_NaN();
        (void)evaluate(nonfinite);
    }, "non-finite hydro ghost pressure was accepted");
    require_rejected([&] {
        PrimitiveData nonfinite = ghost;
        nonfinite.SetTemperature(std::numeric_limits<double>::quiet_NaN());
        (void)evaluate(nonfinite);
    }, "non-finite hydro ghost temperature was accepted");
    require_rejected([&] {
        PrimitiveData negative = ghost;
        negative.SetTemperature(-1.0);
        (void)evaluate(negative);
    }, "non-positive hydro ghost temperature was accepted");
    require_rejected([&] {
        PrimitiveData missing = ghost;
        missing.mass_fractions = {1.0};
        (void)evaluate(missing);
    }, "incomplete hydro ghost composition was accepted");
    require_rejected([&] {
        PrimitiveData incomplete = ghost;
        incomplete.mass_fractions = {};
        (void)evaluate(incomplete);
    }, "missing hydro ghost composition was accepted");
    require_rejected([&] {
        PrimitiveData unnormalized = ghost;
        unnormalized.mass_fractions = {0.6, 0.6};
        (void)evaluate(unnormalized);
    }, "hydro ghost composition that does not sum to one was accepted");
    require_rejected([&] {
        PrimitiveData negative = ghost;
        negative.mass_fractions = {1.2, -0.2};
        (void)evaluate(negative);
    }, "negative hydro ghost mass fraction was accepted");

    // Interior snapshot validation: a well-formed snapshot round-trips.
    const PrimitiveData snapshot = BoundaryInteriorPrimitive(
        interior, std::span<const double>(kComposition), fixture.eos);
    require(snapshot.rho == 0.5 && snapshot.u == 2.0 && snapshot.v == -0.5 && snapshot.w == 0.25,
            "interior snapshot density/velocity");
    require(snapshot.has_temperature && snapshot.temperature == 1000.0,
            "interior snapshot temperature");
    require(snapshot.mass_fractions == kComposition, "interior snapshot composition");
    require_rejected([&] {
        std::vector<double> short_composition{1.0};
        (void)EvaluatePhysicalBoundary(coordinates, fixture.config, fixture.species, fixture.eos,
            [&](const PhysicalBoundaryContext&) { return PhysicalBoundaryData{}; },
            interior, std::span<const double>(short_composition));
    }, "interior composition with the wrong species count was accepted");

    const std::vector<double> unnormalized_interior{0.5, 0.4};
    require_rejected([&] {
        (void)EvaluatePhysicalBoundary(coordinates, fixture.config, fixture.species, fixture.eos,
            [&](const PhysicalBoundaryContext&) {
                PhysicalBoundaryData data;
                data.hydro = ghost;
                return data;
            },
            interior, std::span<const double>(unnormalized_interior));
    }, "unnormalized interior composition was accepted");

    FluidVector unresolved = interior;
    unresolved.eng = 0.0;
    require_rejected([&] {
        (void)BoundaryInteriorPrimitive(unresolved, std::span<const double>(kComposition), fixture.eos);
    }, "interior state without resolvable thermal energy was accepted");

    // A requested ghost that needs a conserved-state repair is rejected, never clamped.
    PrimitiveData below_floor = ghost;
    below_floor.rho = 0.5 * fixture.config.numerics.sml_rho;
    require_rejected([&] { (void)evaluate(below_floor); },
        "a hydro ghost below the density floor was silently repaired");
}

void test_diffusion_ghost_rules(const Fixture& fixture)
{
    const Grid grid = make_grid(3, "cartesian");
    const auto coordinates = MakeBoundaryCoordinates(grid, {3.0, 1.0, 1.0},
        BoundaryAxis::X1, BoundarySide::Upper, 0.5, 1, BoundaryPurpose::Diffusion, {4.0, 1.0, 1.0});
    close(coordinates.physical_distance, 1.0, 1.0e-15, "diffusion ghost distance");
    const FluidVector interior = interior_state(fixture, kComposition);

    const auto evaluate = [&](const PhysicalBoundaryData& data) {
        const auto callback = [&data](const PhysicalBoundaryContext&) { return data; };
        return EvaluatePhysicalBoundary(coordinates, fixture.config, fixture.species, fixture.eos,
            callback, interior, std::span<const double>(kComposition));
    };

    // No prescription preserves the interior state exactly.
    {
        const auto evaluation = evaluate(PhysicalBoundaryData{});
        require_identical(evaluation.conserved, interior, "diffusion without channels changed the interior");
        require(evaluation.mass_fractions == kComposition, "diffusion without channels changed the composition");
    }

    // Temperature Value: W_ghost = 2*W_face - W_interior.
    {
        PhysicalBoundaryData data;
        data.temperature = {ScalarBoundaryKind::Value, 1200.0};
        const auto evaluation = evaluate(data);
        close(evaluation.conserved.eng,
              expected_energy(fixture, 0.5, 2.0 * 1200.0 - 1000.0, 2.0, -0.5, 0.25, kComposition),
              1.0e-14, "temperature Value ghost");
        require(evaluation.mass_fractions == kComposition, "temperature Value must not change species");
    }

    // Temperature NormalGradient: W_ghost = W_interior + 2*d*g_out.
    {
        PhysicalBoundaryData data;
        data.temperature = {ScalarBoundaryKind::NormalGradient, 500.0};
        const auto evaluation = evaluate(data);
        close(evaluation.conserved.eng,
              expected_energy(fixture, 0.5, 1000.0 + 2.0 * 1.0 * 500.0, 2.0, -0.5, 0.25, kComposition),
              1.0e-14, "temperature NormalGradient ghost");
    }

    // Temperature OutwardFlux leaves the ghost untouched and stays in the conditions.
    {
        PhysicalBoundaryData data;
        data.temperature = {ScalarBoundaryKind::OutwardFlux, 1.0e9};
        const auto evaluation = evaluate(data);
        require_identical(evaluation.conserved, interior, "outward temperature flux changed the ghost");
        require(evaluation.conditions.temperature.kind == ScalarBoundaryKind::OutwardFlux &&
                evaluation.conditions.temperature.value == 1.0e9,
                "outward temperature flux was not returned for the direct-flux override");
    }

    // Native velocity channels.
    {
        PhysicalBoundaryData data;
        data.velocity[0] = {ScalarBoundaryKind::Value, 3.0};
        data.velocity[1] = {ScalarBoundaryKind::NormalGradient, -1.0};
        data.velocity[2] = {ScalarBoundaryKind::OutwardFlux, 5.0};
        const auto evaluation = evaluate(data);
        close(evaluation.conserved.mom_u, 0.5 * (2.0 * 3.0 - 2.0), 1.0e-15, "velocity Value ghost");
        close(evaluation.conserved.mom_v, 0.5 * (-0.5 + 2.0 * 1.0 * -1.0), 1.0e-15,
              "velocity NormalGradient ghost");
        close(evaluation.conserved.mom_w, 0.5 * 0.25, 1.0e-15, "velocity OutwardFlux keeps the base value");
        close(evaluation.conserved.eng,
              expected_energy(fixture, 0.5, 1000.0, 4.0, -2.5, 0.25, kComposition),
              1.0e-14, "velocity ghost energy");
        require(evaluation.conditions.velocity[2].kind == ScalarBoundaryKind::OutwardFlux,
                "outward velocity flux was not returned");
    }

    // Species Value and NormalGradient.
    {
        PhysicalBoundaryData data;
        data.species = {{ScalarBoundaryKind::Value, 0.4}, {ScalarBoundaryKind::Value, 0.6}};
        const auto evaluation = evaluate(data);
        require(evaluation.mass_fractions.size() == 2, "species Value composition size");
        close(evaluation.mass_fractions[0], 0.1, 1.0e-15, "species Value ghost fraction 0");
        close(evaluation.mass_fractions[1], 0.9, 1.0e-15, "species Value ghost fraction 1");
        close(evaluation.conserved.eng,
              expected_energy(fixture, 0.5, 1000.0, 2.0, -0.5, 0.25, {0.1, 0.9}),
              1.0e-14, "species Value ghost energy");
        close(evaluation.conserved.rho, 0.5, 1.0e-15, "species Value keeps density");
    }
    {
        PhysicalBoundaryData data;
        data.species = {{ScalarBoundaryKind::NormalGradient, 0.05},
                        {ScalarBoundaryKind::NormalGradient, -0.05}};
        const auto evaluation = evaluate(data);
        close(evaluation.mass_fractions[0], 0.7 + 2.0 * 1.0 * 0.05, 1.0e-15, "species gradient fraction 0");
        close(evaluation.mass_fractions[1], 0.3 - 2.0 * 1.0 * 0.05, 1.0e-15, "species gradient fraction 1");
    }
    {
        PhysicalBoundaryData data;
        data.species = {{ScalarBoundaryKind::OutwardFlux, 2.0e-8},
                        {ScalarBoundaryKind::OutwardFlux, -2.0e-8}};
        const auto evaluation = evaluate(data);
        require_identical(evaluation.conserved, interior, "outward species flux changed the ghost");
        require(evaluation.mass_fractions == kComposition, "outward species flux changed the composition");
        require(evaluation.conditions.species.size() == 2 &&
                evaluation.conditions.species[0].kind == ScalarBoundaryKind::OutwardFlux,
                "outward species flux was not returned");
    }

    // Combined channels on one ghost.
    {
        PhysicalBoundaryData data;
        data.temperature = {ScalarBoundaryKind::Value, 1200.0};
        data.velocity[0] = {ScalarBoundaryKind::Value, 3.0};
        data.velocity[1] = {ScalarBoundaryKind::NormalGradient, -1.0};
        data.velocity[2] = {ScalarBoundaryKind::OutwardFlux, 5.0};
        data.species = {{ScalarBoundaryKind::Value, 0.4}, {ScalarBoundaryKind::Value, 0.6}};
        const auto evaluation = evaluate(data);
        close(evaluation.conserved.mom_u, 0.5 * 4.0, 1.0e-15, "combined x momentum");
        close(evaluation.conserved.mom_v, 0.5 * -2.5, 1.0e-15, "combined y momentum");
        close(evaluation.conserved.mom_w, 0.5 * 0.25, 1.0e-15, "combined z momentum");
        close(evaluation.conserved.eng,
              expected_energy(fixture, 0.5, 1400.0, 4.0, -2.5, 0.25, {0.1, 0.9}),
              1.0e-14, "combined ghost energy");
        close(evaluation.mass_fractions[1], 0.9, 1.0e-15, "combined species ghost");
    }

    // Optional valid ghost data is the base for the diffusion channels.
    {
        PrimitiveData base;
        base.rho = 0.4;
        base.u = 1.0;
        base.v = 0.0;
        base.w = 0.0;
        base.SetTemperature(1200.0);
        base.mass_fractions = {0.25, 0.75};
        PhysicalBoundaryData data;
        data.hydro = base;
        data.temperature = {ScalarBoundaryKind::NormalGradient, 100.0};
        const auto evaluation = evaluate(data);
        close(evaluation.conserved.rho, 0.4, 1.0e-15, "ghost base density");
        close(evaluation.conserved.mom_u, 0.4, 1.0e-15, "ghost base velocity");
        close(evaluation.conserved.eng,
              expected_energy(fixture, 0.4, 1200.0 + 2.0 * 1.0 * 100.0, 1.0, 0.0, 0.0, {0.25, 0.75}),
              1.0e-14, "ghost base with temperature gradient");
        require(evaluation.mass_fractions[0] == 0.25 && evaluation.mass_fractions[1] == 0.75,
                "ghost base composition");
    }
}

void test_channel_misuse(const Fixture& fixture)
{
    const Grid grid = make_grid(3, "cartesian");
    const auto coordinates = MakeBoundaryCoordinates(grid, {3.0, 1.0, 1.0},
        BoundaryAxis::X1, BoundarySide::Upper, 0.5, 1, BoundaryPurpose::Diffusion, {4.0, 1.0, 1.0});
    const FluidVector interior = interior_state(fixture, kComposition);

    const auto evaluate = [&](const PhysicalBoundaryData& data) {
        const auto callback = [&data](const PhysicalBoundaryContext&) { return data; };
        return EvaluatePhysicalBoundary(coordinates, fixture.config, fixture.species, fixture.eos,
            callback, interior, std::span<const double>(kComposition));
    };

    // Unknown scalar kinds and non-finite values.
    {
        PhysicalBoundaryData data;
        data.temperature.kind = static_cast<ScalarBoundaryKind>(7);
        require_rejected([&] { (void)evaluate(data); }, "unknown scalar kind was accepted");
    }
    {
        PhysicalBoundaryData data;
        data.temperature = {ScalarBoundaryKind::Value, std::numeric_limits<double>::infinity()};
        require_rejected([&] { (void)evaluate(data); }, "non-finite scalar value was accepted");
    }
    {
        PhysicalBoundaryData data;
        data.velocity[0] = {ScalarBoundaryKind::NormalGradient,
                            std::numeric_limits<double>::quiet_NaN()};
        require_rejected([&] { (void)evaluate(data); }, "non-finite velocity gradient was accepted");
    }
    {
        PhysicalBoundaryData data;
        data.temperature = {ScalarBoundaryKind::Value, 0.0};
        require_rejected([&] { (void)evaluate(data); }, "non-positive temperature Value was accepted");
    }
    {
        PhysicalBoundaryData data;
        data.temperature = {ScalarBoundaryKind::Value, -10.0};
        require_rejected([&] { (void)evaluate(data); }, "negative temperature Value was accepted");
    }

    // Species prescriptions must be complete, uniform and correctly normalized.
    {
        PhysicalBoundaryData data;
        data.species = {{ScalarBoundaryKind::Value, 0.4}, {ScalarBoundaryKind::NormalGradient, 0.6}};
        require_rejected([&] { (void)evaluate(data); }, "mixed species prescriptions were accepted");
    }
    {
        PhysicalBoundaryData data;
        data.species = {{ScalarBoundaryKind::Value, 1.0}, {ScalarBoundaryKind::None, 0.0}};
        require_rejected([&] { (void)evaluate(data); }, "partial species prescriptions were accepted");
    }
    {
        PhysicalBoundaryData data;
        data.species = {{ScalarBoundaryKind::Value, 0.5}, {ScalarBoundaryKind::Value, 0.25},
                        {ScalarBoundaryKind::Value, 0.25}};
        require_rejected([&] { (void)evaluate(data); }, "species count mismatch was accepted");
    }
    {
        PhysicalBoundaryData data;
        data.species = {{ScalarBoundaryKind::Value, 0.4}, {ScalarBoundaryKind::Value, 0.4}};
        require_rejected([&] { (void)evaluate(data); },
            "species Value fractions that do not sum to one were accepted");
    }
    {
        PhysicalBoundaryData data;
        data.species = {{ScalarBoundaryKind::Value, 1.2}, {ScalarBoundaryKind::Value, -0.2}};
        require_rejected([&] { (void)evaluate(data); }, "negative species Value fraction was accepted");
    }
    {
        PhysicalBoundaryData data;
        data.species = {{ScalarBoundaryKind::NormalGradient, 0.1},
                        {ScalarBoundaryKind::NormalGradient, 0.1}};
        require_rejected([&] { (void)evaluate(data); },
            "species normal gradients that do not sum to zero were accepted");
    }
    {
        PhysicalBoundaryData data;
        data.species = {{ScalarBoundaryKind::OutwardFlux, 0.1},
                        {ScalarBoundaryKind::OutwardFlux, 0.1}};
        require_rejected([&] { (void)evaluate(data); },
            "species outward fluxes that do not sum to zero were accepted");
    }
    {
        PhysicalBoundaryData data;
        data.species = {{ScalarBoundaryKind::NormalGradient, 2.0},
                        {ScalarBoundaryKind::NormalGradient, -2.0}};
        require_rejected([&] { (void)evaluate(data); },
            "a negative diffusion ghost mass fraction was accepted");
    }

    // Transverse velocity remains physical in a one-dimensional shear flow.
    {
        const Grid one_dimensional = make_grid(1, "cartesian");
        const auto line = MakeBoundaryCoordinates(one_dimensional, {3.0, 0.0, 0.0},
            BoundaryAxis::X1, BoundarySide::Upper, 0.5, 1, BoundaryPurpose::Diffusion, {4.0, 0.0, 0.0});
        PhysicalBoundaryData data;
        data.velocity[1] = {ScalarBoundaryKind::Value, 1.0};
        const auto callback = [&data](const PhysicalBoundaryContext&) { return data; };
        const auto result = EvaluatePhysicalBoundary(line, fixture.config, fixture.species, fixture.eos,
            callback, interior, std::span<const double>(kComposition));
        close(result.conserved.mom_v / result.conserved.rho,
            2.0 - interior.mom_v / interior.rho, 1.e-14,
            "1D transverse wall velocity was discarded");
        auto inactive = fixture.config;
        inactive.physics.diffusion.use_viscous_diffusion = false;
        require_rejected([&] {
            (void)EvaluatePhysicalBoundary(line, inactive, fixture.species, fixture.eos,
                callback, interior, std::span<const double>(kComposition));
        }, "inactive viscous closure accepted a wall prescription");
        inactive = fixture.config;
        inactive.physics.diffusion.use_thermal_diffusion = false;
        data.velocity = {};
        data.temperature = {ScalarBoundaryKind::OutwardFlux, 1.};
        require_rejected([&] {
            (void)EvaluatePhysicalBoundary(line, inactive, fixture.species, fixture.eos,
                callback, interior, std::span<const double>(kComposition));
        }, "inactive thermal closure accepted a heat source");
    }

    // Purpose restrictions.
    {
        const auto hydro_coordinates = MakeBoundaryCoordinates(grid, {3.0, 1.0, 1.0},
            BoundaryAxis::X1, BoundarySide::Upper, 0.5, 1, BoundaryPurpose::Hydro, {4.0, 1.0, 1.0});
        PrimitiveData ghost;
        ghost.rho = 0.5;
        ghost.u = 1.0;
        ghost.SetTemperature(1100.0);
        ghost.mass_fractions = kComposition;
        PhysicalBoundaryData hydro_only;
        hydro_only.hydro = ghost;
        require_rejected([&] {
            const auto callback = [&](const PhysicalBoundaryContext&) { return PhysicalBoundaryData{}; };
            (void)EvaluatePhysicalBoundary(hydro_coordinates, fixture.config, fixture.species,
                fixture.eos, callback, interior, std::span<const double>(kComposition));
        }, "Hydro request without a hydro primitive was accepted");
        require_rejected([&] {
            PhysicalBoundaryData mixed = hydro_only;
            mixed.temperature = {ScalarBoundaryKind::Value, 1200.0};
            const auto callback = [&mixed](const PhysicalBoundaryContext&) { return mixed; };
            (void)EvaluatePhysicalBoundary(hydro_coordinates, fixture.config, fixture.species,
                fixture.eos, callback, interior, std::span<const double>(kComposition));
        }, "diffusion channel on a Hydro request was accepted");
        require_rejected([&] {
            PhysicalBoundaryData mixed = hydro_only;
            mixed.species = {{ScalarBoundaryKind::Value, 0.5}, {ScalarBoundaryKind::Value, 0.5}};
            const auto callback = [&mixed](const PhysicalBoundaryContext&) { return mixed; };
            (void)EvaluatePhysicalBoundary(hydro_coordinates, fixture.config, fixture.species,
                fixture.eos, callback, interior, std::span<const double>(kComposition));
        }, "species channel on a Hydro request was accepted");
        // OutwardFlux prescriptions never change a ghost, so they are reported
        // back with the hydro primitive for the manager's direct-flux override.
        require_rejected([&] {
            PhysicalBoundaryData mixed = hydro_only;
            mixed.velocity[0] = {ScalarBoundaryKind::NormalGradient, 10.0};
            const auto callback = [&mixed](const PhysicalBoundaryContext&) { return mixed; };
            (void)EvaluatePhysicalBoundary(hydro_coordinates, fixture.config, fixture.species,
                fixture.eos, callback, interior, std::span<const double>(kComposition));
        }, "velocity gradient channel on a Hydro request was accepted");
        PhysicalBoundaryData flux_only = hydro_only;
        flux_only.temperature = {ScalarBoundaryKind::OutwardFlux, 1.0e9};
        flux_only.velocity[0] = {ScalarBoundaryKind::OutwardFlux, -2.0e6};
        flux_only.species = {{ScalarBoundaryKind::OutwardFlux, 1.0e-8},
                             {ScalarBoundaryKind::OutwardFlux, -1.0e-8}};
        const auto flux_callback = [&flux_only](const PhysicalBoundaryContext&) { return flux_only; };
        const auto flux_evaluation = EvaluatePhysicalBoundary(hydro_coordinates, fixture.config,
            fixture.species, fixture.eos, flux_callback, interior, std::span<const double>(kComposition));
        const auto clean_callback = [&hydro_only](const PhysicalBoundaryContext&) { return hydro_only; };
        const auto clean_evaluation = EvaluatePhysicalBoundary(hydro_coordinates, fixture.config,
            fixture.species, fixture.eos, clean_callback, interior, std::span<const double>(kComposition));
        require_identical(flux_evaluation.conserved, clean_evaluation.conserved,
            "outward flux channels changed the Hydro ghost");
        require(flux_evaluation.mass_fractions == clean_evaluation.mass_fractions,
            "outward flux channels changed the Hydro composition");
        require(flux_evaluation.conditions.temperature.kind == ScalarBoundaryKind::OutwardFlux &&
                flux_evaluation.conditions.temperature.value == 1.0e9 &&
                flux_evaluation.conditions.species.size() == 2,
                "Hydro outward flux channels were not returned for the direct-flux override");
    }
    {
        auto gravity_coordinates = coordinates;
        gravity_coordinates.purpose = BoundaryPurpose::Gravity;
        require_rejected([&] {
            const auto callback = [](const PhysicalBoundaryContext&) { return PhysicalBoundaryData{}; };
            (void)EvaluatePhysicalBoundary(gravity_coordinates, fixture.config, fixture.species,
                fixture.eos, callback, interior, std::span<const double>(kComposition));
        }, "Gravity purpose was routed through the physical evaluator");
        gravity_coordinates.purpose = static_cast<BoundaryPurpose>(9);
        require_rejected([&] {
            const auto callback = [](const PhysicalBoundaryContext&) { return PhysicalBoundaryData{}; };
            (void)EvaluatePhysicalBoundary(gravity_coordinates, fixture.config, fixture.species,
                fixture.eos, callback, interior, std::span<const double>(kComposition));
        }, "unknown purpose was accepted");
    }

    // Direct validation entry point: valid data passes, invalid data is rejected.
    {
        PhysicalBoundaryData valid;
        valid.temperature = {ScalarBoundaryKind::NormalGradient, 10.0};
        valid.species = {{ScalarBoundaryKind::OutwardFlux, 1.0e-9},
                         {ScalarBoundaryKind::OutwardFlux, -1.0e-9}};
        ValidatePhysicalBoundaryData(valid, fixture.config, 2);
        PhysicalBoundaryData invalid = valid;
        invalid.species[1].value = -1.0e-9 + 1.0e-4;
        require_rejected([&] { ValidatePhysicalBoundaryData(invalid, fixture.config, 2); },
            "direct validation accepted unbalanced species fluxes");
        require_rejected([&] { ValidatePhysicalBoundaryData(valid, fixture.config, 3); },
            "direct validation accepted a species count mismatch");
    }

    // Zero-sum species bounds scale with the accumulated magnitude. At tiny
    // physical scales a one-sided (unbalanced) prescription must be rejected
    // while opposite balanced values are accepted; an absolute floor would
    // have silently licensed the one-sided flux. The direct validation entry
    // point and the evaluator path must agree.
    {
        const auto validate_species = [&](const std::vector<ScalarBoundaryCondition>& species) {
            PhysicalBoundaryData data;
            data.species = species;
            ValidatePhysicalBoundaryData(data, fixture.config, 2);
        };
        const auto evaluate_species = [&](const std::vector<ScalarBoundaryCondition>& species) {
            PhysicalBoundaryData data;
            data.species = species;
            const auto callback = [&data](const PhysicalBoundaryContext&) { return data; };
            return EvaluatePhysicalBoundary(coordinates, fixture.config, fixture.species,
                fixture.eos, callback, interior, std::span<const double>(kComposition));
        };

        const std::vector<ScalarBoundaryCondition> one_sided_flux{
            {ScalarBoundaryKind::OutwardFlux, 1.0e-30},
            {ScalarBoundaryKind::OutwardFlux, 0.0}};
        require_rejected([&] { validate_species(one_sided_flux); },
            "direct validation accepted a one-sided 1e-30 outward mass flux");
        require_rejected([&] { (void)evaluate_species(one_sided_flux); },
            "evaluator accepted a one-sided 1e-30 outward mass flux");

        const std::vector<ScalarBoundaryCondition> one_sided_gradient{
            {ScalarBoundaryKind::NormalGradient, 1.0e-30},
            {ScalarBoundaryKind::NormalGradient, 0.0}};
        require_rejected([&] { validate_species(one_sided_gradient); },
            "direct validation accepted a one-sided 1e-30 normal gradient");
        require_rejected([&] { (void)evaluate_species(one_sided_gradient); },
            "evaluator accepted a one-sided 1e-30 normal gradient");

        const std::vector<ScalarBoundaryCondition> balanced_flux{
            {ScalarBoundaryKind::OutwardFlux, 1.0e-30},
            {ScalarBoundaryKind::OutwardFlux, -1.0e-30}};
        validate_species(balanced_flux);
        const auto balanced_evaluation = evaluate_species(balanced_flux);
        require_identical(balanced_evaluation.conserved, interior,
            "balanced tiny outward flux changed the ghost");
        require(balanced_evaluation.mass_fractions == kComposition,
            "balanced tiny outward flux changed the composition");
        require(balanced_evaluation.conditions.species.size() == 2 &&
                balanced_evaluation.conditions.species[0].value == 1.0e-30 &&
                balanced_evaluation.conditions.species[1].value == -1.0e-30,
            "balanced tiny outward flux was not returned for the direct-flux override");

        const std::vector<ScalarBoundaryCondition> balanced_gradient{
            {ScalarBoundaryKind::NormalGradient, 1.0e-30},
            {ScalarBoundaryKind::NormalGradient, -1.0e-30}};
        validate_species(balanced_gradient);
        (void)evaluate_species(balanced_gradient);
    }
}


// Exercise the production handler, rather than only its isolated EOS leaf.
void test_handler_domain_and_stage_time()
{
    Fixture fixture;
    fixture.config.grid.dim = 1;
    fixture.config.grid.x1_min = 0.; fixture.config.grid.x1_max = 16.;
    fixture.config.grid.x1l_boundary_type = fixture.config.grid.x1r_boundary_type = "user";
    int calls = 0;
    ResolvedUserBoundaries callbacks;
    callbacks.physical = [&](const PhysicalBoundaryContext& context) {
        ++calls;
        require(context.time == .375, "handler lost physical stage time");
        PhysicalBoundaryData data;
        if(context.purpose == BoundaryPurpose::Hydro) {
            auto primitive = context.interior;
            primitive.u = context.time + context.native_position[0];
            data.hydro = primitive;
        } else data.temperature = {ScalarBoundaryKind::OutwardFlux, 17.};
        return data;
    };
    ScopedUserBoundarySelection selected(callbacks, fixture.config, fixture.species);
    BCHandler handler(fixture.config); handler.bind(fixture.eos, fixture.species);
    handler.configure_stage(.375, BoundaryPurpose::Hydro);
    const auto hydro_revision = handler.stage_revision();
    handler.configure_stage(.375, BoundaryPurpose::Hydro);
    require(handler.stage_revision() == hydro_revision,
        "identical time/purpose should reuse the boundary context identity");
    auto grid = make_grid(1, "cartesian");
    FluidState state;
    state.Preallocate(grid.GetTotalSize());
    state.InitSpecies(fixture.species.count());
    const auto interior = interior_state(fixture, kComposition);
    for(int cell=0;cell<grid.GetTotalSize();++cell) {
        state.set(cell,interior);
        for(int species=0;species<fixture.species.count();++species) state.X(species,cell)=kComposition[species];
    }
    handler.apply(state, grid);
    for(int depth=1;depth<=grid.ng;++depth) {
        close(state.mom_u[grid.GetIndex(grid.Is()-depth,0,0)] / interior.rho,
            .375, 1e-14, "lower multi-layer user ghost");
        close(state.mom_u[grid.GetIndex(grid.Ie()+depth-1,0,0)] / interior.rho,
            16.375, 1e-14, "upper multi-layer user ghost");
    }
    require(calls==2*grid.ng, "unexpected user ghost count");
    handler.configure_stage(.375, BoundaryPurpose::Diffusion);
    require(handler.stage_revision() != hydro_revision,
        "changing boundary purpose must invalidate a cached context");
    const auto diffusion_revision = handler.stage_revision();
    handler.configure_stage(.5, BoundaryPurpose::Diffusion);
    require(handler.stage_revision() != diffusion_revision,
        "changing only time must invalidate a cached context");
    handler.configure_stage(.375, BoundaryPurpose::Diffusion);
    handler.apply(state,grid);
    require(state.diffusion_boundary && state.diffusion_boundary->faces[0].size()==6,
        "temperature/velocity/species face controls not published");
    const auto view=state.diffusion_boundary->view();
    const auto* controls=view.at(0,grid.Is(),0,0,grid.Is(),grid.Ie(),0,1,0,1,2);
    require(controls && controls[0].value==17., "outward heat-flux controls lost");
    FluidVector flux{0., 2., 3., 4., 30.};
    double species_flux[]{0.,0.};
    ApplyDiffusionBoundaryFlux(controls,-1.,interior,interior,flux,species_flux,2,1);
    const double mechanical=2.*(interior.mom_u/interior.rho)+3.*(interior.mom_v/interior.rho)+4.*(interior.mom_w/interior.rho);
    close(flux.eng,-17.+mechanical,1e-14,"heat-flux override discarded viscous work or outward sign");
    auto internal_grid=Grid(4,4.,12.,0.,2.,0.,2.);
    internal_grid.dim=1;internal_grid.InitializeTopology();
    const int before=calls;
    handler.apply(state,internal_grid);
    require(calls==before,"user callback wrote an internal AMR face");
    // A thermal-only callback leaves the existing reflected normal momentum.
    auto wall_config=fixture.config; wall_config.grid.x1l_boundary_type="reflecting";
    ScopedUserBoundarySelection wall_selected(callbacks,wall_config,fixture.species);
    BCHandler wall(wall_config); wall.bind(fixture.eos,fixture.species);
    wall.configure_stage(.375,BoundaryPurpose::Diffusion); wall.apply(state,grid);
    close(state.mom_u[grid.GetIndex(grid.Is()-1,0,0)],-interior.mom_u,1.e-14,
        "heat-only callback erased a reflecting wall");
    close(state.mom_v[grid.GetIndex(grid.Is()-1,0,0)],interior.mom_v,1.e-14,
        "heat-only callback erased tangential wall state");
}

// Real BCHandler routing for a uniform warm state with zero velocity. For this
// restricted witness has no mixed angular kinetic energy. The actual handler
// now observes center plus eight positive-r points for every full-state ghost;
// axis-negative corners are signed copies after positive axial construction.
// This remains a coordinate/temperature witness, not rotating evolution.
void test_handler_native_rz_coordinates()
{
    constexpr auto rz = GridMetrics::GeometrySemantics::AxisymmetricRz;
    for (const double radial_lower : {0.0, 16.0}) {
        Fixture fixture;
        auto& config = fixture.config;
        config.grid.dim = 2;
        config.grid.geometry = "cylindrical";
        config.grid.x1_min = radial_lower;
        config.grid.x1_max = radial_lower + 16.0;
        config.grid.x2_min = -2.0;
        config.grid.x2_max = 6.0;
        config.grid.x3_min = 0.0;
        config.grid.x3_max = 2.0;
        config.grid.x1l_boundary_type = config.grid.x1r_boundary_type = "user";
        config.grid.x2l_boundary_type = config.grid.x2r_boundary_type = "user";
        Grid grid(4, config.grid.x1_min, config.grid.x1_max,
            config.grid.x2_min, config.grid.x2_max, config.grid.x3_min, config.grid.x3_max);
        grid.dim = 2;
        grid.geometry = "cylindrical";
        grid.InitializeTopology(rz);
        std::array<int, 4> calls{};
        bool negative_tangent_ghost = false;
        ResolvedUserBoundaries callbacks;
        callbacks.physical = [&](const PhysicalBoundaryContext& context) {
            const int axis = static_cast<int>(context.axis);
            const int side = static_cast<int>(context.side);
            require(axis >= 0 && axis < 2 && side >= 0 && side < 2,
                "native RZ callback received an unknown face");
            ++calls[2 * axis + side];
            require(context.purpose == BoundaryPurpose::Hydro && context.time == .375 &&
                context.dimension == 2 && context.ghost_depth >= 1 && context.ghost_depth <= grid.ng,
                "native RZ handler lost stage identity");
            close(context.point.x, context.native_position[0], 1.e-15,
                "native RZ handler face x must be radius");
            close(context.point.y, 0.0, 1.e-15, "native RZ handler face y must be zero");
            close(context.point.z, context.native_position[1], 1.e-15,
                "native RZ handler face z must be axial length");
            close(context.point.r_cy, context.point.x, 1.e-15,
                "native RZ handler face cylindrical radius");
            close(context.point.z_cy, context.point.z, 1.e-15,
                "native RZ handler face cylindrical axial coordinate");
            close(context.point.phi_cy, 0.0, 1.e-15,
                "native RZ handler face azimuth must be zero");
            close(context.ghost_point.y, 0.0, 1.e-15,
                "native RZ handler ghost y must be zero");
            close(context.ghost_point.r_cy, context.ghost_point.x, 1.e-15,
                "native RZ handler preserves signed radial ghost coordinate");
            close(context.ghost_point.z_cy, context.ghost_point.z, 1.e-15,
                "native RZ handler preserves axial ghost coordinate");
            close(context.ghost_point.phi_cy, 0.0, 1.e-15,
                "native RZ handler ghost azimuth must be zero");
            const double sign = side == 0 ? -1.0 : 1.0;
            for (int component = 0; component < 3; ++component)
                close(context.cartesian_normal[component], component == (axis == 0 ? 0 : 2) ? sign : 0.0,
                    1.e-15, "native RZ handler outward normal");
            const double face_coordinate = axis == 0 ? context.point.x : context.point.z;
            const double ghost_coordinate = axis == 0 ? context.ghost_point.x : context.ghost_point.z;
            const double domain_face = axis == 0
                ? (side == 0 ? config.grid.x1_min : config.grid.x1_max)
                : (side == 0 ? config.grid.x2_min : config.grid.x2_max);
            close(face_coordinate, domain_face, 1.e-15, "native RZ handler owns the actual domain face");
            const double spacing = axis == 0 ? grid.dx1 : grid.dx2;
            const double distance = std::abs(ghost_coordinate - face_coordinate);
            close(context.physical_distance, distance, 1.e-14,
                "native RZ sample distance follows its actual ghost/face coordinates");
            const double outward = sign * (ghost_coordinate - face_coordinate);
            require(outward > (context.ghost_depth - 1) * spacing &&
                outward < context.ghost_depth * spacing,
                "native RZ center/Gauss sample left its requested actual ghost layer");
            const int target = side == 0
                ? (axis == 0 ? grid.Is() : grid.Js()) - context.ghost_depth
                : (axis == 0 ? grid.Ie() : grid.Je()) + context.ghost_depth - 1;
            const double cell_lower = axis == 0 ? grid.GetFacePosL(target)
                : grid.x2_min + (target - grid.ng) * grid.dx2;
            const double cell_upper = axis == 0 ? grid.GetFacePosR(target)
                : grid.x2_min + (target - grid.ng + 1) * grid.dx2;
            require(ghost_coordinate > cell_lower && ghost_coordinate < cell_upper,
                "native RZ callback did not sample its actual target cell");
            if (axis == 1 && context.ghost_point.x < 0.0) negative_tangent_ghost = true;
            require(context.ghost_point.x >= 0.0,
                "physical callback must not evaluate negative-r axis corners");
            close(context.interior.temperature, 1000.0, 1.e-14,
                "native RZ warm callback actual EOS temperature");
            close(context.interior.u, 0.0, 1.e-15, "native RZ warm callback radial velocity");
            close(context.interior.v, 0.0, 1.e-15, "native RZ warm callback axial velocity");
            close(context.interior.w, 0.0, 1.e-15, "native RZ warm callback swirl velocity");
            PhysicalBoundaryData data;
            data.hydro = context.interior;
            return data;
        };
        ScopedUserBoundarySelection selected(callbacks, config, fixture.species);
        BCHandler handler(config, rz);
        handler.bind(fixture.eos, fixture.species);
        handler.configure_stage(.375, BoundaryPurpose::Hydro);
        FluidState state;
        state.Preallocate(grid.GetTotalSize());
        state.InitSpecies(fixture.species.count());
        auto primitive = interior_primitive(kComposition);
        primitive.u = primitive.v = primitive.w = 0.0;
        const auto warm = ProblemHelper::detail::InitialConservedState(
            primitive, fixture.eos, config.numerics);
        for (int cell = 0; cell < grid.GetTotalSize(); ++cell) {
            state.set(cell, warm);
            for (int species = 0; species < fixture.species.count(); ++species)
                state.X(species, cell) = kComposition[species];
        }
        handler.apply(state, grid);
        require(calls[1] > 0 && calls[2] > 0 && calls[3] > 0,
            "native RZ handler did not route radial and axial user faces");
        if (radial_lower == 0.0)
            require(calls[0] == 0 && !negative_tangent_ghost,
                "native RZ axis must use signed copies rather than negative-r callbacks");
        else require(calls[0] > 0, "native RZ off-axis lower radial user face was skipped");
        for (const int count : calls)
            require(count % 9 == 0, "full native ghost must visit center and all eight actual samples");
        if (radial_lower == 0.0) {
            for (int j = 0; j < grid.GetTotalY(); ++j)
            for (int i = 0; i < grid.Is(); ++i) {
                const int target = grid.GetIndex(i,j,0);
                const int donor = grid.GetIndex(2*grid.Is()-1-i,j,0);
                const auto actual = state.get(target), positive = state.get(donor);
                const std::array<double,5> fields{actual.rho,actual.mom_u,actual.mom_v,actual.mom_w,actual.eng};
                const std::array<double,5> expected{positive.rho,-positive.mom_u,positive.mom_v,-positive.mom_w,positive.eng};
                for (std::size_t field = 0; field < fields.size(); ++field)
                    require(std::bit_cast<std::uint64_t>(fields[field]) ==
                        std::bit_cast<std::uint64_t>(expected[field]),
                        "warm native axis/corner must copy completed positive-r fields with original parity");
                for (int s = 0; s < fixture.species.count(); ++s)
                    require(std::bit_cast<std::uint64_t>(state.X(s,target)) ==
                        std::bit_cast<std::uint64_t>(state.X(s,donor)),
                        "warm native axis/corner changed copied composition");
            }
        }
        const int total_calls = calls[0] + calls[1] + calls[2] + calls[3];
        auto wrong_chart = grid;
        wrong_chart.geometry = "cartesian";
        require_rejected([&] { handler.apply(state, wrong_chart); },
            "native RZ handler accepted a mismatched actual grid chart");
        require(calls[0] + calls[1] + calls[2] + calls[3] == total_calls,
            "mismatched native RZ chart invoked the user callback");
    }
}

// Exact physical fixture: one material, Cv=2, rho=Omega=1, e0=1/64.
// Native U is generated from independent radial antiderivatives, never from
// the production closure/builder; the actual IdealGas performs every callback
// conversion. These boundary fixtures do not certify a complete Runtime step.
SpeciesManager native_boundary_material()
{
    SpeciesManager material;
    material.add_species("native",1.,1.,1.4,2.);
    return material;
}

FluidVector native_rotating_reference(double lower,double upper)
{
    const long double a=lower,b=upper;
    const long double v=(b*b-a*a)/2.;
    const long double w=(b*b*b-a*a*a)/3.;
    const long double r2=(std::pow(b,4)-std::pow(a,4))/(4.*v);
    const long double rw=(std::pow(b,4)-std::pow(a,4))/(4.*w);
    constexpr long double radial=1.L/16.L,axial=-1.L/32.L,internal=1.L/64.L;
    return {1.,double(a<0.?-radial:radial),double(axial),double(rw),
        double(internal+.5L*(radial*radial+axial*axial+r2))};
}

struct NativeBoundaryFixture {
    SpeciesManager species=native_boundary_material();
    IdealGas eos{1.4,species};
    SimConfig config{};
    Grid grid;
    explicit NativeBoundaryFixture(double radial_lower=0.)
        :grid(4,radial_lower,radial_lower+16.,-2.,6.,0.,2.)
    {
        config.grid.dim=2;config.grid.geometry="cylindrical";
        config.grid.x1_min=radial_lower;config.grid.x1_max=radial_lower+16.;
        config.grid.x2_min=-2.;config.grid.x2_max=6.;
        config.grid.x3_min=0.;config.grid.x3_max=2.;
        config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="user";
        config.grid.x2l_boundary_type=config.grid.x2r_boundary_type="user";
        config.physics.diffusion.use_diffusion=true;
        config.physics.diffusion.use_thermal_diffusion=true;
        config.physics.diffusion.use_viscous_diffusion=true;
        config.physics.diffusion.use_species_diffusion=true;
        grid.dim=2;grid.geometry="cylindrical";
        grid.InitializeTopology(GridMetrics::GeometrySemantics::AxisymmetricRz);
    }
    FluidState cold_state() const
    {
        FluidState state;state.Preallocate(grid.GetTotalSize());state.InitSpecies(1);
        for(int index=0;index<grid.GetTotalSize();++index) {
            // Deliberately nonphysical padding is a storage sentinel, not a
            // valid cell or an available density/composition donor.
            state.set(index,{987.25,123.5,-456.75,321.125,654.625});
            state.X(0,index)=.125;state.enuc_rate[index]=42.+index/8.;
        }
        for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
            const int index=grid.GetIndex(i,j,0);
            state.set(index,native_rotating_reference(grid.GetFacePosL(i),grid.GetFacePosR(i)));
            state.X(0,index)=1.;
        }
        return state;
    }
};

// Fields and composition are compared by bits when the contract is storage
// identity. Scientific state expectations below use independent integrals.
using NativeBoundaryBits=std::array<std::uint64_t,7>;
NativeBoundaryBits native_boundary_bits(const FluidState& state,int index)
{
    const auto u=state.get(index);
    return {std::bit_cast<std::uint64_t>(u.rho),std::bit_cast<std::uint64_t>(u.mom_u),
        std::bit_cast<std::uint64_t>(u.mom_v),std::bit_cast<std::uint64_t>(u.mom_w),
        std::bit_cast<std::uint64_t>(u.eng),std::bit_cast<std::uint64_t>(state.enuc_rate[index]),
        std::bit_cast<std::uint64_t>(state.X(0,index))};
}
std::vector<NativeBoundaryBits> native_boundary_snapshot(const FluidState& state,const Grid& grid)
{
    std::vector<NativeBoundaryBits> bits(static_cast<std::size_t>(grid.GetTotalSize()));
    for(int index=0;index<grid.GetTotalSize();++index)bits[index]=native_boundary_bits(state,index);
    return bits;
}
void require_native_boundary_unchanged(const FluidState& state,const Grid& grid,
    const std::vector<NativeBoundaryBits>& before,std::string_view message)
{
    require(before.size()==static_cast<std::size_t>(grid.GetTotalSize()),"native snapshot layout mismatch");
    for(int index=0;index<grid.GetTotalSize();++index)
        require(native_boundary_bits(state,index)==before[index],message);
}

PhysicalBoundaryData native_rotating_data(const PhysicalBoundaryContext& context)
{
    require(context.ghost_point.r_cy>=0.,"cold physical callback entered negative radius");
    PrimitiveData point;
    point.rho=1.;point.u=1./16.;point.v=-1./32.;point.w=context.ghost_point.r_cy;
    point.SetTemperature(1./128.);point.mass_fractions={1.};
    PhysicalBoundaryData data;data.hydro=point;return data;
}

void test_handler_native_rz_cold_rotation_and_candidate_identity()
{
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    NativeBoundaryFixture fixture;
    auto& grid=fixture.grid;
    std::array<int,4> calls{};
    ResolvedUserBoundaries callbacks;
    callbacks.physical=[&](const PhysicalBoundaryContext& context) {
        ++calls[2*static_cast<int>(context.axis)+static_cast<int>(context.side)];
        require(context.time==.375&&context.purpose==BoundaryPurpose::Hydro,
            "cold native callback lost real stage metadata");
        return native_rotating_data(context);
    };
    ScopedUserBoundarySelection selected(callbacks,fixture.config,fixture.species);
    BCHandler handler(fixture.config,rz);handler.bind(fixture.eos,fixture.species);
    handler.configure_stage(.375,BoundaryPurpose::Hydro);
    auto state=fixture.cold_state();
    const auto original=native_boundary_snapshot(state,grid);
    require(arch::state::recover(state.get(grid.GetIndex(grid.Is(),grid.Js(),0))).status
        ==arch::state::Status::unresolved_energy,
        "cold native witness must distinguish mixed means from a raw point state");
    handler.apply(state,grid);
    require(calls[0]==0&&calls[1]>0&&calls[2]>0&&calls[3]>0,
        "cold native handler lost a real face or invoked axis callback");
    for(const int count:calls)require(count%9==0,"cold full ghost omitted center/eight samples");
    for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
        const int index=grid.GetIndex(i,j,0);
        const auto expected=native_rotating_reference(grid.GetFacePosL(i),grid.GetFacePosR(i));
        const auto actual=state.get(index);
        const std::array<double,5> got{actual.rho,actual.mom_u,actual.mom_v,actual.mom_w,actual.eng};
        const std::array<double,5> reference{expected.rho,expected.mom_u,expected.mom_v,expected.mom_w,expected.eng};
        for(std::size_t field=0;field<got.size();++field)
            require(std::isfinite(got[field])&&std::abs(got[field]-reference[field])<2.e-12,
                "actual cold BCHandler differs from independent native V/W polynomial integrals");
        require(state.X(0,index)==1.,"cold native density-weighted composition changed");
        if(i<grid.Is()) {
            const auto positive=state.get(grid.GetIndex(2*grid.Is()-1-i,j,0));
            require(std::bit_cast<std::uint64_t>(actual.mom_u)==std::bit_cast<std::uint64_t>(-positive.mom_u)
                    &&std::bit_cast<std::uint64_t>(actual.mom_w)==std::bit_cast<std::uint64_t>(-positive.mom_w)
                    &&std::bit_cast<std::uint64_t>(actual.eng)==std::bit_cast<std::uint64_t>(positive.eng),
                "cold native axis/axial corners lost original radial/swirl parity");
        }
    }
    for(int index=0;index<grid.GetTotalSize();++index)
        if(index%grid.stride_y>=grid.GetTotalX())
            require(native_boundary_bits(state,index)==original[index],"native handler wrote or read padded storage");

    // These identity tests prepare from the actual builtin seed. They assert
    // no callback scatter, not rollback of that earlier builtin fill.
    auto seed=fixture.cold_state();handler.apply_builtin(seed,grid);
    const auto seeded=native_boundary_snapshot(seed,grid);
    const auto storage=seed.diffusion_boundary;
    auto candidate=handler.prepare_native(seed,grid);
    require_native_boundary_unchanged(seed,grid,seeded,"native candidate preparation wrote solver fields");
    require(seed.diffusion_boundary==storage,"native candidate preparation published controls");
    BCHandler other(fixture.config,rz);other.bind(fixture.eos,fixture.species);
    other.configure_stage(.375,BoundaryPurpose::Hydro);
    require_rejected([&] {other.validate_native_candidate(candidate,seed,grid);},
        "a different boundary owner accepted another handler's candidate");
    handler.bind(fixture.eos,fixture.species);
    require_rejected([&] {handler.validate_native_candidate(candidate,seed,grid);},
        "EOS rebinding accepted a candidate from an obsolete binding lifetime");
    require_native_boundary_unchanged(seed,grid,seeded,"candidate identity failure scattered fields");
    require(seed.diffusion_boundary==storage,"candidate identity failure published controls");

    int late_calls=0;
    ResolvedUserBoundaries late_callbacks;
    late_callbacks.physical=[&](const PhysicalBoundaryContext& context) {
        auto data=native_rotating_data(context);
        if(++late_calls==38)data.hydro->SetTemperature(std::numeric_limits<double>::quiet_NaN());
        return data;
    };
    ScopedUserBoundarySelection late_selected(late_callbacks,fixture.config,fixture.species);
    BCHandler late(fixture.config,rz);late.bind(fixture.eos,fixture.species);
    late.configure_stage(.375,BoundaryPurpose::Hydro);
    auto failed_seed=fixture.cold_state();late.apply_builtin(failed_seed,grid);
    const auto failed_before=native_boundary_snapshot(failed_seed,grid);
    const auto failed_storage=failed_seed.diffusion_boundary;
    require_rejected([&] {(void)late.prepare_native(failed_seed,grid);},
        "late invalid actual Gauss callback should reject the candidate");
    require(late_calls==38,"late failure did not occur after four completed ghost candidates and the next center");
    require_native_boundary_unchanged(failed_seed,grid,failed_before,
        "late callback failure scattered a partial candidate surface");
    require(failed_seed.diffusion_boundary==failed_storage,"late callback failure published partial face controls");
}

void test_native_rz_builder_inheritance_and_cold_target_rejection()
{
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    NativeBoundaryFixture fixture;
    const auto& grid=fixture.grid;
    auto state=fixture.cold_state();
    const int i=grid.Is()+1,j=grid.Js()-1;
    const int target=grid.GetIndex(i,j,0);
    // Deliberately copy the independent [0,1] native means into [1,2].
    // Target rotation K=49/180; E_rot+thermal=17/64 gives e=-19/2880.
    const auto copied=native_rotating_reference(0.,1.);
    state.set(target,copied);
    const NativeRzBoundaryRequest request{{i,grid.Js()},{i,j},MakeBoundaryCoordinates(grid,
        {grid.GetCellCenterX(i),grid.x2_min,0.},BoundaryAxis::X2,BoundarySide::Lower,.375,1,
        BoundaryPurpose::Diffusion,{grid.GetCellCenterX(i),grid.GetCellCenterY(j),0.},rz)};
    const auto read=[&](int index) {return state.get(index);};
    const auto fraction=[&](int s,int index) {return state.X(s,index);};
    const auto frozen=native_boundary_snapshot(state,grid);
    for(bool flux:{false,true}) {
        int calls=0;
        const auto callback=[&](const PhysicalBoundaryContext& context) {
            ++calls;
            close(context.interior.temperature,1./128.,1.e-14,"cold inherited request must use true source point EOS");
            PhysicalBoundaryData data;
            if(flux)data.temperature={ScalarBoundaryKind::OutwardFlux,17.};
            return data;
        };
        const auto result=EvaluateNativeRzBoundaryCell(grid,request,fixture.config,fixture.species,
            fixture.eos,callback,read,fraction);
        const std::array<double,5> got{result.conserved.rho,result.conserved.mom_u,result.conserved.mom_v,
            result.conserved.mom_w,result.conserved.eng};
        const std::array<double,5> expected{copied.rho,copied.mom_u,copied.mom_v,copied.mom_w,copied.eng};
        for(std::size_t field=0;field<got.size();++field)
            require(std::bit_cast<std::uint64_t>(got[field])==std::bit_cast<std::uint64_t>(expected[field]),
                "None/direct-flux native inheritance changed original U bits");
        require(result.mass_fractions.size()==1&&result.mass_fractions[0]==1.&&calls==1,
            "None/direct-flux native inheritance changed Xi or performed cell quadrature");
        require(result.conditions.temperature.kind==(flux?ScalarBoundaryKind::OutwardFlux:ScalarBoundaryKind::None)
                &&(!flux||result.conditions.temperature.value==17.),
            "face-center direct flux conditions were lost during native inheritance");
    }
    int changing_calls=0;
    const auto changing=[&](const PhysicalBoundaryContext& context) {
        ++changing_calls;
        close(context.interior.temperature,1./128.,1.e-14,
            "invalid copied target must still receive a valid actual source point");
        PhysicalBoundaryData data;data.temperature={ScalarBoundaryKind::Value,1./128.};return data;
    };
    require_rejected([&] {(void)EvaluateNativeRzBoundaryCell(grid,request,fixture.config,fixture.species,
        fixture.eos,changing,read,fraction);},"changing Diffusion silently heated or replaced an invalid inherited cold target");
    require(changing_calls==1,"copied cold target failure must follow the valid center request before any Gauss callback");
    int mixed_calls=0;
    const auto mixed=[&](const PhysicalBoundaryContext& context) {
        ++mixed_calls;
        if(mixed_calls==1)return native_rotating_data(context);
        PhysicalBoundaryData data;data.temperature={ScalarBoundaryKind::OutwardFlux,17.};return data;
    };
    require_rejected([&] {(void)EvaluateNativeRzBoundaryCell(grid,request,fixture.config,fixture.species,
        fixture.eos,mixed,read,fraction);},"native sample hydro/kind presence differed from face-center authority");
    require(mixed_calls==2,"mixed channel schema did not reject the first inconsistent actual Gauss sample");
    int negative_calls=0;
    const int negative_i=grid.Is()-1;
    const NativeRzBoundaryRequest negative{{negative_i,grid.Js()},{negative_i,j},MakeBoundaryCoordinates(grid,
        {grid.GetCellCenterX(negative_i),grid.x2_min,0.},BoundaryAxis::X2,BoundarySide::Lower,.375,1,
        BoundaryPurpose::Diffusion,{grid.GetCellCenterX(negative_i),grid.GetCellCenterY(j),0.},rz)};
    const auto negative_callback=[&](const PhysicalBoundaryContext& context) {
        ++negative_calls;return native_rotating_data(context);
    };
    require_rejected([&] {(void)EvaluateNativeRzBoundaryCell(grid,negative,fixture.config,fixture.species,
        fixture.eos,negative_callback,read,fraction);},"negative-r physical callback request was folded or silently accepted");
    require(negative_calls==0,"negative-r physical request reached the user callback");
    require_native_boundary_unchanged(state,grid,frozen,"pure native builder wrote a field before return/failure");
}

// A separate warm, nonrotating linear density profile makes the x1->x2
// dependency observable: correct completed radial ghosts restore exactly the
// analytic rho(r), including positive outermost tangent corners. This does not
// infer source/stage identities from mock callbacks or qualify evolution.
void test_native_rz_completed_radial_prefix()
{
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    NativeBoundaryFixture fixture(16.);
    const auto& grid=fixture.grid;
    auto state=fixture.cold_state();
    for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
        const long double a=grid.GetFacePosL(i),b=grid.GetFacePosR(i);
        const long double volume=(b*b-a*a)/2.;
        const long double mean_r=(b*b*b-a*a*a)/(3.*volume);
        const double mean_rho=double(1.L+mean_r/64.L);
        state.set(grid.GetIndex(i,j,0),{mean_rho,0.,0.,0.,20.*mean_rho});
    }
    int axial_corner_calls=0;
    ResolvedUserBoundaries callbacks;
    callbacks.physical=[&](const PhysicalBoundaryContext& context) {
        if(context.axis==BoundaryAxis::X2) {
            require(std::abs(context.interior.rho-(1.+context.ghost_point.r_cy/64.))<2.e-12,
                "axial native source did not read the complete analytic radial candidate prefix");
            close(context.interior.temperature,10.,1.e-14,
                "warm radial prefix changed actual source EOS temperature");
            if(context.ghost_point.r_cy<grid.x1_min||context.ghost_point.r_cy>grid.x1_max)
                ++axial_corner_calls;
        }
        PrimitiveData point;point.rho=1.+context.ghost_point.r_cy/64.;
        point.SetTemperature(10.);point.mass_fractions={1.};
        PhysicalBoundaryData data;data.hydro=point;return data;
    };
    ScopedUserBoundarySelection selected(callbacks,fixture.config,fixture.species);
    BCHandler handler(fixture.config,rz);handler.bind(fixture.eos,fixture.species);
    handler.configure_stage(.375,BoundaryPurpose::Hydro);handler.apply(state,grid);
    require(axial_corner_calls>0,"linear-density prefix fixture did not exercise real positive radial/axial corners");
    for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
        const long double a=grid.GetFacePosL(i),b=grid.GetFacePosR(i);
        const long double volume=(b*b-a*a)/2.;
        const double rho=double(1.L+(b*b*b-a*a*a)/(192.L*volume));
        const auto actual=state.get(grid.GetIndex(i,j,0));
        require(std::abs(actual.rho-rho)<2.e-12&&std::abs(actual.eng-20.*rho)<2.e-12
                &&actual.mom_u==0.&&actual.mom_v==0.&&actual.mom_w==0.,
            "linear-density native boundary differs from independent V integrals");
    }
}

/** Independent axis-only executor witness, with already completed axial ghosts.
 * Every real row has a distinct positive-r sentinel; parity is checked with
 * the analytic mirror source i_s=2*Is-1-i_g, never the implementation plan.
 * This qualifies transfer identity and untouched storage, not EOS/handler or
 * whole native RZ stage acceptance. Original scientific tolerances are intact.
 */
void test_native_rz_axis_completion()
{
    BoundaryPlanInput input{};
    input.dimension=2;input.active_extent={4,3,1};input.ghost_depth=2;
    input.faces={BoundaryType::RzAxis,BoundaryType::Outflow,
        BoundaryType::Reflecting,BoundaryType::Outflow,
        BoundaryType::Inactive,BoundaryType::Inactive};
    const auto logical=make_boundary_plan(input);
    // Logical width 8, stride 11, trailing allocation padding: no padding
    // element is a cell or available boundary source/destination.
    const host::HostBoundaryLayout layout{2,4,3,1,2,2,2,0,8,7,1,11,77,81};
    const auto compiled=host::compile(logical,layout);
    FluidState state;state.Preallocate(layout.total_size);state.InitSpecies(2);
    for(int index=0;index<layout.total_size;++index) {
        state.set(index,{1.+index/1024.,.125+index/4096.,
            -.25-index/8192.,.375+index/16384.,10.+index/32.});
        state.enuc_rate[index]=.5+index;
        state.X(0,index)=.25+(index%5)/32.;
        state.X(1,index)=1.-state.X(0,index);
    }
    const auto values=[](const FluidState& actual,int index) {
        const auto u=actual.get(index);
        return std::array<double,8>{u.rho,u.mom_u,u.mom_v,u.mom_w,u.eng,
            actual.enuc_rate[index],actual.X(0,index),actual.X(1,index)};
    };
    const auto snapshot=[&] {
        std::vector<std::array<double,8>> result;
        result.reserve(layout.total_size);
        for(int index=0;index<layout.total_size;++index)result.push_back(values(state,index));
        return result;
    };
    const auto require_bits=[](const std::array<double,8>& actual,
        const std::array<double,8>& expected,std::string_view message) {
        for(std::size_t field=0;field<actual.size();++field)
            require(std::bit_cast<std::uint64_t>(actual[field])==
                std::bit_cast<std::uint64_t>(expected[field]),message);
    };
    const auto original=snapshot();
    host::execute_rz_axis(logical,compiled,state);
    for(int index=0;index<layout.total_size;++index) {
        const int j=index/layout.stride_y,i=index%layout.stride_y;
        if(j<layout.total_y&&i<layout.active_origin_i) {
            const int source=j*layout.stride_y+2*layout.active_origin_i-1-i;
            auto expected=original[source];expected[1]=-expected[1];expected[3]=-expected[3];
            require_bits(values(state,index),expected,"axis-only real-row parity/corner sentinel changed");
        } else require_bits(values(state,index),original[index],
            "axis-only touched positive axial user state, right ghost or padding");
    }
    const auto accepted=snapshot();
    // Every binding failure must reject before the first field write, including
    // metadata-valid source/sign corruption that cannot be caught by range checks.
    const auto reject_unchanged=[&](const BoundaryPlan& owner,const host::HostCompiledBoundaryPlan& plan) {
        require_rejected([&] {host::execute_rz_axis(owner,plan,state);},
            "axis-only accepted a mismatched logical/compiled binding");
        for(int index=0;index<layout.total_size;++index)
            require_bits(values(state,index),accepted[index],"axis-only invalid binding partially wrote state");
    };
    auto corrupt=compiled;corrupt.logical_fingerprint^=1;
    reject_unchanged(logical,corrupt);
    corrupt=compiled;corrupt.transfers.pop_back();reject_unchanged(logical,corrupt);
    corrupt=compiled;corrupt.transfers[0].source_index+=1;reject_unchanged(logical,corrupt);
    corrupt=compiled;corrupt.transfers[0].destination_index+=1;reject_unchanged(logical,corrupt);
    corrupt=compiled;corrupt.transfers[0].conserved_signs[0]=-1;reject_unchanged(logical,corrupt);
    corrupt=compiled;corrupt.transfers[0].species_sign=-1;reject_unchanged(logical,corrupt);
    auto different=input;different.faces[1]=BoundaryType::Reflecting;
    reject_unchanged(make_boundary_plan(different),compiled);
    // No-axis plans are an explicit no-op, with no ordinary physical BC applied.
    auto no_axis=input;no_axis.faces[0]=BoundaryType::Outflow;
    const auto no_axis_logical=make_boundary_plan(no_axis);
    host::execute_rz_axis(no_axis_logical,host::compile(no_axis_logical,layout),state);
    require(rz_axis_corner_operations(no_axis_logical).empty(),"no-axis plan invented RZ corners");
    for(int index=0;index<layout.total_size;++index)
        require_bits(values(state,index),accepted[index],"axis-only no-axis call executed an ordinary BC");
}

/** Independent antiderivatives of physically mirrored cold RZ fields. */
namespace reflecting_cell_checks {
using Polynomial=std::array<long double,5>;
constexpr long double kInternal=1.L/33554432.L;
constexpr long double kRadial=1.L/16.L,kAxial=-1.L/32.L;
constexpr auto kRz=GridMetrics::GeometrySemantics::AxisymmetricRz;

long double power(long double value,int exponent)
{
    long double product=1.L;for(int n=0;n<exponent;++n)product*=value;return product;
}
Polynomial multiply(const Polynomial& left,const Polynomial& right)
{
    Polynomial result{};
    for(int i=0;i<5;++i)for(int j=0;i+j<5;++j)result[i+j]+=left[i]*right[j];
    return result;
}
long double mean(const Polynomial& value,long double lower,long double upper,int weight)
{
    long double integral=0.L;
    for(int n=0;n<5;++n)integral+=value[n]*(power(upper,n+weight+1)-power(lower,n+weight+1))/(n+weight+1);
    return integral/((power(upper,weight+1)-power(lower,weight+1))/(weight+1));
}

/** V/W means of rho=1+beta*r_s^2 and u_phi=r_s, r_s=c+d*r. */
FluidVector reference(double lower,double upper,long double beta,
                      long double c=0.L,long double d=1.L,int direction=-1)
{
    const Polynomial rho{1.L+beta*c*c,2.L*beta*c*d,beta*d*d,0.L,0.L};
    const Polynomial swirl{c,d,0.L,0.L,0.L};
    const auto angular=multiply(rho,swirl);
    auto energy=multiply(rho,multiply(swirl,swirl));
    const long double constant=kInternal+.5L*(kRadial*kRadial+kAxial*kAxial);
    for(int n=0;n<5;++n)energy[n]=.5L*energy[n]+constant*rho[n];
    const long double density=mean(rho,lower,upper,1);
    return {double(density),double((direction==0?-kRadial:kRadial)*density),
        double((direction==1?-kAxial:kAxial)*density),double(mean(angular,lower,upper,2)),
        double(mean(energy,lower,upper,1))};
}

void relative(double actual,double expected,std::string_view message)
{
    const double scale=std::max(std::abs(actual),std::abs(expected));
    require(std::isfinite(actual)&&std::isfinite(expected)
        &&std::abs(actual-expected)<=64.*std::numeric_limits<double>::epsilon()*scale,message);
}

struct Fixture {
    SpeciesManager species=native_boundary_material();
    IdealGas eos{1.4,species};
    SimConfig config{};
    Grid grid{amr::MAX_NG,1.,3.,-.125,.125,0.,1.};
    Fixture() {
        config.grid.dim=2;config.grid.geometry="cylindrical";
        config.grid.x1_min=1.;config.grid.x1_max=3.;
        config.grid.x2_min=-.125;config.grid.x2_max=.125;
        config.grid.x3_min=0.;config.grid.x3_max=1.;
        config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="reflecting";
        config.grid.x2l_boundary_type=config.grid.x2r_boundary_type="reflecting";
        config.numerics.sml_rho=1.e-14;config.numerics.min_eint=0.;
        grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(kRz);
        grid.dyadic_identity.bound=true;
        grid.dyadic_identity.root_lower={1.,-.125};grid.dyadic_identity.root_upper={3.,.125};
        grid.dyadic_identity.root_blocks={1,1};grid.dyadic_identity.level=0;
        grid.dyadic_identity.logical={0,0};grid.dyadic_identity.periodic_axial=false;
        grid.InitializeTopology(kRz);
        (void)GridMetrics::make_geometry_view(grid,kRz);
    }
    FluidState state(long double beta) const {
        FluidState result;result.Preallocate(grid.GetTotalSize());result.InitSpecies(1);
        for(int index=0;index<grid.GetTotalSize();++index) {
            result.set(index,{987.25,123.5,-456.75,321.125,654.625});
            result.enuc_rate[index]=42.+index/8.;result.X(0,index)=.125;
        }
        for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
            const int index=grid.GetIndex(i,j,0);
            result.set(index,reference(grid.GetFacePosL(i),grid.GetFacePosR(i),beta));
            result.X(0,index)=1.;
        }
        return result;
    }
};

/** Bind the actual mirrored donor/depth using the existing public coordinates. */
NativeRzBoundaryRequest request(const Grid& grid,int direction,BoundarySide side,int depth,int tangent)
{
    const bool upper=side==BoundarySide::Upper;
    const int begin=direction==0?grid.Is():grid.Js(),end=direction==0?grid.Ie():grid.Je();
    const int donor=upper?end-depth:begin+depth-1;
    const int ghost=upper?end+depth-1:begin-depth;
    const std::array<int,2> source=direction==0?std::array<int,2>{donor,tangent}:std::array<int,2>{tangent,donor};
    const std::array<int,2> destination=direction==0?std::array<int,2>{ghost,tangent}:std::array<int,2>{tangent,ghost};
    std::array<double,3> face{grid.GetCellCenterX(source[0]),grid.GetCellCenterY(source[1]),0.};
    face[direction]=direction==0?(upper?grid.x1_max:grid.x1_min):(upper?grid.x2_max:grid.x2_min);
    const std::array<double,3> center{grid.GetCellCenterX(destination[0]),grid.GetCellCenterY(destination[1]),0.};
    return {source,destination,MakeBoundaryCoordinates(grid,face,
        direction==0?BoundaryAxis::X1:BoundaryAxis::X2,side,.375,depth,
        BoundaryPurpose::Hydro,center,kRz)};
}

/** Validate the real closure's mean and six physical EOS states, not e_eff=e0. */
void actual_six_point_eos(const FluidState& state,const Grid& grid,int i,int j,
                          const SimConfig& config,const IdealGas& eos)
{
    const auto view=GridMetrics::make_geometry_view(grid,kRz);
    const auto read=[&](int index){return state.get(index);};
    const int support=std::clamp(i-1,0,grid.GetTotalX()-3);
    const arch::state::Bounds bounds{config.numerics.sml_rho,config.numerics.min_eint,config.numerics.max_eint};
    const auto closure=RzThermodynamics::make_cell_supported(read,grid.GetIndex(i,j,0),view,i,support,bounds);
    const double fractions[]{state.X(0,grid.GetIndex(i,j,0))};
    require(closure.valid()&&arch::state::validate_eos(closure.effective_mean,fractions,1,bounds,eos)
        ==arch::state::Status::valid,"projected native mean failed actual IdealGas");
    constexpr std::array<long double,6> fractions_radial{0.L,
        .069431844202973712388026755554L,.330009478207571867598667120449L,
        .669990521792428132401332879551L,.930568155797026287611973244446L,1.L};
    const long double lower=grid.GetFacePosL(i),upper=grid.GetFacePosR(i);
    for(const long double t:fractions_radial) {
        const double radius=double((1.L-t)*lower+t*upper);
        require(arch::state::validate_eos(RzThermodynamics::base_point(closure,radius),fractions,1,bounds,eos)
            ==arch::state::Status::valid,"projected ghost six-point actual IdealGas failed");
    }
}

void check_empty_conditions(const PhysicalBoundaryEvaluation& evaluated)
{
    require(!evaluated.conditions.hydro&&evaluated.conditions.species.empty()
        &&evaluated.conditions.temperature.kind==ScalarBoundaryKind::None
        &&evaluated.conditions.temperature.value==0.,"reflecting scalar leaf fabricated user controls");
    for(const auto& channel:evaluated.conditions.velocity)
        require(channel.kind==ScalarBoundaryKind::None&&channel.value==0.,
            "reflecting scalar leaf fabricated velocity controls");
    require(evaluated.mass_fractions.size()==1,"reflecting scalar leaf omitted full Xi");
    relative(evaluated.mass_fractions[0],1.,"reflecting scalar leaf changed full Xi");
}

void run()
{
    Fixture fixture;const auto& grid=fixture.grid;
    for(long double beta:{0.L,1.L/64.L}) {
        auto original=fixture.state(beta);const auto snapshot=native_boundary_snapshot(original,grid);
        const std::array<const double*,7> addresses{original.rho.data(),original.mom_u.data(),
            original.mom_v.data(),original.mom_w.data(),original.eng.data(),
            original.enuc_rate.data(),original.mass_fractions.data()};
        const auto read=[&](int index){return original.get(index);};
        const auto fraction=[&](int species,int index){return original.X(species,index);};
        for(int direction:{0,1})for(BoundarySide side:{BoundarySide::Lower,BoundarySide::Upper})
        for(int depth=1;depth<=grid.ng;++depth) {
            const int begin=direction==0?grid.Js():grid.Is(),end=direction==0?grid.Je():grid.Ie();
            for(int tangent=begin;tangent<end;++tangent) {
                const auto bound=request(grid,direction,side,depth,tangent);
                actual_six_point_eos(original,grid,bound.source[0],bound.source[1],fixture.config,fixture.eos);
                const auto evaluated=EvaluateNativeRzReflectingCell(grid,bound,fixture.config,
                    fixture.species,fixture.eos,read,fraction);
                check_empty_conditions(evaluated);
                const double wall=side==BoundarySide::Upper?grid.x1_max:grid.x1_min;
                const auto expected=reference(grid.GetFacePosL(bound.destination[0]),
                    grid.GetFacePosR(bound.destination[0]),beta,
                    direction==0?2.L*wall:0.L,direction==0?-1.L:1.L,direction);
                const std::array<double,5> actual{evaluated.conserved.rho,evaluated.conserved.mom_u,
                    evaluated.conserved.mom_v,evaluated.conserved.mom_w,evaluated.conserved.eng};
                const std::array<double,5> wanted{expected.rho,expected.mom_u,expected.mom_v,
                    expected.mom_w,expected.eng};
                for(int field=0;field<5;++field)relative(actual[field],wanted[field],
                    "actual reflecting-cell V/W mean differs from independent mirrored polynomial");
                // rho*Xi is the independent species-mass projection, not an
                // unweighted fraction average; Xi=1 gives its exact rho mean.
                relative(evaluated.conserved.rho*evaluated.mass_fractions[0],expected.rho,
                    "actual reflecting-cell rho*Xi moment differs from independent integral");
                FluidState projected=original;const int destination=grid.GetIndex(
                    bound.destination[0],bound.destination[1],0);
                projected.set(destination,evaluated.conserved);projected.X(0,destination)=evaluated.mass_fractions[0];
                actual_six_point_eos(projected,grid,bound.destination[0],bound.destination[1],fixture.config,fixture.eos);
                require_native_boundary_unchanged(original,grid,snapshot,
                    "read-only reflecting scalar leaf changed original U/X/ENUC/padding bits");
                const std::array<const double*,7> after{original.rho.data(),original.mom_u.data(),
                    original.mom_v.data(),original.mom_w.data(),original.eng.data(),
                    original.enuc_rate.data(),original.mass_fractions.data()};
                require(addresses==after,"read-only reflecting scalar leaf changed source allocation addresses");
            }
        }
        // Keep the direct raw-copy upper-first-ghost counterexample. Its own
        // physical mapping is invalid; genuine projection above remains valid.
        if(beta==0.L) {
            const auto bound=request(grid,0,BoundarySide::Upper,1,grid.Js());
            FluidState copied=original;const int source=grid.GetIndex(bound.source[0],bound.source[1],0);
            const int destination=grid.GetIndex(bound.destination[0],bound.destination[1],0);
            copied.set(destination,original.get(source));
            const auto copied_read=[&](int index){return copied.get(index);};
            const auto view=GridMetrics::make_geometry_view(grid,kRz);
            const auto closure=RzThermodynamics::from_density(copied.get(destination),
                RzDensity::density_cell(copied_read,destination,view,bound.destination[0]),
                {fixture.config.numerics.sml_rho,0.,fixture.config.numerics.max_eint});
            const auto copied_thermal=arch::state::recover(closure.effective_mean);
            require(!closure.valid()&&closure.status==arch::state::Status::unresolved_energy
                &&copied_thermal.status==arch::state::Status::unresolved_energy
                &&std::isfinite(copied_thermal.kinetic)
                &&closure.effective_mean.eng-copied_thermal.kinetic<0.,
                "raw-copy reflecting first ghost lost its actual negative thermal counterexample");
            const long double a=grid.GetFacePosL(bound.source[0]),b=grid.GetFacePosR(bound.source[0]);
            const long double c=grid.GetFacePosL(bound.destination[0]),d=grid.GetFacePosR(bound.destination[0]);
            const long double source_j=(power(b,4)-power(a,4))/4.L/((power(b,3)-power(a,3))/3.L);
            const long double source_r2=(power(b,4)-power(a,4))/4.L/((b*b-a*a)/2.L);
            const long double omega=source_j*((power(d,3)-power(c,3))/3.L)/((power(d,4)-power(c,4))/4.L);
            const long double target_r2=(power(d,4)-power(c,4))/4.L/((d*d-c*c)/2.L);
            const long double copied_internal=kInternal+.5L*source_r2-.5L*omega*omega*target_r2;
            require(copied_internal<0.L,"independent raw-copy effective thermal counterexample is not negative");
        }
        auto bad=request(grid,0,BoundarySide::Lower,1,grid.Js());++bad.source[0];
        require_rejected([&]{(void)EvaluateNativeRzReflectingCell(grid,bad,fixture.config,
            fixture.species,fixture.eos,read,fraction);},"wrong reflecting mirror donor was accepted");
        require_native_boundary_unchanged(original,grid,snapshot,"invalid mirror request wrote source arrays");
    }
    // A distinct authenticated positive root can have a crossing-zero ghost;
    // that target is outside the positive reflecting-cell contract, no folding.
    Grid crossing(amr::MAX_NG,.03125,2.03125,-.125,.125,0.,1.);
    crossing.dim=2;crossing.geometry="cylindrical";crossing.InitializeTopology(kRz);
    crossing.dyadic_identity.bound=true;
    crossing.dyadic_identity.root_lower={.03125,-.125};crossing.dyadic_identity.root_upper={2.03125,.125};
    crossing.dyadic_identity.root_blocks={1,1};crossing.dyadic_identity.level=0;
    crossing.dyadic_identity.logical={0,0};crossing.dyadic_identity.periodic_axial=false;
    crossing.InitializeTopology(kRz);
    // Positive donor cells retain their independently integrated cold state.
    // The crossing target is deliberately an unsupported geometry, not a
    // guessed positive-r extension or a different-root state masquerading as it.
    FluidState invalid;invalid.Preallocate(crossing.GetTotalSize());invalid.InitSpecies(1);
    for(int index=0;index<crossing.GetTotalSize();++index) {
        invalid.set(index,{987.25,123.5,-456.75,321.125,654.625});
        invalid.enuc_rate[index]=42.+index/8.;invalid.X(0,index)=.125;
    }
    for(int j=0;j<crossing.GetTotalY();++j)for(int i=0;i<crossing.GetTotalX();++i) {
        const int index=crossing.GetIndex(i,j,0);
        invalid.set(index,reference(crossing.GetFacePosL(i),crossing.GetFacePosR(i),0.L));
        invalid.X(0,index)=1.;
    }
    const auto before=native_boundary_snapshot(invalid,crossing);
    auto crossing_config=fixture.config;
    crossing_config.grid.x1_min=.03125;crossing_config.grid.x1_max=2.03125;
    const auto crossing_request=request(crossing,0,BoundarySide::Lower,1,crossing.Js());
    const auto invalid_read=[&](int index){return invalid.get(index);};
    const auto invalid_fraction=[&](int species,int index){return invalid.X(species,index);};
    require(crossing.GetFacePosL(crossing_request.destination[0])<0.
        &&crossing.GetFacePosR(crossing_request.destination[0])>0.,"cross-zero negative fixture does not cross zero");
    require_rejected([&]{(void)EvaluateNativeRzReflectingCell(crossing,crossing_request,
        crossing_config,fixture.species,fixture.eos,invalid_read,invalid_fraction);},
        "reflecting scalar leaf folded a crossing-zero target");
    require_native_boundary_unchanged(invalid,crossing,before,"cross-zero request wrote source fields");
    // Separate real selected-EOS representation negative. All Cv=2 physical
    // positives above remain unchanged. Positive finite Cv=1e-320 makes the
    // same e0/Cv temperature overflow double; a nonfinite EOS output must fail.
    SpeciesManager extreme_material;
    extreme_material.add_species("nonfinite-temperature",1.,1.,1.4,1.e-320);
    IdealGas extreme_eos(1.4,extreme_material);
    auto eos_source=fixture.state(0.L);const auto eos_before=native_boundary_snapshot(eos_source,grid);
    const auto eos_read=[&](int index){return eos_source.get(index);};
    const auto eos_fraction=[&](int species,int index){return eos_source.X(species,index);};
    const double xi[]{1.};
    require(std::isfinite(extreme_eos.get_mixture_Cv(xi))&&extreme_eos.get_mixture_Cv(xi)>0.
        &&!std::isfinite(extreme_eos.get_temperature(1.,double(kInternal),xi)),
        "real selected-EOS negative does not produce a nonfinite temperature");
    const auto eos_request=request(grid,0,BoundarySide::Upper,1,grid.Js());
    require_rejected([&]{(void)EvaluateNativeRzReflectingCell(grid,eos_request,fixture.config,
        extreme_material,extreme_eos,eos_read,eos_fraction);},
        "reflecting scalar leaf accepted nonfinite real selected-EOS temperature");
    require_native_boundary_unchanged(eos_source,grid,eos_before,"real EOS rejection wrote source/padding bits");
    std::cout<<"Native reflecting-cell independent V/W checks passed (64 eps; no handler/stage grant)\n";
}
} // namespace reflecting_cell_checks

/** Authentic callback-free Handler preparation/publication and lease negatives. */
namespace reflecting_handler_checks {
using reflecting_cell_checks::kRz;
using reflecting_cell_checks::relative;

std::array<const double*,7> addresses(const FluidState& state)
{
    return {state.rho.data(),state.mom_u.data(),state.mom_v.data(),state.mom_w.data(),
        state.eng.data(),state.enuc_rate.data(),state.mass_fractions.data()};
}

void run()
{
    reflecting_cell_checks::Fixture fixture;
    auto& grid=fixture.grid;
    ResolvedUserBoundaries no_user;
    ScopedUserBoundarySelection selected(no_user,fixture.config,fixture.species);
    require(!no_user.physical,"reflecting Handler witness unexpectedly has a user callback");
    BCHandler handler(fixture.config,kRz);handler.bind(fixture.eos,fixture.species);
    handler.configure_stage(.375,BoundaryPurpose::Hydro);
    auto state=fixture.state(0.L);
    const auto original=native_boundary_snapshot(state,grid);
    const auto original_addresses=addresses(state);
    handler.apply_builtin(state,grid); // Actual logical seed, not a candidate or EOS acceptance.
    const auto seeded=native_boundary_snapshot(state,grid);
    const auto seeded_addresses=addresses(state);
    const auto seeded_storage=state.diffusion_boundary;
    auto candidate=handler.prepare_native(state,grid);
    require_native_boundary_unchanged(state,grid,seeded,"callback-free prepare_native wrote solver fields");
    require(addresses(state)==seeded_addresses&&state.diffusion_boundary==seeded_storage,
        "callback-free prepare_native changed leases or published controls");
    handler.validate_native_candidate(candidate,state,grid);

    // A real Handler with no EOS reflector binding must refuse this seed. It is
    // already complete; refusal must not mutate it or its allocation leases.
    BCHandler unbound(fixture.config,kRz);unbound.configure_stage(.375,BoundaryPurpose::Hydro);
    require_rejected([&]{(void)unbound.prepare_native(state,grid);},
        "callback-free Native reflecting Handler accepted without a real EOS bind");
    require_native_boundary_unchanged(state,grid,seeded,"missing EOS binding wrote callback-free seed");
    require(addresses(state)==seeded_addresses,"missing EOS binding changed seed leases");

    const auto unchanged_seed=[&]() {
        require_native_boundary_unchanged(state,grid,seeded,"candidate rejection wrote seeded solver fields");
        require(state.diffusion_boundary==seeded_storage,"candidate rejection published face controls");
    };
    const auto reject_current=[&](std::string_view message) {
        const auto current_addresses=addresses(state);
        require_rejected([&]{handler.validate_native_candidate(candidate,state,grid);},message);
        unchanged_seed();
        require(addresses(state)==current_addresses,"candidate validation changed the deliberately presented lease");
    };

    // Exact actual root configuration; restore each mutation before the next.
    const auto root=fixture.config.grid;
    for(int fault=0;fault<7;++fault) {
        switch(fault) {
        case 0: fixture.config.grid.x1_min=std::nextafter(root.x1_min,-std::numeric_limits<double>::infinity());break;
        case 1: fixture.config.grid.x1_max=std::nextafter(root.x1_max,std::numeric_limits<double>::infinity());break;
        case 2: fixture.config.grid.x2_min=std::nextafter(root.x2_min,-std::numeric_limits<double>::infinity());break;
        case 3: fixture.config.grid.x2_max=std::nextafter(root.x2_max,std::numeric_limits<double>::infinity());break;
        case 4: fixture.config.grid.nblockx1=root.nblockx1+1;break;
        case 5: fixture.config.grid.nblockx2=root.nblockx2+1;break;
        case 6: fixture.config.grid.x2l_boundary_type=fixture.config.grid.x2r_boundary_type="periodic";break;
        }
        reject_current("Native candidate accepted drift of its actual root configuration");
        fixture.config.grid=root;
        handler.validate_native_candidate(candidate,state,grid);
    }

    const auto stage=handler.snapshot_stage_context();
    handler.configure_stage(.5,BoundaryPurpose::Hydro);
    reject_current("Native candidate accepted changed real stage time");
    handler.restore_stage_context_noexcept(stage);
    handler.validate_native_candidate(candidate,state,grid);
    handler.configure_stage(.375,BoundaryPurpose::Diffusion);
    reject_current("Native candidate accepted changed real operator purpose");
    handler.restore_stage_context_noexcept(stage);
    handler.validate_native_candidate(candidate,state,grid);

    // Hold each original allocation while presenting equal-bit replacement
    // storage, so identity refusal cannot be mistaken for a numerical change.
    std::array<std::vector<double>*,7> vectors{&state.rho,&state.mom_u,&state.mom_v,
        &state.mom_w,&state.eng,&state.enuc_rate,&state.mass_fractions};
    for(int field=0;field<7;++field) {
        std::vector<double> replacement=*vectors[field];
        std::swap(*vectors[field],replacement);
        require(addresses(state)[field]!=seeded_addresses[field],"lease-negative failed to change its real allocation");
        reject_current("Native candidate accepted a foreign real source allocation lease");
        std::swap(*vectors[field],replacement);
        require(addresses(state)==seeded_addresses,"restoring lease-negative lost original allocations");
        handler.validate_native_candidate(candidate,state,grid);
    }

    const auto identity=grid.dyadic_identity;
    for(int fault=0;fault<8;++fault) {
        switch(fault) {
        case 0:grid.dyadic_identity.bound=false;break;
        case 1:grid.dyadic_identity.root_lower[0]=std::nextafter(identity.root_lower[0],-std::numeric_limits<double>::infinity());break;
        case 2:grid.dyadic_identity.root_upper[0]=std::nextafter(identity.root_upper[0],std::numeric_limits<double>::infinity());break;
        case 3:grid.dyadic_identity.root_upper[1]=std::nextafter(identity.root_upper[1],std::numeric_limits<double>::infinity());break;
        case 4:++grid.dyadic_identity.root_blocks[0];break;
        case 5:++grid.dyadic_identity.level;break;
        case 6:++grid.dyadic_identity.logical[0];break;
        case 7:grid.dyadic_identity.periodic_axial=true;break;
        }
        reject_current("Native candidate accepted a changed bound Grid generation identity");
        grid.dyadic_identity=identity;
        handler.validate_native_candidate(candidate,state,grid);
    }
    BCHandler other(fixture.config,kRz);other.bind(fixture.eos,fixture.species);
    other.configure_stage(.375,BoundaryPurpose::Hydro);
    require_rejected([&]{other.validate_native_candidate(candidate,state,grid);},
        "a different actual Handler accepted another owner's reflecting candidate");
    unchanged_seed();require(addresses(state)==seeded_addresses,"foreign Handler changed original leases");

    // All deliberate drift is now restored. Actual validated publication
    // writes ghosts only; radial projection precedes axial projection/corners.
    handler.validate_native_candidate(candidate,state,grid);
    handler.publish_native_noexcept(std::move(candidate),state);
    require(addresses(state)==original_addresses&&state.diffusion_boundary==seeded_storage,
        "reflecting publication changed source allocations or invented controls");
    for(int index=0;index<grid.GetTotalSize();++index) {
        const int i=index%grid.stride_y,j=index/grid.stride_y;
        const bool padding=i>=grid.GetTotalX();
        const bool active=i>=grid.Is()&&i<grid.Ie()&&j>=grid.Js()&&j<grid.Je();
        if(padding||active) {
            require(native_boundary_bits(state,index)==original[index],
                "reflecting candidate publication wrote active cells or padding");
            continue;
        }
        const bool radial=i<grid.Is()||i>=grid.Ie();
        const bool axial=j<grid.Js()||j>=grid.Je();
        const int donor_i=i<grid.Is()?2*grid.Is()-1-i:i>=grid.Ie()?2*grid.Ie()-1-i:i;
        const int donor_j=j<grid.Js()?2*grid.Js()-1-j:j>=grid.Je()?2*grid.Je()-1-j:j;
        const double wall=i<grid.Is()?grid.x1_min:grid.x1_max;
        auto expected=reflecting_cell_checks::reference(grid.GetFacePosL(i),grid.GetFacePosR(i),0.L,
            radial?2.L*wall:0.L,radial?-1.L:1.L,radial?0:1);
        if(radial&&axial)expected.mom_v=-expected.mom_v;
        const auto got=state.get(index);
        const std::array<double,5> actual{got.rho,got.mom_u,got.mom_v,got.mom_w,got.eng};
        const std::array<double,5> wanted{expected.rho,expected.mom_u,expected.mom_v,expected.mom_w,expected.eng};
        for(int component=0;component<5;++component)
            relative(actual[component],wanted[component],"Handler reflecting ghost/corner differs from ordered independent V/W projection");
        relative(state.X(0,index),1.,"Handler reflecting ghost/corner omitted complete Xi");
        const int donor=grid.GetIndex(donor_i,donor_j,0);
        require(std::bit_cast<std::uint64_t>(state.enuc_rate[index])==original[donor][5],
            "reflecting ghost/corner ENUC lost its true ordered prefix donor");
    }
    RzThermodynamics::validate_completed_patch_eos(state,grid,1,
        {fixture.config.numerics.sml_rho,fixture.config.numerics.min_eint,fixture.config.numerics.max_eint},fixture.eos);

    // A real valid physical user law changes only Core root configuration.
    // Grid/array leases stay exact; prepare+validation must refuse publication.
    reflecting_cell_checks::Fixture changing;
    changing.config.grid.x1r_boundary_type="user";
    int callback_calls=0;
    ResolvedUserBoundaries callbacks;
    callbacks.physical=[&](const PhysicalBoundaryContext& context) {
        if(++callback_calls==1)
            changing.config.grid.x1_max=std::nextafter(3.,std::numeric_limits<double>::infinity());
        PrimitiveData physical;physical.rho=1.;physical.u=double(reflecting_cell_checks::kRadial);
        physical.v=double(reflecting_cell_checks::kAxial);physical.w=context.ghost_point.r_cy;
        physical.SetTemperature(double(reflecting_cell_checks::kInternal/2.L));physical.mass_fractions={1.};
        PhysicalBoundaryData data;data.hydro=physical;return data;
    };
    ScopedUserBoundarySelection callback_selected(callbacks,changing.config,changing.species);
    BCHandler changed(changing.config,kRz);changed.bind(changing.eos,changing.species);
    changed.configure_stage(.375,BoundaryPurpose::Hydro);
    auto changed_seed=changing.state(0.L);changed.apply_builtin(changed_seed,changing.grid);
    const auto changed_before=native_boundary_snapshot(changed_seed,changing.grid);
    const auto changed_addresses=addresses(changed_seed);
    const auto changed_identity=changing.grid.dyadic_identity;
    const auto changed_storage=changed_seed.diffusion_boundary;
    require_rejected([&]{auto invalid=changed.prepare_native(changed_seed,changing.grid);
        changed.validate_native_candidate(invalid,changed_seed,changing.grid);},
        "callback root drift passed Native candidate publication gate");
    require(callback_calls>0&&changing.config.grid.x1_max!=3.,"callback-root negative never changed its actual root");
    require(GridMetrics::identity_words(changing.grid.dyadic_identity)==GridMetrics::identity_words(changed_identity)
        &&changing.grid.x1_min==1.&&changing.grid.x1_max==3.
        &&changing.grid.x2_min==-.125&&changing.grid.x2_max==.125
        &&changing.grid.dim==2&&changing.grid.geometry=="cylindrical"
        &&addresses(changed_seed)==changed_addresses&&changed_seed.diffusion_boundary==changed_storage,
        "callback-root refusal changed real Grid identity, leases or controls");
    (void)GridMetrics::make_geometry_view(changing.grid,kRz);
    require_native_boundary_unchanged(changed_seed,changing.grid,changed_before,
        "callback root drift scattered an unaccepted partial boundary candidate");
    std::cout<<"Native reflecting Handler preparation/publication identity checks passed (no face/stage/Runtime grant)\n";
}
} // namespace reflecting_handler_checks

/** Checked wall flags belong to the actual AMR slot/frame, not equal-valued data. */
namespace hydro_wall_authority_checks {
using reflecting_cell_checks::kRz;
using arch::state::ExecutionSide;
using arch::state::StateSlot;
using arch::scheduler::StageBinding;
using arch::scheduler::StageDescriptor;

struct Fixture {
    reflecting_cell_checks::Fixture physical;
    amr::AMRControl control{8,2};
    arch::topology::TopologyIdentityRegistry topology{{2,{1,1,1},0}};
    std::vector<amr::BlockHandle> handles;
    arch::state::StateResidencyLedger ledger{{1}};
    arch::scheduler::MonotonicSchedulerClock clock;
    arch::scheduler::StageExecutionContext context{ExecutionSide::Host,ledger,clock};
    StageBinding binding{context,{}};
    StageDescriptor descriptor=arch::scheduler::supported_hydro_time_plan(
        arch::scheduler::HydroMethod::Euler).stages.front();
    ResolvedUserBoundaries no_user;
    ScopedUserBoundarySelection selected{no_user,physical.config,physical.species};
    BCHandler handler{physical.config,kRz};
    int id=-1;
    amr::BlockHandle published_handle{};

    Fixture() {
        auto& config=physical.config;
        config.grid.nblockx1=1;config.grid.nblockx2=1;config.grid.nblockx3=0;
        config.grid.amr_max_blocks=8;config.amr.lrefinemin=0;config.amr.lrefinemax=0;
        config.numerics.time_integrator="euler";
        control.tree->InitRootGrid(config,1,kRz);
        const auto& active=control.tree->GetActiveBlocks();
        require(active.size()==1,"wall authority fixture did not create one actual Tree leaf");
        id=active.front();auto& b=block();b.RequireNativeGeometryIdentity();
        require(b.grid.dyadic_identity.bound&&b.grid.dyadic_identity.root_lower==std::array<double,2>{1.,-.125}
            &&b.grid.dyadic_identity.root_upper==std::array<double,2>{3.,.125},
            "wall authority fixture lacks its actual bound annulus root");
        const std::vector<arch::topology::TopologyObservation> observations{{id,
            {2,b.level,b.logical_x1,b.logical_x2,b.logical_x3}}};
        auto proposal=topology.stage_adoption(observations);
        const auto adopted=topology.commit_after_success(std::move(proposal),
            [&](const auto& candidate){handles=candidate.handles_in_observation_order;});
        require(handles.size()==1&&handles.front()==topology.handle_for_pool(id)
            &&adopted.epoch==ledger.active_epoch(),"real Tree UID/epoch adoption mismatch");
        published_handle=handles.front();control.BindActiveHandles(handles);binding.handles=handles;
        for(auto* state:{&b.fluid_state,&b.state_next,&b.state_scratch}) {
            for(int index=0;index<b.grid.GetTotalSize();++index) {
                state->set(index,{987.25,123.5,-456.75,321.125,654.625});
                state->enuc_rate[index]=42.+index/8.;state->X(0,index)=.125;
            }
            for(int j=0;j<b.grid.GetTotalY();++j)for(int i=0;i<b.grid.GetTotalX();++i) {
                const int index=b.grid.GetIndex(i,j,0);
                state->set(index,reflecting_cell_checks::reference(
                    b.grid.GetFacePosL(i),b.grid.GetFacePosR(i),0.L));
                state->X(0,index)=1.;
            }
        }
        require(!no_user.physical,"wall authority fixture acquired a user callback");
        handler.bind(physical.eos,physical.species);
        context.step_start_time=.375;context.step_dt=.125;
        context.configure_boundary_context=[this](double time,BoundaryPurpose purpose) {
            handler.configure_stage(time,purpose);
        };
        context.configure_boundary_context(context.step_start_time,BoundaryPurpose::Hydro);
        // The authority requires a real post-boundary consumer, not only a
        // configured BC clock. This selected EOS leaf reads the actual pooled
        // slot and all completed logical ghosts; it never writes or publishes.
        const auto boundary_frame=handler.snapshot_stage_context();
        const auto actual_handles=std::span<const amr::BlockHandle>(handles);
        context.post_boundary_acceptance=[this,boundary_frame,actual_handles](
            const arch::scheduler::StageExecutionContext& actual,StateSlot slot,
            arch::state::StateVersion version) {
            require(&actual==&context&&actual.side==ExecutionSide::Host
                && &actual.ledger==&ledger&& &actual.clock==&clock
                &&actual.step_start_time==.375&&actual.step_dt==.125
                &&handler.stage_context_matches(boundary_frame),
                "actual selected-EOS post-boundary consumer changed its frame");
            const auto live=control.ActiveHandles();
            const auto& active=control.tree->GetActiveBlocks();
            require(live.data()==actual_handles.data()&&live.size()==actual_handles.size()
                &&binding.handles.data()==actual_handles.data()
                &&binding.handles.size()==actual_handles.size()&&active.size()==live.size(),
                "actual selected-EOS consumer changed its borrowed domain");
            for(std::size_t patch=0;patch<active.size();++patch) {
                require(live[patch]==published_handle&&live[patch]==topology.handle_for_pool(active[patch])
                    &&live[patch].epoch==ledger.active_epoch(),
                    "actual selected-EOS consumer changed its true UID/epoch");
                ledger.require_readable({live[patch],slot},{ExecutionSide::Host,version,true,false});
                auto& actual_block=control.pool->GetBlock(active[patch]);
                actual_block.RequireNativeGeometryIdentity();
                const FluidState* input=nullptr;
                switch(slot) {
                case StateSlot::Current:input=&actual_block.fluid_state;break;
                case StateSlot::Next:input=&actual_block.state_next;break;
                case StateSlot::Scratch:input=&actual_block.state_scratch;break;
                default:throw std::logic_error("actual selected-EOS consumer rejected an unknown slot");
                }
                RzThermodynamics::validate_completed_patch_eos(*input,actual_block.grid,1,
                    {physical.config.numerics.sml_rho,physical.config.numerics.min_eint,
                     physical.config.numerics.max_eint},physical.eos);
            }
        };
        // Actual physical ghost construction precedes all real readiness tokens.
        handler.apply_builtin(b.fluid_state,b.grid);
        auto completed=handler.prepare_native(b.fluid_state,b.grid);
        handler.validate_native_candidate(completed,b.fluid_state,b.grid);
        handler.publish_native_noexcept(std::move(completed),b.fluid_state);
        const auto initial=clock.next_publication();
        require(initial.version.value==1,"wall authority initial actual version must be one");
        ledger.register_block(published_handle,initial.version,initial.completion);
        context.post_boundary_acceptance(context,StateSlot::Current,initial.version);
        ledger.publish_ghost({published_handle,StateSlot::Current},ExecutionSide::Host,
            initial.version,clock.next_completion());
        ledger.require_readable({published_handle,StateSlot::Current},
            {ExecutionSide::Host,initial.version,true,true});
    }
    amr::Block& block(){return control.pool->GetBlock(id);}
};

struct Snapshot {
    std::array<std::vector<NativeBoundaryBits>,3> bits;
    std::array<std::array<const double*,7>,3> leases;
    std::array<std::shared_ptr<const DiffusionBoundaryStorage>,3> controls;
    arch::state::SlotCoherence current_coherence;
    std::uint64_t clock_token=0,clock_version=0;
};

Snapshot snapshot(Fixture& f)
{
    Snapshot result;auto& b=f.block();
    const std::array<const FluidState*,3> states{&b.fluid_state,&b.state_next,&b.state_scratch};
    for(int slot=0;slot<3;++slot) {
        result.bits[slot]=native_boundary_snapshot(*states[slot],b.grid);
        result.leases[slot]=reflecting_handler_checks::addresses(*states[slot]);
        result.controls[slot]=states[slot]->diffusion_boundary;
    }
    result.current_coherence=f.ledger.inspect({f.published_handle,StateSlot::Current});
    result.clock_token=f.clock.last_token();result.clock_version=f.clock.last_version();
    return result;
}

void unchanged(Fixture& f,const Snapshot& before)
{
    const auto actual=f.ledger.inspect({f.published_handle,StateSlot::Current});
    const auto same_region=[](const auto& a,const auto& b) {
        return a.residency==b.residency&&a.version==b.version&&a.completion==b.completion
            &&a.pending_transfer==b.pending_transfer;
    };
    require(same_region(actual.interior,before.current_coherence.interior)
        &&same_region(actual.ghost,before.current_coherence.ghost)
        &&actual.ghost_source_version==before.current_coherence.ghost_source_version
        &&f.clock.last_token()==before.clock_token&&f.clock.last_version()==before.clock_version,
        "wall authority rejection changed actual publication or clock metadata");
    auto& b=f.block();const std::array<const FluidState*,3> states{&b.fluid_state,&b.state_next,&b.state_scratch};
    for(int slot=0;slot<3;++slot) {
        require_native_boundary_unchanged(*states[slot],b.grid,before.bits[slot],
            "rejected wall authority wrote a fault-presented slot/ghost/padding");
        require(reflecting_handler_checks::addresses(*states[slot])==before.leases[slot]
            &&states[slot]->diffusion_boundary==before.controls[slot],
            "rejected wall authority changed fault-presented leases/controls");
    }
}

/** Each fault is already present when the read-only authority is asked to act. */
template<class Fault>
void rejects_fault(Fault&& fault,std::string_view message)
{
    Fixture fixture;auto& b=fixture.block();
    HostHydroBoundaryAuthority authority(fixture.handler,fixture.control,fixture.id,
        fixture.binding,fixture.descriptor,b.fluid_state,b.grid);
    fault(fixture);
    const auto presented=snapshot(fixture);
    require_rejected([&]{(void)authority.require_view(&fixture.control,fixture.id,b.fluid_state,b.grid);},message);
    unchanged(fixture,presented);
}

void run()
{
    {
        Fixture fixture;auto& b=fixture.block();
        HostHydroBoundaryAuthority authority(fixture.handler,fixture.control,fixture.id,
            fixture.binding,fixture.descriptor,b.fluid_state,b.grid);
        const auto before=snapshot(fixture);
        const auto view=authority.require_view(&fixture.control,fixture.id,b.fluid_state,b.grid);
        require(view.reflecting==std::array<bool,6>{true,true,true,true,false,false},
            "actual annulus authority failed four real walls or granted an inactive direction");
        for(int direction=0;direction<2;++direction) {
            const int lower=direction==0?b.grid.Is():b.grid.Js();
            const int upper=direction==0?b.grid.Ie():b.grid.Je();int side=99;
            require(view.reflecting_side(direction,lower,lower,upper,side)&&side==0,
                "actual lower reflecting wall flag is missing");
            require(view.reflecting_side(direction,upper,lower,upper,side)&&side==1,
                "actual upper reflecting wall flag is missing");
            require(view.reflecting_side(direction,lower+1,lower,upper,side)&&side==-1,
                "authority granted a reflecting wall at an interior face");
        }
        // r=1 is an actual annulus wall; these flags do not grant a regular origin.
        require(b.grid.GetFacePosL(b.grid.Is())==1.,"annulus authority witness unexpectedly touches the axis");
        unchanged(fixture,before);
        for(int foreign=0;foreign<4;++foreign) {
            amr::AMRControl other_control{8,2};
            other_control.tree->InitRootGrid(fixture.physical.config,1,kRz);
            FluidState equal_state=b.fluid_state;Grid equal_grid=b.grid;
            const auto presented=snapshot(fixture);
            const auto equal_bits=native_boundary_snapshot(equal_state,equal_grid);
            const auto equal_leases=reflecting_handler_checks::addresses(equal_state);
            require_rejected([&]{
                if(foreign==0)(void)authority.require_view(nullptr,fixture.id,b.fluid_state,b.grid);
                if(foreign==1)(void)authority.require_view(&other_control,fixture.id,b.fluid_state,b.grid);
                if(foreign==2)(void)authority.require_view(&fixture.control,fixture.id,equal_state,b.grid);
                if(foreign==3)(void)authority.require_view(&fixture.control,fixture.id,b.fluid_state,equal_grid);
            },"wall authority accepted a null/foreign equal-valued actual owner");
            unchanged(fixture,presented);
            require_native_boundary_unchanged(equal_state,equal_grid,equal_bits,"foreign owner refusal wrote equal-valued foreign state");
            require(reflecting_handler_checks::addresses(equal_state)==equal_leases,"foreign refusal changed foreign leases");
        }
        require_rejected([&]{(void)authority.require_view(&fixture.control,fixture.id+1,b.fluid_state,b.grid);},
            "wall authority accepted a wrong actual block id");
        unchanged(fixture,before);
    }
    rejects_fault([](Fixture& f){f.handler.configure_stage(.5,BoundaryPurpose::Hydro);},"wall authority accepted BC time drift");
    rejects_fault([](Fixture& f){f.handler.configure_stage(.375,BoundaryPurpose::Diffusion);},"wall authority accepted BC purpose drift");
    rejects_fault([](Fixture& f){f.physical.config.grid.x1_max=std::nextafter(3.,4.);},"wall authority accepted root endpoint drift");
    rejects_fault([](Fixture& f){f.physical.config.grid.nblockx1=2;},"wall authority accepted root count drift");
    rejects_fault([](Fixture& f){f.block().grid.dyadic_identity.logical[0]=1;},"wall authority accepted actual bound Grid drift");
    rejects_fault([](Fixture& f){
        const auto changed=f.clock.next_publication();
        f.ledger.publish_interior({f.published_handle,StateSlot::Current},ExecutionSide::Host,
            changed.version,changed.completion);
    },"wall authority accepted newer interior with stale real ghost publication");
    rejects_fault([](Fixture& f){++f.handles.front().epoch.value;},"wall authority accepted borrowed handle epoch drift");
    rejects_fault([](Fixture& f){++f.handles.front().uid.value;},"wall authority accepted borrowed handle UID drift");
    rejects_fault([](Fixture& f){f.context.side=ExecutionSide::Device;},"Host wall authority accepted Device context drift");
    rejects_fault([](Fixture& f){f.context.post_boundary_acceptance={};},"wall authority accepted loss of its genuine selected-EOS consumer");
    rejects_fault([](Fixture& f){f.context.step_start_time=.5;},"wall authority accepted step start clock drift");
    rejects_fault([](Fixture& f){f.context.step_dt=.25;},"wall authority accepted frozen step interval drift");
    rejects_fault([](Fixture& f){std::swap(f.block().fluid_state,f.block().state_next);},
        "wall authority accepted actual Current/Next storage rotation without a new binding");
    // Every source lease is replaced while its old allocation is still alive;
    // equal values therefore cannot accidentally regain the original pointer.
    for(int field=0;field<7;++field) {
        Fixture fixture;auto& b=fixture.block();
        HostHydroBoundaryAuthority authority(fixture.handler,fixture.control,fixture.id,
            fixture.binding,fixture.descriptor,b.fluid_state,b.grid);
        std::array<std::vector<double>*,7> vectors{&b.fluid_state.rho,&b.fluid_state.mom_u,
            &b.fluid_state.mom_v,&b.fluid_state.mom_w,&b.fluid_state.eng,
            &b.fluid_state.enuc_rate,&b.fluid_state.mass_fractions};
        const auto old=reflecting_handler_checks::addresses(b.fluid_state);
        std::vector<double> replacement=*vectors[field];std::swap(replacement,*vectors[field]);
        require(reflecting_handler_checks::addresses(b.fluid_state)[field]!=old[field],
            "authority lease witness did not replace the actual allocation");
        const auto presented=snapshot(fixture);
        require_rejected([&]{(void)authority.require_view(&fixture.control,fixture.id,b.fluid_state,b.grid);},
            "wall authority accepted an equal-bit replacement source lease");
        unchanged(fixture,presented);
    }
    std::cout<<"Native Hydro wall authority real Tree/frame/lease checks passed (no stage/flux/Runtime grant)\n";
}
} // namespace hydro_wall_authority_checks

/** Two real root leaves qualify one stage-wide read-only authority.
 * Workflow: Tree -> registered UID/epoch -> actual native BC/exchange ->
 * selected EOS on each completed pooled slot -> genuine ledger publication ->
 * domain preflight -> indexed patch borrows -> explicit whole-domain join.
 * This is an engineering identity witness, not an executor/Runtime science grant.
 */
namespace hydro_wall_domain_checks {
using reflecting_cell_checks::kRz;
using arch::state::ExecutionSide;
using arch::state::StateSlot;
using arch::scheduler::StageBinding;
using arch::scheduler::StageDescriptor;
using Field=std::vector<double> FluidState::*;
constexpr std::array<Field,7> fields{&FluidState::rho,&FluidState::mom_u,
    &FluidState::mom_v,&FluidState::mom_w,&FluidState::eng,
    &FluidState::enuc_rate,&FluidState::mass_fractions};
constexpr std::array<StateSlot,3> slots{StateSlot::Current,StateSlot::Next,StateSlot::Scratch};

/** Freeze root/BC choices before the immutable selection and handler exist.
 * BCHandler compiles its logical face kinds in its constructor; changing these
 * afterward would not create the outflow boundary that this fixture tests.
 */
struct PhysicalFixture : reflecting_cell_checks::Fixture {
    PhysicalFixture() {
        config.grid.nblockx1=2;config.grid.nblockx2=1;config.grid.nblockx3=0;
        config.grid.amr_max_blocks=8;config.amr.lrefinemin=0;config.amr.lrefinemax=0;
        config.grid.x2r_boundary_type="outflow";
        config.numerics.time_integrator="euler";
    }
};

/** Actual Tree/registry and three completed EOS-backed source slots. */
struct Fixture {
    PhysicalFixture physical;
    amr::AMRControl control{8,2};
    arch::topology::TopologyIdentityRegistry topology{{2,{2,1,1},0}};
    std::vector<amr::BlockHandle> handles,published;
    arch::state::StateResidencyLedger ledger{{1}};
    arch::scheduler::MonotonicSchedulerClock clock;
    arch::scheduler::StageExecutionContext context{ExecutionSide::Host,ledger,clock};
    StageBinding binding{context,{}};
    StageDescriptor descriptor=arch::scheduler::supported_hydro_time_plan(
        arch::scheduler::HydroMethod::Euler).stages.front();
    ResolvedUserBoundaries no_user;
    ScopedUserBoundarySelection selected{no_user,physical.config,physical.species};
    BCHandler handler{physical.config,kRz};
    int eos_patch_checks=0;

    Fixture() {
        auto& config=physical.config;
        require(config.grid.nblockx1==2&&config.grid.nblockx2==1
            &&config.grid.x2r_boundary_type=="outflow",
            "domain fixture configuration was not frozen before handler construction");
        control.tree->InitRootGrid(config,1,kRz);
        const auto& ids=control.tree->GetActiveBlocks();
        require(ids.size()==2,"domain authority fixture lacks two actual Tree roots");
        std::vector<arch::topology::TopologyObservation> observations;
        for(std::size_t n=0;n<ids.size();++n) {
            const int id=ids[n];
            auto& b=control.pool->GetBlock(id);b.RequireNativeGeometryIdentity();
            // Real root Morton order is x1=0 then x1=1, with exact binary
            // annulus endpoints. This independently fixes which block owns
            // each radial wall; the authority's output is not the oracle.
            require(b.active_index==static_cast<int>(n)&&b.level==0
                &&b.logical_x1==n&&b.logical_x2==0&&b.logical_x3==0
                &&b.grid.x1_min==1.+static_cast<double>(n)
                &&b.grid.x1_max==2.+static_cast<double>(n)
                &&b.grid.x2_min==-.125&&b.grid.x2_max==.125,
                "domain fixture actual Tree root ordering/bounds changed");
            require(b.grid.dyadic_identity.bound
                &&b.grid.dyadic_identity.root_blocks==std::array<int,2>{2,1},
                "domain fixture lacks actual two-root Grid provenance");
            observations.push_back({id,{2,b.level,b.logical_x1,b.logical_x2,b.logical_x3}});
            for(auto* input:{&b.fluid_state,&b.state_next,&b.state_scratch}) {
                for(int index=0;index<b.grid.GetTotalSize();++index) {
                    // A real warm constant physical state avoids conflating
                    // the identity test with a cold rotating reconstruction oracle.
                    input->set(index,{1.,0.,0.,0.,10.});
                    input->enuc_rate[index]=42.+id+index/8.;input->X(0,index)=1.;
                }
            }
        }
        auto proposal=topology.stage_adoption(observations);
        const auto adopted=topology.commit_after_success(std::move(proposal),
            [&](const auto& candidate){handles=candidate.handles_in_observation_order;});
        published=handles;
        require(handles.size()==2&&handles[0]!=handles[1]&&adopted.epoch==ledger.active_epoch(),
            "domain fixture did not adopt distinct true UID/epoch handles");
        for(std::size_t n=0;n<ids.size();++n)
            require(handles[n]==topology.handle_for_pool(ids[n]),"domain handle is not registry-owned");
        control.BindActiveHandles(handles);binding.handles=handles;
        require(!no_user.physical,"domain fixture unexpectedly selected a user law");
        handler.bind(physical.eos,physical.species);
        context.step_start_time=.375;context.step_dt=.125;
        context.configure_boundary_context=[this](double time,BoundaryPurpose purpose) {
            handler.configure_stage(time,purpose);
        };
        context.configure_boundary_context(.375,BoundaryPurpose::Hydro);
        const auto frame=handler.snapshot_stage_context();
        const auto actual_handles=std::span<const amr::BlockHandle>(handles);
        context.post_boundary_acceptance=[this,frame,actual_handles](
            const arch::scheduler::StageExecutionContext& actual,StateSlot slot,
            arch::state::StateVersion version) {
            require(&actual==&context&&actual.side==ExecutionSide::Host
                &&&actual.ledger==&ledger&&&actual.clock==&clock
                &&actual.step_start_time==.375&&actual.step_dt==.125
                &&handler.stage_context_matches(frame),"domain selected EOS frame changed");
            const auto live=control.ActiveHandles();const auto& active=control.tree->GetActiveBlocks();
            require(live.data()==actual_handles.data()&&live.size()==actual_handles.size()
                &&binding.handles.data()==actual_handles.data()
                &&binding.handles.size()==actual_handles.size()&&active.size()==live.size(),
                "domain selected EOS borrowed owners changed");
            for(std::size_t n=0;n<active.size();++n) {
                require(live[n]==published[n]&&live[n]==topology.handle_for_pool(active[n])
                    &&live[n].epoch==ledger.active_epoch(),"domain EOS true UID/epoch changed");
                ledger.require_readable({live[n],slot},{ExecutionSide::Host,version,true,false});
                auto& b=control.pool->GetBlock(active[n]);b.RequireNativeGeometryIdentity();
                const auto member=TimeIntegration::hydro_boundary_state_member(slot);
                RzThermodynamics::validate_completed_patch_eos(b.*member,b.grid,1,
                    {physical.config.numerics.sml_rho,physical.config.numerics.min_eint,
                     physical.config.numerics.max_eint},physical.eos);
                ++eos_patch_checks;
            }
        };
        const auto initial=clock.next_publication();
        for(const auto handle:published)ledger.register_block(handle,initial.version,initial.completion);
        for(const auto slot:slots) {
            const auto member=TimeIntegration::hydro_boundary_state_member(slot);
            TimeIntegration::synchronize_domain_boundary(control,handler,member,handles,kRz,
                {config.numerics.sml_rho,config.numerics.min_eint,config.numerics.max_eint});
            const auto ready=slot==StateSlot::Current?initial:clock.next_publication();
            if(slot!=StateSlot::Current)for(const auto handle:published)
                ledger.publish_interior({handle,slot},ExecutionSide::Host,ready.version,ready.completion);
            context.post_boundary_acceptance(context,slot,ready.version);
            const auto completed=clock.next_completion();
            for(const auto handle:published) {
                ledger.publish_ghost({handle,slot},ExecutionSide::Host,ready.version,completed);
                ledger.require_readable({handle,slot},{ExecutionSide::Host,ready.version,true,true});
            }
        }
        require(eos_patch_checks==6,"domain fixture skipped an actual completed slot EOS consumer");
    }
    amr::Block& block(std::size_t n){return control.pool->GetBlock(control.tree->GetActiveBlocks().at(n));}
};

struct SlotSnapshot {
    std::array<std::vector<std::uint64_t>,7> bits;
    std::array<const double*,7> leases{};
    std::shared_ptr<const DiffusionBoundaryStorage> controls;
    std::shared_ptr<BoundaryFluxCaptureStorage> capture;
    arch::state::SlotCoherence coherence;
};
struct Snapshot {
    BCHandler::StageContextSnapshot frame;
    std::array<std::array<SlotSnapshot,3>,2> patches;
    std::array<GridMetrics::DyadicGridIdentity,2> roots;
    std::vector<amr::BlockHandle> handles;
    std::vector<int> ids;
    const amr::BlockHandle* control_span=nullptr;
    const amr::BlockHandle* binding_span=nullptr;
    std::size_t binding_size=0;
    std::uint64_t token=0,version=0;
    int eos_checks=0;
};

/** Snapshot even malformed vectors without dereferencing logical cell indices. */
Snapshot snapshot(Fixture& f) {
    Snapshot result{f.handler.snapshot_stage_context()};
    result.handles.assign(f.handles.begin(),f.handles.end());
    result.ids=f.control.tree->GetActiveBlocks();
    result.control_span=f.control.ActiveHandles().data();result.binding_span=f.binding.handles.data();
    result.binding_size=f.binding.handles.size();
    result.token=f.clock.last_token();result.version=f.clock.last_version();result.eos_checks=f.eos_patch_checks;
    for(std::size_t n=0;n<2;++n) {
        auto& b=f.block(n);result.roots[n]=b.grid.dyadic_identity;
        for(std::size_t s=0;s<slots.size();++s) {
            const auto member=TimeIntegration::hydro_boundary_state_member(slots[s]);
            const auto& state=b.*member;auto& saved=result.patches[n][s];
            for(std::size_t field=0;field<fields.size();++field) {
                const auto& values=state.*fields[field];saved.leases[field]=values.data();
                for(double value:values)saved.bits[field].push_back(std::bit_cast<std::uint64_t>(value));
            }
            saved.controls=state.diffusion_boundary;saved.capture=state.boundary_flux_capture;
            saved.coherence=f.ledger.inspect({f.published[n],slots[s]});
        }
    }
    return result;
}

/** Refusal must preserve all fault-presented inputs, outputs and publication owners. */
void unchanged(Fixture& f,const Snapshot& saved) {
    require(f.handler.stage_context_matches(saved.frame)&&f.handles==saved.handles
        &&f.control.tree->GetActiveBlocks()==saved.ids
        &&f.control.ActiveHandles().data()==saved.control_span
        &&f.binding.handles.data()==saved.binding_span&&f.binding.handles.size()==saved.binding_size
        &&f.clock.last_token()==saved.token&&f.clock.last_version()==saved.version
        &&f.eos_patch_checks==saved.eos_checks,"domain refusal changed its frame/owners/publication");
    const auto same_region=[](const auto& a,const auto& b) {
        return a.residency==b.residency&&a.version==b.version&&a.completion==b.completion
            &&a.pending_transfer==b.pending_transfer;
    };
    for(std::size_t n=0;n<2;++n) {
        auto& b=f.block(n);
        require(GridMetrics::equal_identity(b.grid.dyadic_identity,saved.roots[n]),
            "domain refusal changed fault-presented root provenance");
        for(std::size_t s=0;s<slots.size();++s) {
            const auto member=TimeIntegration::hydro_boundary_state_member(slots[s]);
            const auto& state=b.*member;const auto& before=saved.patches[n][s];
            require(state.diffusion_boundary==before.controls&&state.boundary_flux_capture==before.capture,
                "domain refusal changed boundary controls or capture owner");
            for(std::size_t field=0;field<fields.size();++field) {
                const auto& values=state.*fields[field];
                require(values.data()==before.leases[field]&&values.size()==before.bits[field].size(),
                    "domain refusal changed a presented seven-array lease/extent");
                for(std::size_t index=0;index<values.size();++index)
                    require(std::bit_cast<std::uint64_t>(values[index])==before.bits[field][index],
                        "domain refusal changed source/output/ghost/padding bits");
            }
            const auto actual=f.ledger.inspect({f.published[n],slots[s]});
            require(same_region(actual.interior,before.coherence.interior)
                &&same_region(actual.ghost,before.coherence.ghost)
                &&actual.ghost_source_version==before.coherence.ghost_source_version,
                "domain refusal published or altered actual slot readiness");
        }
    }
}

/** Last-patch preflight faults are present before any domain/worker is granted. */
template<class Fault> void rejects_preflight(Fault&& fault,std::string_view message) {
    Fixture f;fault(f);const auto presented=snapshot(f);
    require_rejected([&]{HostHydroBoundaryDomainAuthority domain(f.handler,f.control,f.binding,f.descriptor);},message);
    unchanged(f,presented);
}

void run() {
    {
        Fixture f;const auto before=snapshot(f);
        HostHydroBoundaryDomainAuthority domain(f.handler,f.control,f.binding,f.descriptor);
        for(std::size_t n=0;n<2;++n) {
            auto& b=f.block(n);
            HostHydroBoundaryAuthority patch(domain,n,b.id,b.fluid_state,b.grid);
            const auto view=patch.require_view(&f.control,b.id,b.fluid_state,b.grid);
            const std::array<bool,6> expected{n==0,n==1,true,false,false,false};
            require(view.reflecting==expected,"domain patch granted an internal/unmarked wall or lost its own wall");
            int side=99;
            const int internal=n==0?b.grid.Ie():b.grid.Is();
            require(view.reflecting_side(0,internal,b.grid.Is(),b.grid.Ie(),side)&&side==-1,
                "domain patch granted a wall on the actual inter-root radial face");
            require(view.reflecting_side(1,b.grid.Je(),b.grid.Js(),b.grid.Je(),side)&&side==-1,
                "domain patch granted the actual configured outflow boundary a wall");
            require_rejected([&]{HostHydroBoundaryAuthority wrong(domain,1-n,b.id,b.fluid_state,b.grid);},
                "domain patch accepted another true loop index for the same pool input");
        }
        domain.require_complete_domain();unchanged(f,before);
    }
    rejects_preflight([](Fixture& f){const auto next=f.clock.next_publication();
        f.ledger.publish_interior({f.published.back(),StateSlot::Current},ExecutionSide::Host,
            next.version,next.completion);},"domain preflight accepted last-patch stale ghosts");
    rejects_preflight([](Fixture& f){++f.handles.back().uid.value;},
        "domain preflight accepted a last-patch foreign UID");
    rejects_preflight([](Fixture& f){++f.handles.back().epoch.value;},
        "domain preflight accepted a last-patch foreign epoch");
    rejects_preflight([](Fixture& f){f.block(1).grid.dyadic_identity.root_upper[0]=std::nextafter(3.,4.);},
        "domain preflight accepted last-patch root provenance drift");
    for(const auto field:fields)rejects_preflight([field](Fixture& f){(f.block(1).fluid_state.*field).pop_back();},
        "domain preflight accepted a malformed last-patch source array");
    // A synchronous unrelated-patch fault cannot be seen by an O(1) borrow of
    // patch zero; the mandatory joined domain gate must reject it before publication.
    for(int field=0;field<7;++field) {
        Fixture f;auto& first=f.block(0);auto& last=f.block(1);
        HostHydroBoundaryDomainAuthority domain(f.handler,f.control,f.binding,f.descriptor);
        HostHydroBoundaryAuthority patch(domain,0,first.id,first.fluid_state,first.grid);
        auto& values=last.fluid_state.*fields[field];std::vector<double> old=values;
        std::swap(old,values);require(old.data()!=values.data(),"domain joined lease witness did not replace storage");
        const auto presented=snapshot(f);
        (void)patch.require_view(&f.control,first.id,first.fluid_state,first.grid);
        require_rejected([&]{HostHydroBoundaryAuthority stale(domain,1,last.id,last.fluid_state,last.grid);},
            "domain last-patch entry accepted a captured equal-bit replacement lease");
        require_rejected([&]{domain.require_complete_domain();},
            "domain join accepted an unrelated equal-bit replacement lease");
        unchanged(f,presented);
    }
    for(int identity=0;identity<2;++identity) {
        Fixture f;auto& first=f.block(0);
        HostHydroBoundaryDomainAuthority domain(f.handler,f.control,f.binding,f.descriptor);
        HostHydroBoundaryAuthority patch(domain,0,first.id,first.fluid_state,first.grid);
        if(identity==0)++f.handles.back().uid.value;else ++f.handles.back().epoch.value;
        const auto presented=snapshot(f);
        (void)patch.require_view(&f.control,first.id,first.fluid_state,first.grid);
        require_rejected([&]{domain.require_complete_domain();},
            "domain join accepted an unrelated captured handle identity change");
        unchanged(f,presented);
    }
    std::cout<<"Native Hydro two-root domain preflight/index/join checks passed (local EOS identity; no Runtime grant)\n";
}
} // namespace hydro_wall_domain_checks

/** A real cold active donor cannot borrow provisional raw-copy ghost density.
 * Real Tree/root and registry handles execute the actual builtin -> exchange ->
 * immutable callback -> publication -> exchange sequence. The full selected
 * EOS subsequently checks all logical native cells; no Runtime/step grant is
 * inferred. The independent reference uses polynomial antiderivatives.
 */
void test_native_rz_cold_seed_density_support()
{
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    constexpr double e0=0x1p-25;
    SimConfig config;config.grid.dim=2;config.grid.geometry="cylindrical";
    config.grid.nblockx1=config.grid.nblockx2=1;config.grid.nblockx3=0;
    config.grid.x1_min=1.;config.grid.x1_max=3.;config.grid.x2_min=0.;config.grid.x2_max=2.;
    config.grid.amr_max_blocks=4;config.amr.lrefinemin=config.amr.lrefinemax=0;
    config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="user";
    config.grid.x2l_boundary_type=config.grid.x2r_boundary_type="periodic";
    config.numerics.sml_rho=config.numerics.min_eint=1.e-14;config.numerics.max_eint=1.e10;
    SpeciesManager material;material.add_species("cold-density",1.,1.,1.4,3.);
    IdealGas eos(1.4,material);amr::AMRControl control(4,2);
    control.tree->InitRootGrid(config,1,rz);
    const auto& active=control.tree->GetActiveBlocks();
    require(active.size()==1,"cold seed fixture requires a genuine one-root domain");
    auto& block=control.pool->GetBlock(active.front());const auto& grid=block.grid;
    block.RequireNativeGeometryIdentity();
    require(grid.dyadic_identity.bound&&grid.dyadic_identity.periodic_axial
        &&grid.ng==amr::MAX_NG&&grid.Ie()-grid.Is()==16
        &&grid.dx1==.125&&grid.dx2==.125,
        "cold seed fixture lost authentic root/periodic/dyadic geometry");
    const auto integral=[](long double l,long double h,int p) {
        return (std::pow(h,p+1)-std::pow(l,p+1))/static_cast<long double>(p+1);
    };
    const auto independent=[&](int i) {
        const long double l=grid.GetFacePosL(i),h=grid.GetFacePosR(i);
        const auto v=integral(l,h,1),w=integral(l,h,2);
        const auto m=.875L*v+.25L*integral(l,h,3);
        const auto inertia=.875L*integral(l,h,3)+.25L*integral(l,h,5);
        return FluidVector{double(m/v),0.,0.,double(inertia/w),double(e0*m/v+.5L*inertia/v)};
    };
    auto& state=block.fluid_state;
    for(int c=0;c<grid.GetTotalSize();++c) {
        state.set(c,{987.,123.,-456.,321.,654.});state.X(0,c)=.125;state.enuc_rate[c]=42.+c/8.;
    }
    for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
        const int c=grid.GetIndex(i,j,0);state.set(c,independent(i));state.X(0,c)=1.;
    }
    const auto initialized=native_boundary_snapshot(state,grid);
    arch::topology::TopologyIdentityRegistry registry{{2,{1,1,1},0}};
    auto proposal=registry.stage_adoption({{block.id,{2,block.level,
        block.logical_x1,block.logical_x2,block.logical_x3}}});
    std::vector<amr::BlockHandle> handles;
    (void)registry.commit_after_success(std::move(proposal),
        [&](const auto& candidate){handles=candidate.handles_in_observation_order;});
    require(handles.size()==1&&handles.front()==registry.handle_for_pool(block.id),
        "cold seed fixture lacks an authentic registered handle");
    control.BindActiveHandles(handles);
    int lower_calls=0,upper_calls=0;
    ResolvedUserBoundaries callbacks;callbacks.identity="native-cold-active-density-support";
    callbacks.physical=[&](const PhysicalBoundaryContext& c) {
        require(c.axis==BoundaryAxis::X1&&c.time==.375&&c.purpose==BoundaryPurpose::Hydro,
            "cold seed callback lost actual face/stage purpose/time");
        if(c.side==BoundarySide::Lower)++lower_calls;else ++upper_calls;
        const double donor_r=2.*(c.side==BoundarySide::Lower?1.:3.)-c.ghost_point.r_cy;
        close(c.interior.rho,.875+.25*donor_r*donor_r,2.e-12,
            "cold seed callback did not get a genuine positive physical donor");
        close(c.interior.w,donor_r,2.e-12,"cold seed callback source angular physical point changed");
        close(c.interior.temperature,e0/3.,2.e-12,"cold seed source lost genuine positive EOS thermal input");
        PrimitiveData point;const double r=c.ghost_point.r_cy;
        point.rho=.875+.25*r*r;point.w=r;point.SetTemperature(e0/3.);point.mass_fractions={1.};
        PhysicalBoundaryData data;data.hydro=point;return data;
    };
    ScopedUserBoundarySelection selected(callbacks,config,material);
    BCHandler handler(config,rz);handler.bind(eos,material);handler.configure_stage(.375,BoundaryPurpose::Hydro);
    handler.apply_builtin(state,grid);
    const int first=grid.GetIndex(grid.Is(),grid.Js(),0);
    const int seed_ghost=grid.GetIndex(grid.Is()-1,grid.Js(),0);
    require(state.rho[seed_ghost]==state.rho[first]
        &&state.rho[seed_ghost]!=independent(grid.Is()-1).rho,
        "actual seed did not overwrite the analytic density halo in this counterexample");
    const auto read=[&](int c){return state.get(c);};
    const auto view=GridMetrics::make_geometry_view(grid,rz);
    const arch::state::Bounds bounds{config.numerics.sml_rho,
        config.numerics.min_eint,config.numerics.max_eint};
    const auto stale=RzThermodynamics::make_cell_supported(read,first,view,grid.Is(),grid.Is()-1,bounds);
    require(stale.inertia_mapping_valid&&!stale.valid()&&stale.status==arch::state::Status::unresolved_energy,
        "raw seed-density support did not distinguish the false cold thermal rejection");
    const auto seeded=native_boundary_snapshot(state,grid);
    auto candidate=handler.prepare_native(state,grid);
    require_native_boundary_unchanged(state,grid,seeded,"cold read-only boundary preparation wrote solver arrays");
    handler.validate_native_candidate(candidate,state,grid);
    require(lower_calls>0&&upper_calls>0&&lower_calls%9==0&&upper_calls%9==0,
        "cold physical source did not reach center/eight-node selected EOS callbacks");
    // Actual whole-domain re-preparation, including the two real exchanges,
    // does not use manually copied ghost/reference data.
    TimeIntegration::synchronize_domain_boundary(control,handler,&amr::Block::fluid_state,
        handles,rz,bounds);
    RzThermodynamics::validate_completed_patch_eos(state,grid,1,bounds,eos);
    for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
        const int c=grid.GetIndex(i,j,0);const auto expected=independent(i),actual=state.get(c);
        if(i>=grid.Is()&&i<grid.Ie()&&j>=grid.Js()&&j<grid.Je())
            require(native_boundary_bits(state,c)==initialized[static_cast<std::size_t>(c)],
                "cold boundary phasing changed active interior/ENUC/species bits");
        close(actual.rho,expected.rho,2.e-12,"cold completed boundary density V mean reference");
        close(actual.mom_w,expected.mom_w,2.e-12,"cold completed boundary J/W reference");
        close(actual.eng,expected.eng,2.e-12,"cold completed boundary E/V reference");
        require(actual.mom_u==0.&&actual.mom_v==0.&&state.X(0,c)==1.,
            "cold completed boundary changed zero meridional motion or composition");
    }
    for(int j=0;j<grid.GetTotalY();++j)for(int i=grid.GetTotalX();i<grid.stride_y;++i) {
        const int c=grid.GetIndex(i,j,0);
        require(native_boundary_bits(state,c)==initialized[static_cast<std::size_t>(c)],
            "cold boundary phasing touched storage padding");
    }
    // Paired periodic coordinates are physical aliases, so corner validation
    // above covers all real rows without treating alias z as an extended domain.
    std::cout<<"Native cold seeded-density donor support checked through actual phased BC and completed EOS (local boundary; no Runtime step grant)\n";
}

} // namespace

void test_user_physical_boundary()
{
    test_native_rz_cold_seed_density_support();
    reflecting_cell_checks::run();
    reflecting_handler_checks::run();
    hydro_wall_authority_checks::run();
    hydro_wall_domain_checks::run();
    test_native_rz_axis_completion();
    test_handler_domain_and_stage_time();
    const Fixture fixture;
    test_geometry_normals();
    test_coordinate_dimensions();
    test_native_rz_coordinates();
    test_handler_native_rz_coordinates();
    test_handler_native_rz_cold_rotation_and_candidate_identity();
    test_native_rz_builder_inheritance_and_cold_target_rejection();
    test_native_rz_completed_radial_prefix();
    test_interior_snapshot(fixture);
    test_hydro_temperature_state(fixture);
    test_invalid_hydro_requests(fixture);
    test_diffusion_ghost_rules(fixture);
    test_channel_misuse(fixture);
    std::cout << "user physical boundary contract passed\n";
}
