/**
 * @file IGravityPolicy.h
 * @brief Type-erased Host patch interface for gravitational source accumulation.
 *
 * Host integrators use this interface without including concrete policies.
 * CUDA execution uses plain views and shared cell mathematics rather than
 * invoking these virtual methods over Host-owned vectors.
 * Workflow:
 * 1. Receive one native hydro patch and its stage fluxes.
 * 2. Accumulate the selected gravity momentum and energy contributions.
 * 3. Keep backend launch/storage and domain solves in their concrete owners.
 */

#pragma once

#include <vector>

#include "data/FluidState.h"
#include "grid/Grid.h"

namespace Physical {
namespace Gravity {

/**
 * @brief Pure virtual interface for gravity models.
 * Acts as a compilation firewall (type erasure boundary) between the generic
 * Host hydrodynamics loops and specific gravity implementations.
 */
class IGravityPolicy {
public:
    virtual ~IGravityPolicy() = default;
    // Optional work from the actual Riemann mass flux. Existing external gravity
    // retains its cell source; self gravity supplies this compatible face work.
    virtual void add_flux_work_on_patch(std::vector<FluidVector>&,
        const std::vector<FluidVector>&, const FluidState&, const Grid&, double, int) const {}

    /**
     * @brief Evaluates the gravity source terms on a single patch and adds them to dU.
     * @param dU The flux divergence accumulator array.
     * @param dt The time step size.
     * @param execution_stream Reserved opaque execution context; current Host policies ignore it.
     */
    virtual void add_sources_on_patch(std::vector<FluidVector>& dU, const FluidState& state,
                                      const Grid& grid, double dt, void* execution_stream = nullptr) const = 0;
};

} // namespace Gravity
} // namespace Physical
