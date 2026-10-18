/**
 * @file HydroSourceBinding.h
 * @brief Host Hydro binding to the gravity owner's patch source.
 *
 * Workflow:
 * 1. Borrow the gravity policy for the lifetime of one Hydro patch evaluation.
 * 2. Consume the original Riemann mass flux through the policy's optional
 *    face-work hook immediately after divergence.
 * 3. Delegate original same-stage AMR coarse-fine registration.
 * 4. Apply the policy's original body source after geometric sources.
 * The concrete gravity policy retains field lifetime, publication and all
 * numerical operations; this binding introduces no second source scheme.
 */
#pragma once

#include <vector>

#include "amr/flux/AMRFluxRegistering.h"
#include "driver/schedule/StageScheduler.h"
#include "grid/Grid.h"
#include "numerics/curvilinear/GridMetrics.h"
#include "physics/gravity/IGravityPolicy.h"

namespace Physical::Gravity {

/** One patch's source adapter, borrowing the original gravity policy.
 * Workflow: consume each original face flux immediately after divergence;
 * register paired Energy through AMR's generic receipt protocol; apply the
 * original body source after geometric sources. No field, numerical cache or
 * second source scheme is introduced.
 */
class HydroPatchSource final {
public:
    explicit HydroPatchSource(const IGravityPolicy* gravity):gravity_(gravity) {}

    HydroPatchSource(const HydroPatchSource&)=delete;
    HydroPatchSource& operator=(const HydroPatchSource&)=delete;

    /** Consume the same immutable Riemann mass flux, without an extra weight. */
    void add_flux_work(std::vector<FluidVector>& dU,
        const std::vector<FluidVector>& flux_buffer,const FluidState& state,
        const Grid& grid,double dt,int dir)
    {
        if (gravity_) gravity_->add_flux_work_on_patch(dU, flux_buffer, state, grid, dt, dir);
    }

    /** Delegate original AMR registration with the actual stage fluxes. */
    void register_coarse_fine_fluxes(amr::AMRControl& amr_ctrl,int block_id,
        const Grid& grid,int dir,const std::vector<FluidVector>& flux_buffer,
        const std::vector<double>& spec_flux_buffer,int n_spec,double flux_weight)
    {
        amr::RegisterCoarseFineFluxes(amr_ctrl, block_id, grid, dir,
            flux_buffer, spec_flux_buffer, n_spec, flux_weight);
    }

    /** Apply the original body source after geometry. */
    void add_sources(std::vector<FluidVector>& dU,const FluidState& state,
        const Grid& grid,double dt)
    {
        if (gravity_) gravity_->add_sources_on_patch(dU,state,grid,dt,nullptr);
    }

private:
    const IGravityPolicy* gravity_;
};
} // namespace Physical::Gravity
