/**
 * @file ExternalGravitySource.h
 * @brief Shared cell-local momentum and energy increments from external gravity.
 *
 * For conserved density rho, momentum rho*v, and total energy density E,
 * add delta(rho*v) = dt*rho*g and delta(E) = dt*rho*(v dot g).
 * Acceleration uses length/time^2 in the simulation's consistent units and
 * follows the stored orthonormal axes. These are explicit stage increments;
 * the time integrator owns their combination and the backend owns traversal.
 */
#pragma once

#include "data/FluidState.h"

namespace Physical::Gravity {

// Components follow the existing CPU convention: stored orthonormal momentum
// axes, not a separately transformed Cartesian vector on curved meshes.
struct ExternalGravityView {
    double g_x = 0.0, g_y = 0.0, g_z = 0.0;
    bool enabled = false;
};

ARCH_INLINE void add_external_gravity_source_cell(
    const FluidVector& state, ExternalGravityView gravity, double dt,
    FluidVector& delta)
{
    const double rho = state.rho;
    if (!gravity.enabled) return;
    delta.mom_u += dt * rho * gravity.g_x;
    delta.mom_v += dt * rho * gravity.g_y;
    delta.mom_w += dt * rho * gravity.g_z;
    delta.eng += dt * (state.mom_u * gravity.g_x + state.mom_v * gravity.g_y + state.mom_w * gravity.g_z);
}

} // namespace Physical::Gravity
