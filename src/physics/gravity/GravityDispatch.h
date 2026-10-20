/**
 * @file GravityDispatch.h
 * @brief Host factory for the resolved gravity policy.
 *
 * Runtime selection constructs a type-erased patch policy; it does not solve
 * a field or apply source terms. ExternalGravity delegates its cell update to
 * GravitySource.h, the mathematical owner also used by device code.
 * Workflow:
 * 1. Read the resolved gravity kind and CGS parameters.
 * 2. Leave disabled gravity absent, or construct the external/self policy.
 * 3. Return the optional policy; field solves begin only at stage preparation.
 */

#pragma once

#include <memory>
#include <stdexcept>
#include <string>

#include "physics/gravity/ExternalGravity.h"
#include "physics/gravity/self/SelfGravity.h"

#include "data/GlobalDefs.h"
#include "driver/dispatch/capability/ResolvedExecutionPlan.h"

namespace Physical
{
    namespace Gravity
    {
        /**
         * @brief Factory for gravity policy
         * @return std::unique_ptr<IGravityPolicy>
         */
        inline std::unique_ptr<IGravityPolicy> make_gravity(
            const SimConfig& config, arch::dispatch::GravityId gravity)
        {
            using arch::dispatch::GravityId;
            switch (gravity) {
            case GravityId::None:
                return nullptr;
            case GravityId::External:
                return std::make_unique<ExternalGravity>(
                    config.physics.gravity.g_x,
                    config.physics.gravity.g_y,
                    config.physics.gravity.g_z);
            case GravityId::Self:
                return std::make_unique<SelfGravity>(config.physics.gravity);
            }
            throw std::logic_error("resolved gravity has no CPU binding");
        }

    } // namespace Gravity
} // namespace Physical
