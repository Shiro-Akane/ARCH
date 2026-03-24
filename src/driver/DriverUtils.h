/**
 * @file DeriverUtils.h
 * @brief Utility functions for boundary enforcement and time-step control.
 * * Provides essential support for the main driver loop, specifically:
 * * 1. Populating ghost cells to enforce boundary conditions (e.g., Outflow).
 * * 2. Computing the adaptive time step (dt) based on the CFL stability criterion.
 */

#pragma once

#include "../data/FluidState.h"

#include "../core/RuntimeParams.h"

#include "../grid/Grid.h"

/**
 * @brief Applies boundary conditions to the fluid state.
 * Currently implements a Zero-Gradient (Outflow/Neumann) boundary condition
 * where ghost cells simply copy the values of the nearest active cell.
 *
 * @param state The fluid state container (conservative variables).
 * @param grid  The grid topology information.
 */
void apply_boundary_conditions(FluidState &state, const Grid &grid, const SimConfig &cfg)
{
    // Define the indices of the first and last active physical cells
    int n_species = state.GetNumSpecies();

    // Helper lambda to copy all conservative variables from a source index to a destination index
    auto copy_cell = [&](int src, int dst)
    {
        state.rho[dst] = state.rho[src];
        state.mom_x[dst] = state.mom_x[src];
        state.mom_y[dst] = state.mom_y[src];
        state.mom_z[dst] = state.mom_z[src];
        state.eng[dst] = state.eng[src];
        for (int k = 0; k < n_species; ++k)
        {
            state.Y(k, dst) = state.Y(k, src);
        }
    };

    auto reflect_cell = [&](int src, int dst, int dir)
    {
        copy_cell(src, dst);
        if (dir == 0)
            state.mom_x[dst] = -state.mom_x[dst]; // X-reflect
        if (dir == 1)
            state.mom_y[dst] = -state.mom_y[dst]; // Y-reflect
        if (dir == 2)
            state.mom_z[dst] = -state.mom_z[dst]; // Z-reflect
    };

    int ng = grid.ng;

    // =========================================================
    // 1. X-Direction Boundaries
    // =========================================================
    for (int k = grid.Ks(); k < grid.Ke(); ++k)
    {
        for (int j = grid.Js(); j < grid.Je(); ++j)
        {
            int i_start = grid.Is();
            int i_end = grid.Ie() - 1;

            // Left X
            for (int g = 1; g <= ng; g++)
            {
                int dst = grid.GetIndex(i_start - g, j, k);
                if (cfg.grid.xl_boundary_type == "periodic")
                    copy_cell(grid.GetIndex(i_end - g + 1, j, k), dst);
                else if (cfg.grid.xl_boundary_type == "reflect")
                    reflect_cell(grid.GetIndex(i_start + g - 1, j, k), dst, 0);
                else
                    copy_cell(grid.GetIndex(i_start, j, k), dst); // outflow
            }
            // Right X
            for (int g = 1; g <= ng; g++)
            {
                int dst = grid.GetIndex(i_end + g, j, k);
                if (cfg.grid.xr_boundary_type == "periodic")
                    copy_cell(grid.GetIndex(i_start + g - 1, j, k), dst);
                else if (cfg.grid.xr_boundary_type == "reflect")
                    reflect_cell(grid.GetIndex(i_end - g + 1, j, k), dst, 0);
                else
                    copy_cell(grid.GetIndex(i_end, j, k), dst); // outflow
            }
        }
    }

    // =========================================================
    // 2. Y-Direction Boundaries (Skips if 1D)
    // Note: Loop over full X range (0 to nx+2ng) to fill corners!
    // =========================================================
    if (grid.dim >= 2)
    {
        for (int k = grid.Ks(); k < grid.Ke(); ++k)
        {
            for (int i = 0; i < grid.nx + 2 * ng; ++i)
            { // Full X
                int j_start = grid.Js();
                int j_end = grid.Je() - 1;

                // Bottom Y
                for (int g = 1; g <= ng; g++)
                {
                    int dst = grid.GetIndex(i, j_start - g, k);
                    if (cfg.grid.yl_boundary_type == "periodic")
                        copy_cell(grid.GetIndex(i, j_end - g + 1, k), dst);
                    else if (cfg.grid.yl_boundary_type == "reflect")
                        reflect_cell(grid.GetIndex(i, j_start + g - 1, k), dst, 1);
                    else
                        copy_cell(grid.GetIndex(i, j_start, k), dst);
                }
                // Top Y
                for (int g = 1; g <= ng; g++)
                {
                    int dst = grid.GetIndex(i, j_end + g, k);
                    if (cfg.grid.yr_boundary_type == "periodic")
                        copy_cell(grid.GetIndex(i, j_start + g - 1, k), dst);
                    else if (cfg.grid.yr_boundary_type == "reflect")
                        reflect_cell(grid.GetIndex(i, j_end - g + 1, k), dst, 1);
                    else
                        copy_cell(grid.GetIndex(i, j_end, k), dst);
                }
            }
        }
    }

    // =========================================================
    // 3. Z-Direction Boundaries (Skips if 1D/2D)
    // Note: Loop over full X and Y ranges to fill 3D corners!
    // =========================================================
    if (grid.dim == 3)
    {
        for (int j = 0; j < grid.ny + 2 * ng; ++j)
        { // Full Y
            for (int i = 0; i < grid.nx + 2 * ng; ++i)
            { // Full X
                int k_start = grid.Ks();
                int k_end = grid.Ke() - 1;

                // Back Z
                for (int g = 1; g <= ng; g++)
                {
                    int dst = grid.GetIndex(i, j, k_start - g);
                    if (cfg.grid.zl_boundary_type == "periodic")
                        copy_cell(grid.GetIndex(i, j, k_end - g + 1), dst);
                    else if (cfg.grid.zl_boundary_type == "reflect")
                        reflect_cell(grid.GetIndex(i, j, k_start + g - 1), dst, 2);
                    else
                        copy_cell(grid.GetIndex(i, j, k_start), dst);
                }
                // Front Z
                for (int g = 1; g <= ng; g++)
                {
                    int dst = grid.GetIndex(i, j, k_end + g);
                    if (cfg.grid.zr_boundary_type == "periodic")
                        copy_cell(grid.GetIndex(i, j, k_start + g - 1), dst);
                    else if (cfg.grid.zr_boundary_type == "reflect")
                        reflect_cell(grid.GetIndex(i, j, k_end - g + 1), dst, 2);
                    else
                        copy_cell(grid.GetIndex(i, j, k_end), dst);
                }
            }
        }
    }
}

/**
 * @brief Computes adaptive time step (dt) strictly evaluating 3D wave speeds.
 * Uses dt = CFL * min( dx/(|u|+c), dy/(|v|+c), dz/(|w|+c) )
 */
template <typename EosType>
inline double adaptive_dt(const FluidState &state, const EosType &eos, const Grid &grid, double cfl_number)
{
    int n_species = state.GetNumSpecies();
    std::vector<double> Yi_cache(n_species);

    double min_dt = 1e10;

    for (int k = grid.Ks(); k < grid.Ke(); ++k)
    {
        for (int j = grid.Js(); j < grid.Je(); ++j)
        {
            for (int i = grid.Is(); i < grid.Ie(); ++i)
            {
                int idx = grid.GetIndex(i, j, k);
                FluidVector U = state.get(idx);
                double rho = U.rho;

                if (rho < 1e-12)
                    continue;

                for (int s = 0; s < n_species; ++s)
                    Yi_cache[s] = state.Y(s, idx);

                double p = eos.get_pressure(U, Yi_cache.data());
                double c = eos.get_sound_speed(U, p, Yi_cache.data());

                double inv_dt_sum = (std::abs(U.mom_x / rho) + c) / grid.dx;

                if (grid.dim >= 2)
                {
                    inv_dt_sum += (std::abs(U.mom_y / rho) + c) / grid.dy;
                }

                if (grid.dim == 3)
                {
                    inv_dt_sum += (std::abs(U.mom_z / rho) + c) / grid.dz;
                }

                // 当前网格允许的最大安全时间步长
                double cell_dt = 1.0 / std::max(inv_dt_sum, 1e-10);

                min_dt = std::min(min_dt, cell_dt);
            }
        }
    }

    return cfl_number * min_dt;
}