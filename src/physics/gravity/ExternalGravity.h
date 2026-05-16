
/**
 * ExternalGravity.h
 * @brief Implements an external gravity model for the simulation.
 * This model applies a constant gravitational field throughout the domain.
 */

#pragma once
#include "../../data/FluidState.h"
#include "../../grid/Grid.h"

struct ExternalGravity
{
    double g_x, g_y, g_z;

    ExternalGravity(double gx, double gy, double gz) : g_x(gx), g_y(gy), g_z(gz) {}

    // 常数引力无需更新
    inline void update_field(const FluidState &state, const Grid &grid) {}

    // 内联返回常数
    inline void get_gravity(int i, int j, int k, double &gx, double &gy, double &gz) const
    {
        gx = g_x;
        gy = g_y;
        gz = g_z;
    }
};