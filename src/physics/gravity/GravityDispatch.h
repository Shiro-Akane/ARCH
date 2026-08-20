/**
 * @file GravityDispatch.h
 * @brief Factory for creating gravity policies.
 * Resolves the gravity type at compile-time and returns a pure virtual interface pointer.
 */

/**
 * Workflow:
 * 1. Construct the selected gravity policy from runtime configuration.
 * 2. Evaluate accelerations or potentials on the current active AMR geometry.
 * 3. Apply gravity only through the common source-term interface used by every integrator.
 */

#pragma once

#include <memory>
#include <string>

#include "ExternalGravity.h"
#include "GravityNone.h"

#include "../../data/GlobalDefs.h"

namespace Physical
{
    namespace Gravity
    {
        /**
         * @brief Factory for gravity policy
         * @return std::unique_ptr<IGravityPolicy>
         */
        inline std::unique_ptr<IGravityPolicy> make_gravity(const SimConfig &config)
        {
            std::string grav_type = config.physics.gravity.type;

            if (grav_type == "external" || grav_type == "External")
            {
                return std::make_unique<ExternalGravity>(
                    config.physics.gravity.g_x,
                    config.physics.gravity.g_y,
                    config.physics.gravity.g_z);
            }
            else if (grav_type == "self" || grav_type == "Self")
            {
                std::cerr << "[Error] Self gravity is not yet implemented!" << std::endl;
                std::abort();
            }
            else
            {
                // Default: None
                return std::make_unique<GravityNone>();
            }
        }

    } // namespace Gravity
} // namespace Physical
