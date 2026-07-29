/**
 * @file DiffDispatch.h
 * @brief Dispatcher for selecting the appropriate diffusion time integrator.
 * *
 * * Workflow:
 * * 1. Read the diffusion configuration from SimConfig.
 * * 2. If diffusion is disabled, use a NoDiffusionIntegrator (dummy).
 * * 3. Otherwise, instantiate the requested integrator (RKL1, RKL2, etc.).
 * * 4. Pass the selected integrator type to the next step via a callback.
 */

#pragma once

#include <string>
#include <iostream>
#include "../../data/GlobalDefs.h"
#include "RKL1TimeIntegrator.h"
#include "RKL2TimeIntegrator.h"

namespace Numerics
{
    namespace Diffusion
    {
        // Dummy struct for no diffusion
        struct NoDiffusionIntegrator
        {
            static void integrate(FluidState&, const auto&, const Grid&, const SimConfig&, double, double, const auto&)
            {
                // Do nothing
            }
        };

        /**
         * @brief Generic zero-overhead dispatcher for the diffusion time integrator.
         * @tparam NextDispatchFunc The lambda/function to execute with the selected integrator.
         */
        template <typename NextDispatchFunc>
        inline void dispatch_diffusion(const SimConfig &config, NextDispatchFunc &&next_step)
        {
            if (!config.physics.diffusion.use_diffusion)
            {
                NoDiffusionIntegrator integrator;
                next_step(integrator);
                return;
            }

            std::string integrator_type = config.physics.diffusion.integrator;

            if (integrator_type == "RKL1" || integrator_type == "rkl1")
            {
                RKL1TimeIntegrator integrator;
                next_step(integrator);
            }
            else if (integrator_type == "RKL2" || integrator_type == "rkl2")
            {
                RKL2TimeIntegrator integrator;
                next_step(integrator);
            }

            else
            {
                std::cerr << "[Fatal Error] Unknown diffusion integrator: " << integrator_type 
                          << ". Check your configuration file." << std::endl;
                std::exit(EXIT_FAILURE);
            }
        }

    } // namespace Diffusion
} // namespace Numerics
