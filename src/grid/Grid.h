/**
 * @file Grid.h
 * @brief Defines the 1D spatial grid topology and geometry.
 * * Manages mesh parameters (size, spacing, boundaries) and provides utilities
 * * to map between array indices and physical coordinates.
 */

#pragma once

#include <cmath>

// Grid Setup
struct Grid
{
    // -- Grid Dimensions --
    int nx;    ///< Number of active physical cells
    int ng;    ///< Number of ghost cells (guard cells) on each side
    double dx; ///< Spatial step size (cell width)

    // -- Physical Domain Boundaries --
    double x_min; ///< Coordinate of the left physical boundary
    double x_max; ///< Coordinate of the right physical boundary

    /**
     * @brief Constructor initializes grid parameters and computes cell width (dx).
     */
    Grid(int nx_in, int ng_in, double x_min_in, double x_max_in)
        : nx(nx_in), ng(ng_in), x_min(x_min_in), x_max(x_max_in)
    {
        dx = (x_max - x_min) / nx;
    }

    /**
     * @brief Returns the total allocation size required for data arrays.
     * Includes both physical cells and ghost cells on both sides.
     */
    int GetTotalSize() const { return nx + 2 * ng; }

    /**
     * @brief Computes the physical coordinate (center) of a cell given its global index.
     * @param i The global index in the data array (0 to TotalSize-1).
     * @return The physical x-coordinate.
     */
    double GetCellCenter(int i) const
    {
        // Formula: x_min + (local_index * dx) + half_cell
        return x_min + (i - ng) * dx + 0.5 * dx;
    }
    // -- Loop Bounds for Physical Domain --

    /// Returns the starting index of the physical domain (inclusive).
    int Is() const { return ng; }

    /// Returns the ending index of the physical domain (exclusive).
    /// Used for loops like: for (int i = Is(); i < Ie(); ++i)
    int Ie() const { return nx + ng; }
};
