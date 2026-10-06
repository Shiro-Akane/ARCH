/**
 * @file PhysicalBoundary.h
 * @brief Shared EOS-backed evaluation of physical boundary faces and ghosts.
 *
 * Workflow:
 * 1. The manager loops native/AMR faces and ghost depths and builds
 *    BoundaryCoordinates with MakeBoundaryCoordinates: the face and ghost
 *    native positions are mapped through Grid::PhysicalCoordsFromNative, the
 *    outward Cartesian normal and the face-to-ghost normal distance come from
 *    the GridMetrics authority, and the boundary time is staged verbatim.
 *    An explicit native RZ chart uses the same Grid coordinate expansion and
 *    shared r/z length metrics. This coordinate extension does not convert
 *    mixed native V/W moments into callback thermodynamic point states.
 * 2. BoundaryInteriorPrimitive copies the conserved interior cell into a
 *    PrimitiveData snapshot (rho, native velocity, pressure, temperature and
 *    the complete mass-fraction vector) supplied by the selected EOS.
 * 3. EvaluatePhysicalBoundary calls the read-only user callback with that
 *    snapshot, then ResolvePhysicalBoundaryData validates the returned data,
 *    resolves the purpose-specific point ghost primitive and converts it back
 *    through the shared InitialStateConversion authority. Native adapters may
 *    supply actual EOS-validated point snapshots to the same resolver; native
 *    V/W sampling and final axis parity remain outside this point-law owner.
 * 4. The manager publishes the returned conserved state and conditions. CUDA
 *    kernels, direct-flux overrides and any block/state mutation stay outside
 *    this shared Host leaf; nothing here writes grid or solver storage.
 *
 * Science notes:
 * - A Hydro request must return a complete hydro primitive. Diffusion channels
 *   never change it: Value/NormalGradient channels are rejected as misuse,
 *   while OutwardFlux values are only reported back in the conditions.
 * - A Diffusion request inherits the completed builtin ghost, preserving wall
 *   reflection and corner ownership for unspecified channels. An explicitly
 *   returned primitive replaces that base. Scalar Value prescriptions use
 *   the face-centered ghost rule W_ghost = 2*W_face - W_interior;
 *   NormalGradient uses W_ghost = W_interior + 2*d*g_out with d the
 *   face-to-ghost metric distance (the face is midway between the interior and
 *   ghost cell centers); OutwardFlux leaves the ghost at its base value and is
 *   reported in the returned conditions for the manager's direct-flux
 *   override, so no heat flux is approximated by a conductivity evaluated at
 *   the wrong temperature.
 * - Composition sums are checked against scaled FP64 accumulation bounds (the
 *   same factor family InitialStateConversion uses) and prescriptions are never
 *   renormalized or floored here. A simplex mass-fraction vector uses the
 *   fixed unit target bound 512*n*eps*max(1, sum|x_i|); a zero-sum normal
 *   gradient or outward flux uses the magnitude-scaled bound
 *   512*n*eps*sum|x_i| without the 1.0 floor, so a one-sided flux is rejected
 *   at any physical scale.
 */

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/problem/InitialStateConversion.h"
#include "data/FluidState.h"
#include "data/GlobalDefs.h"
#include "data/UserTypes.h"
#include "grid/Grid.h"
#include "grid/GridMetrics.h"
#include "numerics/state/StateAdmissibility.h"
#include "physics/boundary/BoundaryTypes.h"
#include "physics/species/Species.h"

namespace arch::boundary {

/**
 * Resolved ghost state plus the validated request data that produced it.
 * The manager applies `conserved`/`mass_fractions`; `conditions` carries the
 * scalar prescriptions (including OutwardFlux values) for direct overrides.
 */
struct PhysicalBoundaryEvaluation {
    FluidVector conserved{};
    std::vector<double> mass_fractions;
    PhysicalBoundaryData conditions;
};

namespace detail {

// Documented scaled FP64 accumulation bound for composition sums. This is the
// factor family used by InitialStateConversion/StateAdmissibility.
//
// Dimensional scaling: composition sums are dimensionful quantities. For a
// simplex (mass-fraction) target of exactly one the compared magnitude is
// O(1), so flooring the scale at 1 keeps the bound a fixed O(eps) absolute
// window that matches the O(1) target. A zero-sum species prescription (normal
// gradient or outward flux) instead cancels to zero at arbitrary physical
// scale, so the same 1.0 floor would license a spurious O(1e-13) *absolute*
// outward flux. The zero-sum bound is therefore scaled only by the accumulated
// magnitude sum(abs(values)); it stays a fixed O(eps) *relative* window that
// rejects one-sided fluxes at any scale while accepting opposite balanced
// values whose cancellation is limited by FP64 accumulation.
constexpr double kCompositionAccumulationFactor = 512.0;

// Simplex composition bound: 512*n*eps*max(1, sum(abs(values))). The 1.0 floor
// matches the unit target of a mass-fraction vector.
inline double AccumulationTolerance(std::size_t count, double magnitude)
{
    return kCompositionAccumulationFactor * static_cast<double>(count)
         * std::numeric_limits<double>::epsilon() * std::max(1.0, magnitude);
}

// Zero-sum composition bound: 512*n*eps*sum(abs(values)), with no 1.0 floor so
// the tolerance scales with the physical magnitude of the accumulated fluxes.
inline double ZeroSumTolerance(std::size_t count, double magnitude)
{
    return kCompositionAccumulationFactor * static_cast<double>(count)
         * std::numeric_limits<double>::epsilon() * magnitude;
}

inline void RequireFinite(double value, const char* message)
{
    if (!std::isfinite(value)) throw std::invalid_argument(message);
}

// Validate one mass-fraction vector against the registered species count.
// simplex=true requires non-negative fractions summing to one (a primitive
// composition) within the fixed O(eps) simplex bound; simplex=false requires
// the values to sum to zero (a normal gradient or outward flux) within the
// magnitude-scaled zero-sum bound 512*n*eps*sum(abs(values)). No value is
// modified.
inline void RequireMassFractions(std::span<const double> fractions, int species_count,
                                 bool simplex, const char* label)
{
    if (static_cast<int>(fractions.size()) != species_count)
        throw std::invalid_argument(std::string(label)
            + " must supply one mass fraction per registered species");
    if (species_count == 0) return;
    double sum = 0.0;
    double magnitude = 0.0;
    for (const double value : fractions) {
        if (!std::isfinite(value))
            throw std::invalid_argument(std::string(label) + " mass fractions must be finite");
        if (simplex && !(value >= 0.0))
            throw std::invalid_argument(std::string(label) + " mass fractions must be non-negative");
        sum += value;
        magnitude += std::abs(value);
    }
    const double target = simplex ? 1.0 : 0.0;
    const double tolerance = simplex
        ? AccumulationTolerance(fractions.size(), magnitude)
        : ZeroSumTolerance(fractions.size(), magnitude);
    if (std::abs(sum - target) > tolerance)
        throw std::invalid_argument(std::string(label) + (simplex
            ? " mass fractions must sum to one"
            : " normal gradients and outward fluxes must sum to zero across species"));
}

inline void ValidateScalarCondition(const ScalarBoundaryCondition& condition, const char* label)
{
    const unsigned raw = static_cast<unsigned>(condition.kind);
    if (raw > static_cast<unsigned>(ScalarBoundaryKind::OutwardFlux))
        throw std::invalid_argument(std::string(label) + " uses an unknown scalar boundary kind");
    if (!std::isfinite(condition.value))
        throw std::invalid_argument(std::string(label) + " boundary value must be finite");
}

inline bool HasPrescription(const ScalarBoundaryCondition& condition)
{
    return condition.kind != ScalarBoundaryKind::None;
}

// Ghost rule shared by temperature, native velocity and species channels.
// `physical_distance` is the face-to-ghost normal distance, so the interior
// cell center is one distance inside the face and the ghost center one
// distance outside it.
inline double ApplyScalarPrescription(const ScalarBoundaryCondition& condition,
                                      double interior_value, double physical_distance)
{
    switch (condition.kind) {
    case ScalarBoundaryKind::None:
        return interior_value;
    case ScalarBoundaryKind::Value:
        return 2.0 * condition.value - interior_value;
    case ScalarBoundaryKind::NormalGradient:
        return interior_value + 2.0 * physical_distance * condition.value;
    case ScalarBoundaryKind::OutwardFlux:
        // The manager applies the reported outward flux directly; the ghost
        // stays at its base value.
        return interior_value;
    }
    return interior_value;
}

// Convert one resolved boundary primitive to conserved variables through the
// existing InitialStateConversion authority, reject any repair (never hide a
// clamp), and require a strictly finite state inside the selected EOS domain.
template <class Eos>
FluidVector BuildBoundaryConserved(const PrimitiveData& primitive, const Eos& eos,
                                   const SimConfig& config, const char* label)
{
    arch::state::Repair report{};
    const FluidVector state = ProblemHelper::detail::InitialConservedState(
        primitive, eos, config.numerics, &report);
    if (report.status == arch::state::Status::repaired)
        throw std::runtime_error(std::string(label)
            + " required a conserved-state repair; physical boundary requests are rejected instead of clamped");
    for (const double component : {state.rho, state.mom_u, state.mom_v, state.mom_w, state.eng})
        if (!std::isfinite(component))
            throw std::runtime_error(std::string(label) + " produced a non-finite conserved state");
    const auto kinematics = arch::state::recover(state);
    if (kinematics.status != arch::state::Status::valid)
        throw std::runtime_error(std::string(label) + " produced an inadmissible conserved state");
    const double* xi = primitive.mass_fractions.empty() ? nullptr : primitive.mass_fractions.data();
    const double temperature = eos.get_temperature(state.rho, kinematics.internal, xi);
    const double pressure = eos.get_pressure(state, xi);
    const double sound_speed = eos.get_sound_speed(state, pressure, xi);
    if (!(temperature > 0.0) || !std::isfinite(temperature) ||
        !(pressure > 0.0) || !std::isfinite(pressure) ||
        !(sound_speed > 0.0) || !std::isfinite(sound_speed))
        throw std::runtime_error(std::string(label)
            + " lies outside the selected EOS domain (temperature, pressure, sound speed)");
    return state;
}

// Temperature of a resolved boundary primitive. A temperature-based primitive
// already carries it; a pressure-based one recovers the specific internal
// energy through the same conservation authority used for the ghost build.
template <class Eos>
double BoundaryBaseTemperature(const PrimitiveData& primitive, const Eos& eos,
                               const SimConfig& config, const char* label)
{
    if (primitive.has_temperature) return primitive.temperature;
    arch::state::Repair report{};
    const FluidVector state = ProblemHelper::detail::InitialConservedState(
        primitive, eos, config.numerics, &report);
    if (report.status == arch::state::Status::repaired)
        throw std::runtime_error(std::string(label)
            + " required a conserved-state repair; physical boundary requests are rejected instead of clamped");
    const auto kinematics = arch::state::recover(state);
    if (kinematics.status != arch::state::Status::valid)
        throw std::runtime_error(std::string(label) + " has no resolvable thermal energy");
    const double* xi = primitive.mass_fractions.empty() ? nullptr : primitive.mass_fractions.data();
    const double temperature = eos.get_temperature(state.rho, kinematics.internal, xi);
    if (!(temperature > 0.0) || !std::isfinite(temperature))
        throw std::runtime_error(std::string(label) + " lies outside the selected EOS temperature domain");
    return temperature;
}

inline GridMetrics::Geometry PhysicalBoundaryGeometry(const Grid& grid)
{
    const GridMetrics::Geometry geometry = GridMetrics::geometry_kind(grid);
    if (geometry == GridMetrics::Geometry::Unsupported)
        throw std::invalid_argument("physical boundary requires a cartesian, cylindrical or spherical grid");
    return geometry;
}

// Outward Cartesian normal of a boundary face in the native orthonormal basis:
// Cartesian e_i, cylindrical e_R/e_z/e_phi for the actual grid dimension, and
// spherical e_r/e_theta/e_phi. The dimension-1 radial mapping follows
// Grid::PhysicalCoordsFromNative, so 1-D radial faces use e_r with the
// equatorial spherical parameters. The direction is derived from the native
// coordinate mapping, never injected arbitrarily at an axis or pole; spacing
// degeneracies are reported through the metric distance instead.
inline std::array<double, 3> PhysicalBoundaryNormal(const Grid& grid, const PointCoords& face,
    BoundaryAxis axis, BoundarySide side,
    GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing)
{
    return BoundaryCartesianNormal(grid, face, axis, side, semantics);
}

// Per-native-unit metric factor of the boundary axis. The physical normal step
// is dx_axis * factor, and it comes from the shared GridMetrics::PhysicalSpacing
// authority evaluated at the face, so angular directions use r (and r*sin
// theta) while length directions use unity. A zero-extent axis carries no
// physical distance and returns 0. Explicit native RZ uses shared (dr,dz)
// physical spacing, so both active coordinates have metric factor one.
inline double PhysicalBoundaryMetric(const Grid& grid, const PointCoords& face, BoundaryAxis axis,
    GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing)
{
    const auto view = GridMetrics::make_geometry_view(grid, semantics);
    const GridMetrics::Geometry geometry = PhysicalBoundaryGeometry(grid);
    const int direction = static_cast<int>(axis);
    if (direction < 0 || direction >= grid.dim)
        throw std::invalid_argument("physical boundary axis must be an active grid direction");
    const double radius = (geometry == GridMetrics::Geometry::Cylindrical) ? face.r_cy : face.r;
    const double spacing = view.semantics == GridMetrics::GeometrySemantics::AxisymmetricRz
        ? GridMetrics::Rz::PhysicalSpacing(direction, view.dx1, view.dx2)
        : GridMetrics::PhysicalSpacing(geometry, grid.dim, direction,
            grid.dx1, grid.dx2, grid.dx3, radius, face.theta);
    const double native_step = direction == 0 ? grid.dx1 : (direction == 1 ? grid.dx2 : grid.dx3);
    if (!(native_step > 0.0)) return 0.0;
    const double metric = spacing / native_step;
    if (!std::isfinite(metric) || metric < 0.0)
        throw std::invalid_argument("physical boundary metric factor must be finite and non-negative");
    return metric;
}

} // namespace detail

/**
 * Validate a returned PhysicalBoundaryData against the registry size and the
 * configured floors. Rejects unknown scalar kinds, non-finite values, invalid
 * composition counts/sums, inconsistent species prescriptions and hydro
 * primitives that are outside the primitive domain. Nothing is modified.
 *
 * The EOS-domain part of the contract (temperature, pressure, sound speed of the
 * resolved ghost) is enforced by EvaluatePhysicalBoundary, which owns the EOS.
 */
inline void ValidatePhysicalBoundaryData(const PhysicalBoundaryData& data,
                                         const SimConfig& config, int species_count)
{
    if (species_count < 0)
        throw std::invalid_argument("physical boundary requires a non-negative species count");
    const NumericsConfig& limits = config.numerics;
    if (!std::isfinite(limits.sml_rho) || limits.sml_rho < 0.0 ||
        !std::isfinite(limits.min_eint) || limits.min_eint < 0.0 ||
        std::isnan(limits.max_eint) || limits.max_eint < limits.min_eint)
        throw std::invalid_argument(
            "physical boundary requires finite ordered numerics floors (sml_rho, min_eint, max_eint)");

    detail::ValidateScalarCondition(data.temperature, "temperature");
    if (data.temperature.kind == ScalarBoundaryKind::Value && !(data.temperature.value > 0.0))
        throw std::invalid_argument("a temperature Value prescription must be a positive face temperature");
    for (int axis = 0; axis < 3; ++axis)
        detail::ValidateScalarCondition(data.velocity[axis], "velocity");

    if (!data.species.empty()) {
        if (static_cast<int>(data.species.size()) != species_count)
            throw std::invalid_argument(
                "species prescriptions must supply one entry per registered species");
        const ScalarBoundaryKind kind = data.species.front().kind;
        for (const ScalarBoundaryCondition& condition : data.species) {
            detail::ValidateScalarCondition(condition, "species");
            if (condition.kind == ScalarBoundaryKind::None)
                throw std::invalid_argument(
                    "species prescriptions must set one scalar kind for every registered species");
            if (condition.kind != kind)
                throw std::invalid_argument(
                    "species prescriptions must all use the same scalar kind");
        }
        if (kind == ScalarBoundaryKind::Value) {
            for (const ScalarBoundaryCondition& condition : data.species)
                if (!(condition.value >= 0.0))
                    throw std::invalid_argument("species Value prescriptions must be non-negative fractions");
        }
        std::vector<double> values;
        values.reserve(data.species.size());
        for (const ScalarBoundaryCondition& condition : data.species) values.push_back(condition.value);
        detail::RequireMassFractions(std::span<const double>(values), species_count,
            kind == ScalarBoundaryKind::Value, "species prescription");
    }

    if (data.hydro) {
        const PrimitiveData& hydro = *data.hydro;
        if (!(hydro.rho > 0.0) || !std::isfinite(hydro.rho))
            throw std::invalid_argument(
                "hydro ghost density must be finite and positive; exact vacuum is unsupported");
        detail::RequireFinite(hydro.u, "hydro ghost velocity must be finite");
        detail::RequireFinite(hydro.v, "hydro ghost velocity must be finite");
        detail::RequireFinite(hydro.w, "hydro ghost velocity must be finite");
        if (!std::isfinite(hydro.p))
            throw std::invalid_argument("hydro ghost pressure must be finite");
        if (hydro.has_temperature) {
            if (!(hydro.temperature > 0.0) || !std::isfinite(hydro.temperature))
                throw std::invalid_argument("hydro ghost temperature must be finite and positive");
        } else if (!(hydro.p > 0.0)) {
            throw std::invalid_argument(
                "hydro ghost pressure must be finite and positive when no temperature is supplied");
        }
        detail::RequireMassFractions(std::span<const double>(hydro.mass_fractions), species_count,
            true, "hydro ghost");
    }
}

/**
 * Build one physical face/ghost request. `face_native` is the native position of
 * the boundary face and `ghost_native` the native center of the requested ghost
 * layer (for ghost_depth == 0 the ghost is the face itself, so the default
 * `ghost_native` resolves to `face_native`). The outward Cartesian normal and
 * the face-to-ghost normal distance use the Grid/GridMetrics geometry authority.
 * `semantics` is explicit: native RZ maps (r,z) to (x=r,y=0,z), with distances
 * |delta r| or |delta z|. Existing callers retain their current coordinate
 * conventions; this request builder does not certify native callback states.
 */
inline BoundaryCoordinates MakeBoundaryCoordinates(const Grid& grid,
    std::array<double, 3> face_native, BoundaryAxis axis, BoundarySide side, double time,
    int ghost_depth = 0, BoundaryPurpose purpose = BoundaryPurpose::Hydro,
    std::array<double, 3> ghost_native = {},
    GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing)
{
    if (grid.dim < 1 || grid.dim > 3)
        throw std::invalid_argument("physical boundary requires an active grid dimension in [1,3]");
    (void)GridMetrics::make_geometry_view(grid, semantics);
    if (!std::isfinite(time))
        throw std::invalid_argument("physical boundary time must be finite");
    if (ghost_depth < 0)
        throw std::invalid_argument("physical boundary ghost depth must be non-negative");
    if (static_cast<unsigned>(purpose) > static_cast<unsigned>(BoundaryPurpose::Gravity))
        throw std::invalid_argument("physical boundary purpose is not a known BoundaryPurpose");
    for (const double coordinate : face_native)
        detail::RequireFinite(coordinate, "physical boundary face native coordinates must be finite");
    for (const double coordinate : ghost_native)
        detail::RequireFinite(coordinate, "physical boundary ghost native coordinates must be finite");
    // Depth zero evaluates the boundary face, which has no separate ghost center.
    if (ghost_depth == 0) ghost_native = face_native;

    BoundaryCoordinates coordinates;
    coordinates.axis = axis;
    coordinates.side = side;
    coordinates.purpose = purpose;
    coordinates.dimension = grid.dim;
    coordinates.ghost_depth = ghost_depth;
    coordinates.time = time;
    coordinates.native_position = face_native;
    coordinates.point = Grid::PhysicalCoordsFromNative(grid.dim, grid.geometry,
        face_native[0], face_native[1], face_native[2], semantics);
    coordinates.ghost_point = Grid::PhysicalCoordsFromNative(grid.dim, grid.geometry,
        ghost_native[0], ghost_native[1], ghost_native[2], semantics);
    coordinates.cartesian_normal = detail::PhysicalBoundaryNormal(grid, coordinates.point, axis, side, semantics);
    const double metric = detail::PhysicalBoundaryMetric(grid, coordinates.point, axis, semantics);
    const int direction = static_cast<int>(axis);
    const double distance = std::abs(ghost_native[direction] - face_native[direction]) * metric;
    if (!std::isfinite(distance) || distance < 0.0)
        throw std::invalid_argument("physical boundary distance must be finite and non-negative");
    coordinates.physical_distance = distance;
    return coordinates;
}

/**
 * Copy a conserved interior cell into the primitive snapshot passed to the
 * physical boundary callback: rho, native velocity, pressure, temperature and
 * the complete mass-fraction vector from the selected EOS. The caller owns
 * `composition` for the duration of the call.
 */
template <class Eos>
PrimitiveData BoundaryInteriorPrimitive(const FluidVector& interior,
                                       std::span<const double> composition, const Eos& eos)
{
    if (!(interior.rho > 0.0) || !std::isfinite(interior.rho))
        throw std::invalid_argument(
            "interior boundary density must be finite and positive; exact vacuum is unsupported");
    const auto kinematics = arch::state::recover(interior);
    if (kinematics.status != arch::state::Status::valid)
        throw std::runtime_error("interior boundary state has no resolvable thermal energy");
    const int species_count = static_cast<int>(composition.size());
    detail::RequireMassFractions(composition, species_count, true, "interior boundary composition");
    const double* xi = composition.empty() ? nullptr : composition.data();
    PrimitiveData primitive;
    primitive.rho = interior.rho;
    primitive.u = kinematics.u;
    primitive.v = kinematics.v;
    primitive.w = kinematics.w;
    primitive.temperature = eos.get_temperature(interior.rho, kinematics.internal, xi);
    if (!(primitive.temperature > 0.0) || !std::isfinite(primitive.temperature))
        throw std::runtime_error("interior boundary state lies outside the selected EOS temperature domain");
    primitive.p = eos.get_pressure(interior, xi);
    if (!(primitive.p > 0.0) || !std::isfinite(primitive.p))
        throw std::runtime_error("interior boundary state lies outside the selected EOS pressure domain");
    primitive.has_temperature = true;
    primitive.mass_fractions.assign(composition.begin(), composition.end());
    return primitive;
}

/**
 * Resolve already returned callback data using one shared point-law owner.
 *
 * Workflow: validate the returned configuration/channels, resolve Hydro or
 * Diffusion inheritance, apply Value (2*face-interior) or NormalGradient
 * (interior+2*distance*gradient), and call the sole BuildBoundaryConserved
 * authority. None/OutwardFlux preserve inherited conserved/composition bytes.
 * The caller owns actual point-EOS validation of interior_primitive and any
 * supplied inherited_primitive; this function does not reinterpret native
 * V/W cell means, sample a cell, invoke a callback, or publish solver storage.
 * A supplied inherited point is used only when no hydro replacement exists
 * and a channel changes state. Request time comes from MakeBoundaryCoordinates;
 * no new time policy is introduced here or in the existing wrapper.
 */
template <class Eos>
PhysicalBoundaryEvaluation ResolvePhysicalBoundaryData(const BoundaryCoordinates& coordinates,
    const SimConfig& config, const Eos& eos, const PhysicalBoundaryData& data,
    const FluidVector& interior, std::span<const double> composition,
    const PrimitiveData& interior_primitive, const FluidVector* inherited_ghost = nullptr,
    std::span<const double> inherited_composition = {},
    const PrimitiveData* inherited_primitive = nullptr)
{
    if (composition.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("physical boundary requires a non-negative species count");
    const int species_count = static_cast<int>(composition.size());
    ValidatePhysicalBoundaryData(data, config, species_count);

    PhysicalBoundaryEvaluation evaluation{};
    evaluation.conditions = data;

    if (coordinates.purpose == BoundaryPurpose::Hydro) {
        if (!data.hydro)
            throw std::invalid_argument(
                "a Hydro physical boundary request requires a hydro primitive");
        // Diffusion settings never change the hydro ghost. A Value or
        // NormalGradient prescription would rewrite the ghost state, so it is
        // channel misuse here; OutwardFlux prescriptions leave every ghost at
        // its base value and stay in the returned conditions for the manager's
        // direct-flux override, so they are reported without changing hydro.
        bool state_changing_channel =
            data.temperature.kind == ScalarBoundaryKind::Value ||
            data.temperature.kind == ScalarBoundaryKind::NormalGradient;
        for (int axis = 0; axis < 3; ++axis)
            state_changing_channel = state_changing_channel ||
                data.velocity[axis].kind == ScalarBoundaryKind::Value ||
                data.velocity[axis].kind == ScalarBoundaryKind::NormalGradient;
        for (const ScalarBoundaryCondition& condition : data.species)
            state_changing_channel = state_changing_channel ||
                condition.kind == ScalarBoundaryKind::Value ||
                condition.kind == ScalarBoundaryKind::NormalGradient;
        if (state_changing_channel)
            throw std::invalid_argument(
                "Value/NormalGradient diffusion channels cannot change a Hydro physical boundary request");
        evaluation.conserved = detail::BuildBoundaryConserved(*data.hydro, eos, config, "hydro ghost");
        evaluation.mass_fractions = data.hydro->mass_fractions;
        return evaluation;
    }

    if (coordinates.purpose != BoundaryPurpose::Diffusion)
        throw std::invalid_argument(
            "physical boundary evaluation supports the Hydro and Diffusion purposes; potential boundaries have their own owner");

    const bool temperature_prescribed = detail::HasPrescription(data.temperature);
    bool velocity_prescribed = false;
    for (int axis = 0; axis < 3; ++axis)
        velocity_prescribed = velocity_prescribed || detail::HasPrescription(data.velocity[axis]);
    const bool species_prescribed = !data.species.empty();

    // Spatial dimension does not remove transverse velocity components. A 1D
    // shear problem still evolves all three momenta through the shared closure.
    const auto& diffusion = config.physics.diffusion;
    if ((temperature_prescribed && !diffusion.use_thermal_diffusion)
        || (velocity_prescribed && !diffusion.use_viscous_diffusion)
        || (species_prescribed && !diffusion.use_species_diffusion))
        throw std::invalid_argument(
            "physical boundary prescribes an inactive diffusion channel");

    const FluidVector& inherited = inherited_ghost ? *inherited_ghost : interior;
    const auto inherited_x = inherited_ghost ? inherited_composition : composition;
    if(inherited_x.size()!=composition.size())
        throw std::invalid_argument("inherited boundary composition must match the registered species");
    const auto changes_state=[](const ScalarBoundaryCondition& condition) {
        return condition.kind==ScalarBoundaryKind::Value || condition.kind==ScalarBoundaryKind::NormalGradient;
    };
    bool changing_channel=changes_state(data.temperature);
    for(const auto& condition:data.velocity) changing_channel=changing_channel || changes_state(condition);
    for(const auto& condition:data.species) changing_channel=changing_channel || changes_state(condition);
    if (!data.hydro && !changing_channel) {
        // None and direct fluxes retain the original builtin ghost exactly.
        // In particular, a heat-only callback cannot remove wall reflection.
        evaluation.conserved = inherited;
        evaluation.mass_fractions.assign(inherited_x.begin(), inherited_x.end());
        return evaluation;
    }

    // A native caller supplies its actual already-EOS-validated point base.
    // Inheritance-only/direct-flux requests returned above without conversion.
    // These new supplied-view guards protect species indexing; the Existing
    // wrapper supplies nullptr and retains its original primitive owner.
    if (!data.hydro && inherited_primitive
        && inherited_primitive->mass_fractions.size() != composition.size())
        throw std::invalid_argument("inherited boundary composition must match the registered species");
    if (!data.hydro && species_prescribed
        && interior_primitive.mass_fractions.size() != composition.size())
        throw std::invalid_argument(
            "interior boundary composition must supply one mass fraction per registered species");
    PrimitiveData ghost = data.hydro ? *data.hydro
        : inherited_primitive ? *inherited_primitive
        : BoundaryInteriorPrimitive(inherited, inherited_x, eos);
    if (!ghost.has_temperature)
        ghost.temperature = detail::BoundaryBaseTemperature(ghost, eos, config, "diffusion ghost base");
    ghost.has_temperature = true;

    if (changes_state(data.temperature)) {
        ghost.temperature = detail::ApplyScalarPrescription(
            data.temperature, data.hydro ? ghost.temperature : interior_primitive.temperature, coordinates.physical_distance);
        if (!(ghost.temperature > 0.0) || !std::isfinite(ghost.temperature))
            throw std::invalid_argument(
                "temperature prescription leaves the ghost outside the EOS temperature domain");
    }
    for (int axis = 0; axis < 3; ++axis) {
        if (!detail::HasPrescription(data.velocity[axis])) continue;
        double& component = axis == 0 ? ghost.u : (axis == 1 ? ghost.v : ghost.w);
        if(changes_state(data.velocity[axis])) {
            const double reference=data.hydro ? component : axis==0 ? interior_primitive.u
                : axis==1 ? interior_primitive.v : interior_primitive.w;
            component = detail::ApplyScalarPrescription(data.velocity[axis], reference, coordinates.physical_distance);
        }
        if (!std::isfinite(component))
            throw std::invalid_argument("velocity prescription produced a non-finite ghost velocity");
    }
    if (species_prescribed) {
        for (std::size_t index = 0; index < ghost.mass_fractions.size(); ++index)
            ghost.mass_fractions[index] = detail::ApplyScalarPrescription(
                data.species[index], changes_state(data.species[index]) && !data.hydro
                    ? interior_primitive.mass_fractions[index] : ghost.mass_fractions[index],
                coordinates.physical_distance);
        detail::RequireMassFractions(std::span<const double>(ghost.mass_fractions), species_count,
            true, "diffusion ghost composition");
    }
    evaluation.conserved = detail::BuildBoundaryConserved(ghost, eos, config, "diffusion ghost");
    evaluation.mass_fractions = ghost.mass_fractions;
    return evaluation;
}

/**
 * Resolve one physical boundary request into a conserved ghost state.
 *
 * The read-only callback is invoked as `PhysicalBoundaryData callback(const
 * PhysicalBoundaryContext&)` with an EOS-backed interior snapshot; it must not
 * retain the context or write solver state. Hydro requests build the ghost
 * directly from the returned primitive. Diffusion requests inherit the builtin
 * ghost (or the interior when no inherited snapshot is supplied). Value and
 * NormalGradient reference the interior; None and OutwardFlux retain the base.
 * State-changing channels rebuild conserved variables. Repairs, out-of-EOS
 * states, non-finite results and channel misuse are rejected.
 */
template <class Eos, class Callback>
PhysicalBoundaryEvaluation EvaluatePhysicalBoundary(const BoundaryCoordinates& coordinates,
    const SimConfig& config, const SpeciesManager& species, const Eos& eos,
    const Callback& callback, const FluidVector& interior, std::span<const double> composition,
    const FluidVector* inherited_ghost = nullptr, std::span<const double> inherited_composition = {})
{
    const int species_count = species.count();
    if (species_count < 0)
        throw std::invalid_argument("physical boundary requires a non-negative species count");
    if (composition.size() != static_cast<std::size_t>(species_count))
        throw std::invalid_argument(
            "interior boundary composition must supply one mass fraction per registered species");
    if (coordinates.dimension < 1 || coordinates.dimension > 3)
        throw std::invalid_argument("physical boundary requires an active dimension in [1,3]");
    if (coordinates.ghost_depth < 0)
        throw std::invalid_argument("physical boundary ghost depth must be non-negative");
    if (!std::isfinite(coordinates.physical_distance) || coordinates.physical_distance < 0.0)
        throw std::invalid_argument("physical boundary distance must be finite and non-negative");

    const PrimitiveData interior_primitive = BoundaryInteriorPrimitive(interior, composition, eos);
    const PhysicalBoundaryContext context(coordinates, config, species, interior_primitive);
    PhysicalBoundaryData data = callback(context);
    return ResolvePhysicalBoundaryData(coordinates, config, eos, data, interior,
        composition, interior_primitive, inherited_ghost, inherited_composition);
}

} // namespace arch::boundary
