/**
 * @file IGravityPolicy.h
 * @brief Defines the common gravity-policy boundary for CPU and future device paths.
 *
 * Workflow:
 * 1. Construct the selected gravity policy from runtime configuration.
 * 2. Evaluate accelerations or potentials on the current active AMR geometry.
 * 3. Apply gravity only through the common source-term interface used by every integrator.
 */

#pragma once

#include "../../data/FluidState.h"
#include "../../grid/Grid.h"
#include <vector>

namespace Physical {
namespace Gravity {

/**
 * @brief Pure virtual interface for gravity models.
 * Acts as a compilation firewall (type erasure boundary) between the generic
 * numerical hydrodynamics loops and specific gravity implementations (CPU/CUDA).
 */
class IGravityPolicy {
public:
    virtual ~IGravityPolicy() = default;

    /**
     * @brief Computes and stores gravity fields/potentials if necessary.
     * @param execution_stream Opaque pointer for heterogeneous execution (e.g. cudaStream_t).
     */
    virtual void update_field(const FluidState& state, const Grid& grid, void* execution_stream = nullptr) const = 0;

    /**
     * @brief Evaluates the gravity source terms on a single patch and adds them to dU.
     * @param dU The flux divergence accumulator array.
     * @param dt The time step size.
     * @param execution_stream Opaque pointer for heterogeneous execution.
     */
    virtual void add_sources_on_patch(std::vector<FluidVector>& dU, const FluidState& state,
                                      const Grid& grid, double dt, void* execution_stream = nullptr) const = 0;
};

} // namespace Gravity
} // namespace Physical
