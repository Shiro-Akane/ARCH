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
    int i_start = grid.Is();
    int i_end = grid.Ie() - 1;
    int n_species = state.GetNumSpecies();

    // Helper lambda to copy all conservative variables from a source index to a destination index
    auto copy_cell = [&](int src, int dst)
    {
        state.rho[dst] = state.rho[src];
        state.mom[dst] = state.mom[src];
        state.eng[dst] = state.eng[src];
        for (int k = 0; k < n_species; ++k)
        {
            state.Y(k, dst) = state.Y(k, src);
        }
    };

    auto reflect_cell = [&](int src, int dst)
    {
        state.rho[dst] = state.rho[src];
        state.mom[dst] = -state.mom[src]; // <--- 关键：动量反向
        state.eng[dst] = state.eng[src];
        for (int k = 0; k < n_species; ++k)
        {
            state.Y(k, dst) = state.Y(k, src);
        }
    };

    // Left Boundary: Outflow Condition.
    // We iterate through the ghost cells (1 to ng) and copy data from the first active cell (i_start).
    std::string bc_type_L = cfg.grid.xl_boundary_type;

    if (bc_type_L == "periodic")
    {
        // periodic: Ghost(i_start - g) <--- Active(i_end - g + 1)
        for (int g = 1; g <= grid.ng; g++)
        {
            copy_cell(i_end - g + 1, i_start - g);
        }
    }
    else if (bc_type_L == "reflect")
    {
        // Ghost(i_start - g) <--- Active(i_start + g - 1)
        for (int g = 1; g <= grid.ng; g++)
        {
            reflect_cell(i_start + g - 1, i_start - g);
        }
    }
    else // Default: Outflow / Zero Gradient
    {
        for (int g = 1; g <= grid.ng; g++)
        {
            copy_cell(i_start, i_start - g);
        }
    }
    // Right Boundary: Outflow Condition.
    // We iterate through the ghost cells and copy data from the last active cell (i_end).
    std::string bc_type_R = cfg.grid.xr_boundary_type;
    if (bc_type_R == "periodic")
    {
        // Ghost(i_end + g) <--- Active(i_start + g - 1)
        for (int g = 1; g <= grid.ng; g++)
        {
            copy_cell(i_start + g - 1, i_end + g);
        }
    }
    else if (bc_type_R == "reflect")
    { // Ghost(i_end + g) <--- Active(i_end - g + 1)
        for (int g = 1; g <= grid.ng; g++)
        {
            reflect_cell(i_end - g + 1, i_end + g);
        }
    }
    else // Default: Outflow
    {
        for (int g = 1; g <= grid.ng; g++)
        {
            copy_cell(i_end, i_end + g);
        }
    }
}

/**
 * @brief Computes the adaptive time step (dt) based on the CFL condition.
 * Formula: dt = CFL * dx / max(|u| + c)
 *
 * @tparam EosType The Equation of State class type.
 * @param state      The current fluid state.
 * @param eos        The equation of state object for calculating pressure and sound speed.
 * @param grid       The grid topology (provides dx).
 * @param cfl_number The target Courant number (usually < 1.0).
 * @return double    The computed time step size.
 */
template <typename EosType>
double adaptive_dt(const FluidState &state, const EosType &eos, const Grid &grid, double cfl_number)
{
    double umax = 1e-10; // Small epsilon to avoid division by zero if fluid is perfectly still
    int n_species = state.GetNumSpecies();

    // Cache buffer for species mass fractions to pass into EOS
    std::vector<double> Yi_cache(n_species);

    // Loop over all active physical cells
    for (int i = grid.Is(); i < grid.Ie(); i++)
    {
        double rho = state.rho[i];
        if (rho < 1e-12)
            continue;

        double mom = state.mom[i];
        double eng = state.eng[i];

        // Load species mass fractions
        for (int k = 0; k < n_species; ++k)
        {
            Yi_cache[k] = state.Y(k, i);
        }

        // Calculate primitive variables needed for sound speed
        double u = std::abs(mom / rho); // Fluid velocity magnitude

        // Compute thermodynamic properties using EOS
        double p = eos.get_pressure(rho, mom, eng, Yi_cache.data());
        double c = eos.get_sound_speed(rho, p, Yi_cache.data());

        // Max characteristic wave speed (spectral radius): |u| + c
        double lambda = u + c;
        if (lambda > umax)
        {
            umax = lambda;
        }
    }

    // Return the stable time step
    if (umax < 1e-9)
        umax = 1e-9;

    return cfl_number * grid.dx / umax;
}