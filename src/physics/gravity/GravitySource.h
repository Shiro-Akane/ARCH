/**
 * @file GravitySource.h
 * @brief Shared self/external gravity source data and cell arithmetic.
 *
 * Workflow:
 * 1. Publish physical face acceleration and geometry-weighted work coefficients.
 * 2. Accumulate one momentum update from local orthonormal acceleration.
 * 3. Accumulate the chosen self-gravity face-flux or external cell work once.
 *
 * The same leaves are called by Host and CUDA; backends own storage and loops.
 */
#pragma once

#include "core/ArchPortability.h"
#include "data/FluidState.h"

namespace Physical::Gravity {
struct GravityPatchView {
    const double* density=nullptr;
    const double* faces[3]{};      // Physical acceleration at each native face.
    const double* work_faces[3]{}; // Cell-side coefficient from Phi_face-Phi_cell.
    /** Report whether a resident density and face field have been published. */
    ARCH_INLINE bool enabled() const {return density!=nullptr;}
};
/** Apply the midpoint face-acceleration momentum source. */
ARCH_INLINE double gravity_momentum(double low,double high,double rho,double dt) {
    // Delta(rho*u) = dt * rho * (g_left+g_right)/2.
    return dt*rho*0.5*(low+high);
}
/** Apply gravity work from the actual transported face mass flux. */
ARCH_INLINE double gravity_flux_work(double low,double high,double flux_low,double flux_high,double dt) {
    // Delta(E) = dt/2 * [F_rho,left*w_left + F_rho,right*w_right].
    // Radial w_side = +/-2*A_face/V_cell*(Phi_face-Phi_cell),
    // with plus for the low face. Cartesian retains its prior compatible
    // face-acceleration coefficient.
    return 0.5*dt*(flux_low*low+flux_high*high);
}
struct ExternalGravityView {
    double g_x = 0.0, g_y = 0.0, g_z = 0.0;
    bool enabled = false;
};
/** Apply an external acceleration in the native orthonormal vector basis. */
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
