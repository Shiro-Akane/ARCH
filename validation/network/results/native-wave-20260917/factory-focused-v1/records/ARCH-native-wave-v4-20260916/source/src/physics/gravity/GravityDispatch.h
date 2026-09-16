/**
 * @file GravityDispatch.h
 * @brief Host factory for the resolved gravity policy.
 *
 * Runtime selection constructs a type-erased patch policy; it does not solve
 * a field or apply source terms. ExternalGravity delegates its cell update to
 * ExternalGravitySource.h, the mathematical owner also used by device code.
 */

#pragma once

#include <memory>
#include <stdexcept>
#include <string>

#include "ExternalGravity.h"
#include "GravityNone.h"

#include "../../data/GlobalDefs.h"
#include "../../driver/dispatch/ResolvedExecutionPlan.h"

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
                return std::make_unique<GravityNone>();
            case GravityId::External:
                return std::make_unique<ExternalGravity>(
                    config.physics.gravity.g_x,
                    config.physics.gravity.g_y,
                    config.physics.gravity.g_z);
            case GravityId::Self:
                throw std::runtime_error("Self gravity is not yet implemented!");
            }
            throw std::logic_error("resolved gravity has no CPU binding");
        }

    } // namespace Gravity
} // namespace Physical
