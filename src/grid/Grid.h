/**
 * @file Grid.h
 * @brief Defines the 1D spatial grid topology and geometry.
 * * Manages mesh parameters (size, spacing, boundaries) and provides utilities
 * * to map between array indices and physical coordinates.
 */

#pragma once

#include <cmath>

#include "../data/GlobalDefs.h"

// Grid Setup
struct Grid
{
    // -- Dimensionality --
    int dim; ///< Number of spatial dimensions (1, 2 or 3)

    // -- Grid Dimensions --
    int nx; ///< Number of active physical cells in x
    int ny; ///< Number of active physical cells in y
    int nz; ///< Number of active physical cells in z
    int ng; ///< Number of ghost cells (guard cells) on each side

    // -- Memory Layout Strides --
    int stride_y;   ///< Stride for moving 1 index in y-direction
    int stride_z;   ///< Stride for moving 1 index in z-direction
    int total_size; ///< Total number of cells allocated in memory

    double dx, dy, dz; ///< Spatial step size (cell width)

    // -- Physical Domain Boundaries --
    double x_min, y_min, z_min; ///< Coordinate of the left physical boundary
    double x_max, y_max, z_max; ///< Coordinate of the right physical boundary

    /**
     * @brief Constructor initializes grid parameters and computes cell width (dx).
     */
    Grid(int nx_in, int ny_in, int nz_in, int ng_in,
         double x_min_in, double x_max_in,
         double y_min_in = 0.0, double y_max_in = 0.0,
         double z_min_in = 0.0, double z_max_in = 0.0)
        : nx(nx_in), ny(ny_in), nz(nz_in), ng(ng_in),
          x_min(x_min_in), x_max(x_max_in),
          y_min(y_min_in), y_max(y_max_in),
          z_min(z_min_in), z_max(z_max_in)
    {
        InitializeTopology();
    }

    Grid(const GridConfig &cfg, int ng_required)
        : nx(cfg.nx), ny(cfg.ny > 0 ? cfg.ny : 1), nz(cfg.nz > 0 ? cfg.nz : 1), ng(ng_required),
          x_min(cfg.x_min), x_max(cfg.x_max),
          y_min(cfg.y_min), y_max(cfg.y_max),
          z_min(cfg.z_min), z_max(cfg.z_max)
    {
        InitializeTopology();
    }

private:
    /**
     * @brief Computes dimensions, steps, and memory strides.
     * Centralized to avoid duplicated code in constructors.
     */
    void InitializeTopology()
    {
        // dimension cerirital
        dim = (nz > 1) ? 3 : ((ny > 1) ? 2 : 1);

        // Step Length
        dx = (nx > 0) ? (x_max - x_min) / nx : 0.0;
        dy = (ny > 1) ? (y_max - y_min) / ny : 0.0;
        dz = (nz > 1) ? (z_max - z_min) / nz : 0.0;

        // Total length including NG cells
        int total_x = nx + 2 * ng;
        int total_y = (dim >= 2) ? ny + 2 * ng : 1;
        int total_z = (dim == 3) ? nz + 2 * ng : 1;

        // Calculate strides
        stride_y = total_x;
        stride_z = total_x * total_y;
        total_size = total_x * total_y * total_z;
    }

public:
    /**
     * @brief Core 1D mapping: converts 3D (i, j, k) indices to flattened 1D array index.
     * Provides default arguments so GetIndex(i) still works for 1D.
     */
    int GetIndex(int i, int j = 0, int k = 0) const
    {
        return k * stride_z + j * stride_y + i;
    }

    /**
     * @brief Returns the total allocation size required for data arrays.
     * Includes both physical cells and ghost cells on both sides.
     */
    int GetTotalSize() const { return total_size; }

    /**
     * @brief Computes the physical coordinate (center) of a cell given its global index.
     * @param i The global index in the data array (0 to TotalSize-1).
     * @return The physical x-coordinate.
     */
    double GetCellCenterX(int i) const
    {
        // Formula: x_min + (local_index * dx) + half_cell
        return x_min + (i - ng) * dx + 0.5 * dx;
    }
    double GetCellCenterY(int j) const
    {
        if (dim < 2)
            return 0.0;
        return y_min + (j - ng) * dy + 0.5 * dy;
    }

    double GetCellCenterZ(int k) const
    {
        if (dim < 3)
            return 0.0;
        return z_min + (k - ng) * dz + 0.5 * dz;
    }
    // -- Loop Bounds for Physical Domain --

    // X-direction bounds
    int Is() const { return ng; }
    int Ie() const { return nx + ng; }

    // Y-direction bounds (if 1D, loop will run exactly once: from 0 to 1)
    int Js() const { return (dim >= 2) ? ng : 0; }
    int Je() const { return (dim >= 2) ? ny + ng : 1; }

    // Z-direction bounds
    int Ks() const { return (dim == 3) ? ng : 0; }
    int Ke() const { return (dim == 3) ? nz + ng : 1; }
};
