/**
 * @file BoundaryTypes.h
 * @brief Typed, read-only case boundary requests and returned physical data.
 *
 * Workflow:
 * 1. Core constructs face coordinates, stage time and a copied interior state.
 * 2. A case callback returns primitive or scalar boundary data in CGS units.
 * 3. Core validates the result and applies it through the selected EOS/operator.
 *
 * Callbacks never receive an AMR block, writable state or device pointer.
 * Hydro and diffusion data belong to physical_boundary.cpp; potential data
 * belong to gravity_boundary.cpp. Native velocities use the Grid basis.
 */
#pragma once

#include <array>
#include <optional>
#include <vector>

#include "amr/exchange/BoundaryPlan.h"
#include "data/UserTypes.h"
#include "grid/Grid.h"
#include "physics/species/Species.h"

namespace arch::boundary {
enum class BoundaryPurpose { Hydro, Diffusion, Gravity };
enum class ScalarBoundaryKind { None, Value, NormalGradient, OutwardFlux };

/** Scalar data: value, outward normal derivative, or outward transport flux. */
struct ScalarBoundaryCondition {
    ScalarBoundaryKind kind = ScalarBoundaryKind::None;
    double value = 0.;
};

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
