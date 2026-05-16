/**
 * @file GravityDispatch.h
 * @brief Dispatcher for gravity policies.
 * Resolves the gravity type at compile-time and passes the policy to the next stage.
 */

#pragma once

#include <string>
#include "../../data/GlobalDefs.h"
#include "GravityNone.h"
#include "ExternalGravity.h"
// #include "SelfGravityMultigrid.h" // 未来的泊松求解器
// #include "SelfGravityFFT.h"       // 未来的 FFT 求解器

namespace Physical
{
    namespace Gravity
    {

        /**
         * @brief 泛型分发器
         * @tparam NextDispatchFunc 下一个要执行的函数（通常是一个 lambda 表达式）
         */
        template <typename NextDispatchFunc>
        inline void dispatch_gravity(const SimConfig &config, NextDispatchFunc &&next_step)
        {
            std::string grav_type = config.physics.gravity.type;

            if (grav_type == "external" || grav_type == "External")
            {
                ExternalGravity gravity(
                    config.physics.gravity.g_x,
                    config.physics.gravity.g_y,
                    config.physics.gravity.g_z);
                // 执行下一步，并将 gravity 策略注入进去
                next_step(gravity);
            }
            else if (grav_type == "self" || grav_type == "Self")
            {
                // ==========================================
                // 未来的泊松方程求解器在这里分发
                // ==========================================
                // std::string solver = config.physics.gravity.poisson_solver;
                // if (solver == "multigrid") {
                //     SelfGravityMultigrid gravity(config.physics.gravity.G_const, config.physics.gravity.poisson_tol);
                //     next_step(gravity);
                // } else if (solver == "fft") { ... }

                std::cerr << "[Error] Self gravity is not yet implemented!" << std::endl;
                std::abort();
            }
            else
            {
                // 默认情况：无引力 (零开销)
                GravityNone gravity;
                next_step(gravity);
            }
        }

    } // namespace Gravity
} // namespace Physical