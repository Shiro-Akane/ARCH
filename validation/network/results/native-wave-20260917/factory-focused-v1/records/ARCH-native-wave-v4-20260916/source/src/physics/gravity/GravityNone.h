/**
 * @file GravityNone.h
 * @brief Provides the no-gravity policy with a source-free contract.
 *
 * Both field preparation and source accumulation are no-ops, allowing the
 * integrator to retain the same policy interface when gravity is disabled.
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
