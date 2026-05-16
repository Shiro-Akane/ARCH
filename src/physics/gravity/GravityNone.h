/**
 * GravityNone.h
 * @brief Implements a "no gravity" model for the simulation.
 * This is a placeholder that allows the rest of the code to call gravity-related functions
 * without needing to check if gravity is enabled or not.
 * The compiler will optimize away the empty functions and zero source terms, resulting in
 * no performance overhead when gravity is not used.
 */

#pragma once
#include "../../data/FluidState.h"
#include "../../grid/Grid.h"

struct GravityNone
{
    // 默认构造
    GravityNone() = default;

    // 空的更新函数
    inline void update_field(const FluidState &state, const Grid &grid) {}

    // 强制返回 0，编译器会自动把对应的源项加法消除掉
    inline void get_gravity(int i, int j, int k, double &gx, double &gy, double &gz) const
    {
        gx = 0.0;
        gy = 0.0;
        gz = 0.0;
    }
};