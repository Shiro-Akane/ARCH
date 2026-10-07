/**
 * @file GravitySource.h
 * @brief Shared self/external gravity source data and cell arithmetic.
 *
 * Workflow:
 * 1. Publish physical face acceleration and geometry-weighted work coefficients.
 * 2. Accumulate one momentum update from local orthonormal acceleration.
 * 3. Accumulate the chosen self-gravity face-flux or external cell work once.
 * 4. For explicit native external math, apply the same point leaf to eight
 *    immutable physical states, then integrate source momentum with V/W and
 *    physical rho*u dot g work with V. No source origin is inferred here.
 *
 * The same leaves are called by Host and CUDA; backends own storage and loops.
 */
#pragma once

#include <array>
#include <cmath>
#include <cstddef>

#include "core/ArchPortability.h"
#include "data/FluidState.h"
#include "grid/GridMetrics.h"
#include "numerics/state/RzCellAverage.h"

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

namespace detail {

/** Borrow the local eight-point source buffer without a host-only closure.
 * The pointer expires at the containing call's return; the shared averaging
 * leaf invokes this device-callable reader only while that buffer is alive.
 */
struct NativeExternalSourceReader {
    const FluidVector* values;
    ARCH_INLINE FluidVector operator()(std::size_t index) const {
        return values[index];
    }
};

} // namespace detail

/** Return one native external source increment from real physical samples.
 * Workflow:
 * 1. Require finite acceleration and a finite strictly positive stage dt.
 * 2. Borrow the eight immutable, already actual-EOS-validated point states;
 *    recheck finite fields and positive rho before invoking the original leaf.
 * 3. Apply add_external_gravity_source_cell once to each zero local increment.
 * 4. Integrate that signed source using the one shared V/W arithmetic owner.
 *
 * For explicit native RZ, g_x/g_y/g_z mean local g_r/g_z/g_phi. At each
 * sample the original leaf gives dt*(rho*g_r,rho*g_z,rho*g_phi) and
 * dt*rho*(u_r*g_r+u_z*g_z+u_phi*g_phi). The first two sources use V, the
 * azimuthal source uses W, and energy uses V. Thus Delta(m_phi,W)=dt*Q_J/W
 * with Q_J=integral(r*rho*g_phi dV); no extra radius lever is applied. Work
 * consumes physical point m_phi, never the stored J/W as a V momentum.
 *
 * Delta rho is exactly zero. No caller buffer/state is written, including on
 * failure. Nonpositive dt uses the existing invalid_weight status; nonfinite
 * view/dt/point data uses nonfinite_state. Positive-density and integration
 * failures retain the shared existing categories and unusable NaN payload.
 * No EOS is recomputed here, and success certifies only this mathematical
 * integration, not source origin, stage identity, Runtime or Device support.
 * The point reader must be immutable, device-callable and must not throw.
 */
template<class StateReader>
ARCH_INLINE RzCellAverage::ConservedMean native_external_source_mean(
    const std::array<GridMetrics::Rz::CellAverageSample,8>& samples,
    const StateReader& point_reader,ExternalGravityView gravity,double dt)
{
    using RzCellAverage::ConservedMean;
    using RzCellAverage::Status;
    if(!std::isfinite(gravity.g_x)||!std::isfinite(gravity.g_y)
        ||!std::isfinite(gravity.g_z)||!std::isfinite(dt))
        return ConservedMean(Status::nonfinite_state);
    if(!(dt>0.))return ConservedMean(Status::invalid_weight);
    std::array<FluidVector,8> increments{};
    for(std::size_t k=0;k<increments.size();++k) {
        const auto point=point_reader(k);
        if(!std::isfinite(point.rho)||!std::isfinite(point.mom_u)
            ||!std::isfinite(point.mom_v)||!std::isfinite(point.mom_w)
            ||!std::isfinite(point.eng))
            return ConservedMean(Status::nonfinite_state);
        if(!(point.rho>0.))return ConservedMean(Status::invalid_density);
        add_external_gravity_source_cell(point,gravity,dt,increments[k]);
    }
    return RzCellAverage::source_components_mean(
        samples,detail::NativeExternalSourceReader{increments.data()});
}

} // namespace Physical::Gravity
