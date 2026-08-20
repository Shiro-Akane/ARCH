/**
 * @file GravityNone.h
 * @brief Provides the no-gravity policy with a source-free contract.
 *
 * Workflow:
 * 1. Construct the selected gravity policy from runtime configuration.
 * 2. Evaluate accelerations or potentials on the current active AMR geometry.
 * 3. Apply gravity only through the common source-term interface used by every integrator.
 */

/**
 * GravityNone.h
 * @brief Implements a "no gravity" model for the simulation.
 */

#pragma once
#include "IGravityPolicy.h"

#include "../../data/FluidState.h"
#include "../../grid/Grid.h"

namespace Physical {
namespace Gravity {

struct GravityNone : public IGravityPolicy
{
    GravityNone() = default;

    virtual void update_field(const FluidState &state, const Grid &grid, void* execution_stream = nullptr) const override {}

    virtual void add_sources_on_patch(std::vector<FluidVector>& dU, const FluidState& state,
                                      const Grid& grid, double dt, void* execution_stream = nullptr) const override {}
};

}
}
