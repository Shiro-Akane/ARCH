/**
 * @file DriverUtils.h
 * @brief Utility functions for boundary enforcement and time-step control.
 * Provides essential support for the main driver loop, specifically:
 * 1. Populating ghost cells to enforce boundary conditions (e.g., Outflow).
 * 2. Computing the adaptive time step (dt) based on the CFL stability criterion.
 */

/**
 * Workflow:
 * 1. Select the configured policy and determine a stable macro step.
 * 2. Apply hydro, diffusion, gravity, and burn operators in the documented order.
 * 3. Synchronize AMR leaves and emit diagnostics before continuing the evolution.
 */

#pragma once

#include "../data/FluidState.h"

#include "../core/RuntimeParams.h"

#include "../grid/Grid.h"

/**
 * @brief Applies boundary conditions to the fluid state.
 * Supports outflow, reflecting, and periodic faces. Outflow ghosts copy the
 * nearest active cell; reflecting faces also reverse normal momentum.
 *
 * @param state The fluid state container (conservative variables).
 * @param grid  The grid topology information.
 */
inline void apply_boundary_conditions(FluidState &state, const Grid &grid, const SimConfig &cfg)
{
    // Species follow the same ghost-cell mapping as the conserved fields.
    int n_species = state.GetNumSpecies();

    // Copy one complete cell, including species mass fractions.
    auto copy_cell = [&](int src, int dst)
    {
        state.rho[dst] = state.rho[src];
        state.mom_u[dst] = state.mom_u[src];
        state.mom_v[dst] = state.mom_v[src];
        state.mom_w[dst] = state.mom_w[src];
        state.eng[dst] = state.eng[src];
        for (int k = 0; k < n_species; ++k)
        {
            state.X(k, dst) = state.X(k, src);
        }
    };

    auto reflect_cell = [&](int src, int dst, int dir)
    {
        copy_cell(src, dst);
        if (dir == 0)
            state.mom_u[dst] = -state.mom_u[dst]; // X-reflect
        if (dir == 1)
            state.mom_v[dst] = -state.mom_v[dst]; // Y-reflect
        if (dir == 2)
            state.mom_w[dst] = -state.mom_w[dst]; // Z-reflect
    };

    int ng = grid.ng;

    // 1. X-Direction Boundaries
    const int ks = grid.Ks();
    const int ke = grid.Ke();
    const int js = grid.Js();
    const int je = grid.Je();
    const int nk = ke - ks;
    const int nj = je - js;

#pragma omp parallel
    {
#pragma omp for schedule(static)
        for (int kj = 0; kj < nk * nj; ++kj)
    {
        int k = ks + kj / nj;
        int j = js + kj % nj;
        {
            int i_start = grid.Is();
            int i_end = grid.Ie() - 1;

            // Left X
            for (int g = 1; g <= ng; g++)
            {
                int dst = grid.GetIndex(i_start - g, j, k);
                if (cfg.grid.x1l_boundary_type == "periodic")
                    copy_cell(grid.GetIndex(i_end - g + 1, j, k), dst);
                else if (cfg.grid.x1l_boundary_type == "reflect")
                    reflect_cell(grid.GetIndex(i_start + g - 1, j, k), dst, 0);
                else
                    copy_cell(grid.GetIndex(i_start, j, k), dst); // outflow
            }
            // Right X
            for (int g = 1; g <= ng; g++)
            {
                int dst = grid.GetIndex(i_end + g, j, k);
                if (cfg.grid.x1r_boundary_type == "periodic")
                    copy_cell(grid.GetIndex(i_start + g - 1, j, k), dst);
                else if (cfg.grid.x1r_boundary_type == "reflect")
                    reflect_cell(grid.GetIndex(i_end - g + 1, j, k), dst, 0);
                else
                    copy_cell(grid.GetIndex(i_end, j, k), dst); // outflow
            }
        }
    }

    // 2. Y-direction boundaries. Include X ghost columns so corners are filled.
    if (grid.dim >= 2)
    {
        const int total_x = amr::BLOCK_NX + 2 * ng;
        const int nk2 = ke - ks;

#pragma omp for schedule(static)
        for (int ki = 0; ki < nk2 * total_x; ++ki)
        {
            int k = ks + ki / total_x;
            int i = ki % total_x;
            {
                int j_start = grid.Js();
                int j_end = grid.Je() - 1;

                // Bottom Y
                for (int g = 1; g <= ng; g++)
                {
                    int dst = grid.GetIndex(i, j_start - g, k);
                    if (cfg.grid.x2l_boundary_type == "periodic")
                        copy_cell(grid.GetIndex(i, j_end - g + 1, k), dst);
                    else if (cfg.grid.x2l_boundary_type == "reflect")
                        reflect_cell(grid.GetIndex(i, j_start + g - 1, k), dst, 1);
                    else
                        copy_cell(grid.GetIndex(i, j_start, k), dst);
                }
                // Top Y
                for (int g = 1; g <= ng; g++)
                {
                    int dst = grid.GetIndex(i, j_end + g, k);
                    if (cfg.grid.x2r_boundary_type == "periodic")
                        copy_cell(grid.GetIndex(i, j_start + g - 1, k), dst);
                    else if (cfg.grid.x2r_boundary_type == "reflect")
                        reflect_cell(grid.GetIndex(i, j_end - g + 1, k), dst, 1);
                    else
                        copy_cell(grid.GetIndex(i, j_end, k), dst);
                }
            }
        }
    }

    // 3. Z-direction boundaries. Include X/Y ghost rows to fill 3D corners.
    if (grid.dim == 3)
    {
#pragma omp for schedule(static)
        for (int j = 0; j < amr::BLOCK_NY + 2 * ng; ++j)
        { // Full Y
            for (int i = 0; i < amr::BLOCK_NX + 2 * ng; ++i)
            { // Full X
                int k_start = grid.Ks();
                int k_end = grid.Ke() - 1;

                // Back Z
                for (int g = 1; g <= ng; g++)
                {
                    int dst = grid.GetIndex(i, j, k_start - g);
                    if (cfg.grid.x3l_boundary_type == "periodic")
                        copy_cell(grid.GetIndex(i, j, k_end - g + 1), dst);
                    else if (cfg.grid.x3l_boundary_type == "reflect")
                        reflect_cell(grid.GetIndex(i, j, k_start + g - 1), dst, 2);
                    else
                        copy_cell(grid.GetIndex(i, j, k_start), dst);
                }
                // Front Z
                for (int g = 1; g <= ng; g++)
                {
                    int dst = grid.GetIndex(i, j, k_end + g);
                    if (cfg.grid.x3r_boundary_type == "periodic")
                        copy_cell(grid.GetIndex(i, j, k_start + g - 1), dst);
                    else if (cfg.grid.x3r_boundary_type == "reflect")
                        reflect_cell(grid.GetIndex(i, j, k_end - g + 1), dst, 2);
                    else
                        copy_cell(grid.GetIndex(i, j, k_end), dst);
                }
            }
        }
    }
    }
}

/**
 * @brief Binds SimConfig to the boundary-policy interface used by RK stages.
 */
struct BCHandler
{
    const SimConfig &config;

    void apply(FluidState &state, const Grid &grid) const
    {
        apply_boundary_conditions(state, grid, config);
    }
};

/**
 * @brief Computes adaptive time step (dt) strictly evaluating 3D wave speeds.
 * Uses dt = CFL * min( dx1/(|u|+c), dx2/(|v|+c), dx3/(|w|+c) )
 */
template <typename EosType>
inline double adaptive_dt(const FluidState &state, const EosType &eos, const Grid &grid, double cfl_number)
{
    int n_species = state.GetNumSpecies();
    double min_dt = 1e10;

    const int ks = grid.Ks();
    const int ke = grid.Ke();
    const int js = grid.Js();
    const int je = grid.Je();
    const int nk = ke - ks;
    const int nj = je - js;

#pragma omp parallel
    {
        std::vector<double> Xi_cache(n_species);
        double local_min_dt = 1e10;

#pragma omp for schedule(static)
        for (int kj = 0; kj < nk * nj; ++kj)
        {
            int k = ks + kj / nj;
            int j = js + kj % nj;
            for (int i = grid.Is(); i < grid.Ie(); ++i)
            {
                int idx = grid.GetIndex(i, j, k);
                FluidVector U = state.get(idx);
                double rho = U.rho;

                if (rho < 1e-12)
                    continue;

                for (int s = 0; s < n_species; ++s)
                    Xi_cache[s] = state.X(s, idx);

                double p = eos.get_pressure(U, Xi_cache.data());
                double c = eos.get_sound_speed(U, p, Xi_cache.data());

                double inv_dt_sum = (std::abs(U.mom_u / rho) + c) / grid.dx1;

                if (grid.dim >= 2)
                    inv_dt_sum += (std::abs(U.mom_v / rho) + c) / grid.dx2;

                if (grid.dim == 3)
                    inv_dt_sum += (std::abs(U.mom_w / rho) + c) / grid.dx3;

                double cell_dt = 1.0 / std::max(inv_dt_sum, 1e-10);
                local_min_dt = std::min(local_min_dt, cell_dt);
            }
        }

#pragma omp critical
        {
            min_dt = std::min(min_dt, local_min_dt);
        }
    }

    return cfl_number * min_dt;
}
