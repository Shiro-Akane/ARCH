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
 */
#include "physics/boundary/PhysicalBoundary.h"
#include "physics/boundary/PhysicalBoundaryHandler.h"
#include "numerics/integrator/TimeIntegratorHelper.h"
#include "physics/eos/IdealGas.h"

#include <array>
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

} // namespace

void test_user_physical_boundary()
{
    test_handler_domain_and_stage_time();
    const Fixture fixture;
    test_geometry_normals();
    test_coordinate_dimensions();
    test_interior_snapshot(fixture);
    test_hydro_temperature_state(fixture);
    test_invalid_hydro_requests(fixture);
    test_diffusion_ghost_rules(fixture);
    test_channel_misuse(fixture);
    std::cout << "user physical boundary contract passed\n";
}
