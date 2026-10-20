/** @file GravityUserBoundary.h
 * @brief Resolve the per-side scalar gravity policy owned by a case callback.
 *
 * Workflow:
 * 1. Identify coordinate joins and periodic AMR sides before sampling a user
 *    callback; these joins keep their geometric/topological conditions.
 * 2. Evaluate the callback with native coordinates, a Cartesian outward normal
 *    and the actual stage time, then validate its scalar boundary data.
 * 3. Publish the per-side kind/a/b structure and datum evaluator to the
 *    composite operator; only a structure change rebuilds its hierarchy.
 *
 * The composite Poisson operator owns the actual elimination of
 * a*Phi + b*dPhi/dn = c. This helper only collects the immutable per-side
 * kind/a/b structure and the position/time-dependent datum c from the active
 * user callback, so SelfGravity and its workspace stay readable.
 */

#pragma once

#include <array>
#include <functional>

#include "amr/exchange/BoundaryPlan.h"
#include "numerics/elliptic/CompositePoisson.h"
#include "physics/boundary/BoundaryTypes.h"
#include "physics/boundary/UserBoundary.h"
#include "physics/species/Species.h"

namespace Physical::Gravity {
/** True for a coordinate-axis or pole face whose physical area is zero.
 *  These faces stay regular (zero flux) and are never owned by a callback. */
bool gravity_regular_side(const arch::elliptic::EllipticMesh& mesh, int index);

/** Build the homogeneous Dirichlet/Neumann policy for the `dirichlet` and
 *  `neumann` gravity boundary names (c is zero on every owned side). Periodic
 *  AMR axes and coordinate/pole faces stay regular and are not owned here. */
arch::elliptic::CompositeBoundary gravity_homogeneous_boundary(
    const arch::elliptic::EllipticMesh& mesh, const std::array<bool, 3>& periodic,
    arch::elliptic::FaceBoundaryKind kind);

/** Sample one physical face of the active callback at a native position/time. */
arch::boundary::GravityBoundaryData gravity_user_sample(
    const arch::boundary::GravityBoundaryFunction& callback, const SimConfig& config,
    const SpeciesManager& species, const arch::elliptic::EllipticMesh& mesh, int index,
    const std::array<double, 3>& native, double time);

/** Resolve the per-side structure of the active callback for one stage.
 *  kind/a/b must be constant across a side at the requested time, and a
 *  periodic side must match the AMR topology stored in @p periodic. */
arch::elliptic::CompositeBoundary gravity_user_boundary(
    const arch::boundary::GravityBoundaryFunction& callback, const SimConfig& config,
    const SpeciesManager& species, const arch::elliptic::EllipticMesh& mesh,
    const std::array<bool, 3>& periodic, double time);

/** Compare only the per-side structure (kind, a, b) owned by the solver
 *  hierarchy; a datum-only change keeps the existing multigrid levels. */
bool gravity_same_structure(const arch::elliptic::CompositeBoundary& left,
    const arch::elliptic::CompositeBoundary& right);
} // namespace Physical::Gravity
