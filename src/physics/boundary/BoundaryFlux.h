/**
 * @file BoundaryFlux.h
 * @brief Shared scalar boundary controls and conservative transport-flux edits.
 *
 * Workflow:
 * 1. Host evaluates a physical callback at physical boundary faces only.
 * 2. Host or CUDA borrows the same packed face controls in native face order.
 * 3. The diffusion operator applies outward fluxes before divergence/reflux.
 *
 * Value/gradient conditions are realized by EOS-backed ghost construction.
 * Prescribed fluxes enter the actual flux, so temperature-dependent material
 * coefficients cannot change an imposed heat or species flux.
 */
#pragma once

#include <array>
#include <memory>
#include <vector>

#include "core/ArchPortability.h"

namespace arch::boundary {
enum class BoundaryPurpose { Hydro, Diffusion, Gravity };
enum class ScalarBoundaryKind { None, Value, NormalGradient, OutwardFlux };
struct ScalarBoundaryCondition {
    ScalarBoundaryKind kind = ScalarBoundaryKind::None;
    double value = 0.;
};

/** Flat mathematical wall flags borrowed from one trusted boundary frame.
 * No FluidState pointer, allocation or serialized scientific state is carried.
 * The upstream builder authenticates actual BC/topology/time and excludes the
 * native regular axis; these mathematical flags do not prove that authority.
 */
struct HydroBoundaryView {
    std::array<bool,6> reflecting{};

    /** An empty default preserves the existing non-wall arithmetic path. */
    ARCH_INLINE bool empty() const {
        for(bool wall:reflecting)if(wall)return false;
        return true;
    }

    /** Checked actual face lookup; false leaves side unchanged on malformed
     * direction/extents/range. Successful side=-1 is internal/unmarked, 0 is
     * lower, 1 upper. An internal face never inherits a boundary flag.
     */
    ARCH_INLINE bool reflecting_side(int direction,int face,int active_lower,
        int active_upper,int& side) const {
        if(direction<0||direction>=3||active_lower<0||active_upper<=active_lower
            ||face<active_lower||face>active_upper)return false;
        const int candidate=face==active_lower?0:face==active_upper?1:-1;
        side=candidate>=0&&reflecting[2*direction+candidate]?candidate:-1;
        return true;
    }
};

/** Non-owning face planes: temperature, three velocities, then species. */
struct DiffusionBoundaryView {
    std::array<const ScalarBoundaryCondition*, 6> faces{};
    /** Find a physical-face record; internal AMR faces always return null. */
    ARCH_INLINE const ScalarBoundaryCondition* at(int direction, int i, int j, int k,
        int is, int ie, int js, int je, int ks, int ke, int species) const {
        const int coordinate[3]{i, j, k}, lower[3]{is, js, ks}, upper[3]{ie, je, ke};
        const int side = coordinate[direction] == lower[direction] ? 0
            : (coordinate[direction] == upper[direction] ? 1 : -1);
        if (side < 0 || !faces[2 * direction + side]) return nullptr;
        const int a = (direction + 1) % 3, b = (direction + 2) % 3;
        const int offset = coordinate[a] - lower[a]
            + (upper[a] - lower[a]) * (coordinate[b] - lower[b]);
        return faces[2 * direction + side] + offset * (4 + species);
    }
};

/** Ephemeral host face storage; never serialized as evolved scientific state. */
struct DiffusionBoundaryStorage {
    std::array<std::vector<ScalarBoundaryCondition>, 6> faces;
    DiffusionBoundaryView view() const {
        DiffusionBoundaryView result;
        for (int side = 0; side < 6; ++side)
            result.faces[side] = faces[side].empty() ? nullptr : faces[side].data();
        return result;
    }
};

/** Preserve transport work while imposing outward flux components.
 * Native mixed-measure producers supply their matching work velocities; the
 * default retains ordinary mean-velocity arithmetic. No E/F division is used.
 */
template<class State, class Flux>
ARCH_INLINE void ApplyDiffusionBoundaryFlux(const ScalarBoundaryCondition* controls,
    double outward_sign, const State& left, const State& right, Flux& flux,
    double* species_flux, int species_count, int species_stride,
    const double* matched_work_velocity=nullptr) {
    if (!controls) return;
    const double velocity[3]{
        matched_work_velocity?matched_work_velocity[0]:.5*(left.mom_u/left.rho+right.mom_u/right.rho),
        matched_work_velocity?matched_work_velocity[1]:.5*(left.mom_v/left.rho+right.mom_v/right.rho),
        matched_work_velocity?matched_work_velocity[2]:.5*(left.mom_w/left.rho+right.mom_w/right.rho)};
    // F_E = q + tau_i*v_i. Replace q/tau without discarding mechanical work.
    double heat = flux.eng - flux.mom_u * velocity[0]
        - flux.mom_v * velocity[1] - flux.mom_w * velocity[2];
    if (controls[0].kind == ScalarBoundaryKind::OutwardFlux)
        heat = outward_sign * controls[0].value;
    double* momentum[3]{&flux.mom_u, &flux.mom_v, &flux.mom_w};
    for (int d = 0; d < 3; ++d)
        if (controls[d + 1].kind == ScalarBoundaryKind::OutwardFlux)
            *momentum[d] = outward_sign * controls[d + 1].value;
    flux.eng = heat + flux.mom_u * velocity[0]
        + flux.mom_v * velocity[1] + flux.mom_w * velocity[2];
    for (int s = 0; s < species_count; ++s)
        if (controls[4 + s].kind == ScalarBoundaryKind::OutwardFlux)
            species_flux[s * species_stride] = outward_sign * controls[4 + s].value;
}
} // namespace arch::boundary
