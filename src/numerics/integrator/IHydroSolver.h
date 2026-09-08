/**
 * @file IHydroSolver.h
 * @brief Defines the type-erased block-level hydrodynamics solver contract.
 *
 * Host time integrators request patch divergence/source evaluation and stage
 * combination through this interface. Concrete flux and reconstruction
 * templates remain behind the binding, while the driver controls AMR
 * synchronization and the integrator supplies stage weights.
 */

#pragma once

#include <vector>

#include "../../amr/AMRControl.h"
#include "../../core/RuntimeParams.h"
#include "../../data/FluidState.h"
#include "../../grid/Grid.h"
#include "../../physics/gravity/IGravityPolicy.h"

namespace Numerics {

/**
 * @brief Pure virtual interface for hydrodynamics solvers.
 * Acts as a compilation firewall for the heavy templates of Flux and Reconstruction schemes.
 * Operates at the Patch (Block) level to allow the AMR framework full control over synchronization.
 */
class IHydroSolver {
public:
    virtual ~IHydroSolver() = default;

    /**
     * @brief Evaluates flux divergences and geometric/gravity sources on a single patch.
     * @param state The current fluid state.
     * @param grid The grid topology.
     * @param dt The time step size.
     * @param dU The accumulator for conservative variable changes.
     * @param d_spec The accumulator for conserved species-density changes (rho*X).
     * @param gravity The gravity policy interface.
     * @param execution_stream Reserved opaque execution context; the Host binding ignores it.
     */
    virtual void evaluate_patch(amr::AMRControl* amr_ctrl, int block_id,
                                const FluidState& state, const Grid& grid, double dt,
                                std::vector<FluidVector>& dU, std::vector<double>& d_spec,
                                const Physical::Gravity::IGravityPolicy* gravity,
                                const NumericsConfig& num_cfg, double flux_weight = 1.0,
                                void* execution_stream = nullptr) const = 0;

    /**
     * @brief Performs the Runge-Kutta stage update combining old, current, and flux divergence states.
     * @param state_old State at t^n.
     * @param state_curr State at intermediate RK stage.
     * @param state_new The destination state for the updated variables.
     * @param dU The computed flux divergence.
     * @param d_spec The computed species flux divergence.
     * @param grid The grid topology.
     * @param w_old Weight for the old state.
     * @param w_flux Weight for the flux contribution.
     * @param execution_stream Reserved opaque execution context; the Host binding ignores it.
     */
    virtual void update_patch(const FluidState& state_old, const FluidState& state_curr, FluidState& state_new,
                              const std::vector<FluidVector>& dU, const std::vector<double>& d_spec,
                              const Grid& grid, double w_old, double w_flux,
                              const NumericsConfig& num_cfg,
                              void* execution_stream = nullptr) const = 0;
};

} // namespace Numerics
