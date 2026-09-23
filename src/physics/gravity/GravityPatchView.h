/**
 * @file GravityPatchView.h
 * @brief Share face acceleration and flux-work formulas with host and CUDA policies.
 *
 * Workflow:
 * 1. Receive active density with mesh and generation identity.
 * 2. Share face acceleration and flux-work formulas with host and CUDA policies.
 * 3. Publish a checked potential/acceleration field for the requested stage.
 */

#pragma once

#include "core/ArchPortability.h"

namespace Physical::Gravity {
struct GravityPatchView {
    const double* density=nullptr;
    const double* faces[3]{};
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
    // Delta(E) = dt/2 * [F_rho,left*g_left + F_rho,right*g_right].
    return 0.5*dt*(flux_low*low+flux_high*high);
}
}
