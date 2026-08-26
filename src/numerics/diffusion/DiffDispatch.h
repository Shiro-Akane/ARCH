/**
 * @file DiffDispatch.h
 * @brief Dispatcher for selecting the appropriate diffusion time integrator.
 *
 * Workflow:
 * 1. Read the diffusion configuration from SimConfig.
 * 2. If diffusion is disabled, use a NoDiffusionIntegrator (dummy).
 * 3. Otherwise, instantiate the requested integrator (RKL1, RKL2, etc.).
 * 4. Pass the selected integrator type to the next step via a callback.
 */

#pragma once

#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "RKL1TimeIntegrator.h"
#include "RKL2TimeIntegrator.h"

#include "../../data/GlobalDefs.h"
#include "../../driver/dispatch/PolicyDescriptor.h"

namespace Numerics
{
    namespace Diffusion
    {
        // Dummy struct for no diffusion
        struct NoDiffusionIntegrator
        {
            static void integrate(amr::Block&, const auto&, const Grid&, const SimConfig&, double, double, const auto&)
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

            const std::string integrator_type = config.physics.diffusion.integrator;
            using namespace arch::dispatch;
            const auto selected = parse_registered_policy<DiffusionIntegratorPolicies>(
                integrator_type);
            if (!selected.ok || selected.value == DiffusionIntegratorId::None)
            {
                std::cerr << "[Fatal Error] Unknown diffusion integrator: " << integrator_type
                          << ". Check your configuration file." << std::endl;
                std::exit(EXIT_FAILURE);
            }
            visit_policy<DiffusionIntegratorPolicies>(selected.value, [&]<class Registration> {
                using Binding = typename PolicyRegistration<Registration>::CpuBinding;
                if constexpr (std::is_same_v<Binding, CpuRkl1Binding>) {
                    RKL1TimeIntegrator integrator;
                    next_step(integrator);
                } else if constexpr (std::is_same_v<Binding, CpuRkl2Binding>) {
                    RKL2TimeIntegrator integrator;
                    next_step(integrator);
                }
            });
        }

        template <typename NextDispatchFunc>
        inline void dispatch_diffusion(
            const SimConfig& config,
            arch::dispatch::DiffusionIntegratorId selected_id,
            NextDispatchFunc&& next_step)
        {
            using namespace arch::dispatch;
            if (!config.physics.diffusion.use_diffusion) {
                if (selected_id != DiffusionIntegratorId::None)
                    throw std::logic_error(
                        "disabled diffusion received an active plan");
                NoDiffusionIntegrator integrator;
                next_step(integrator);
                return;
            }
            if (selected_id == DiffusionIntegratorId::None)
                throw std::logic_error(
                    "enabled diffusion received an inactive plan");
            if (!visit_policy<DiffusionIntegratorPolicies>(
                    selected_id, [&]<class Registration> {
                        using Binding = typename PolicyRegistration<
                            Registration>::CpuBinding;
                        if constexpr (std::is_same_v<Binding, CpuRkl1Binding>) {
                            RKL1TimeIntegrator integrator;
                            next_step(integrator);
                        } else if constexpr (std::is_same_v<Binding,
                                                           CpuRkl2Binding>) {
                            RKL2TimeIntegrator integrator;
                            next_step(integrator);
                        }
                    })) {
                throw std::logic_error(
                    "resolved diffusion integrator has no CPU binding");
            }
        }

    } // namespace Diffusion
} // namespace Numerics
