/**
 * @file Grid.h
 * @brief Defines the 1D spatial grid topology and geometry.
 * Manages mesh parameters (size, spacing, boundaries) and provides utilities
 * to map between array indices and physical coordinates.
 */

/**
 * Workflow:
 * 1. Derive active logical extents and physical coordinates from RuntimeParams.
 * 2. Expose consistent cell, face, and metric information to numerical operators.
 * 3. Preserve compact 1D/2D storage while retaining the common contiguous pool layout.
 */

#pragma once

#include <cmath>
#include "physics/constant/PhysicalConstants.h"
#include <stdexcept>
#include <string>
#include <vector>

#include "../amr/AmrDefines.h"
#include "../data/GlobalDefs.h"

// -- Global Coord. Sturcture
struct PointCoords
{
    double x, y, z;            // Cartesian
    double r, theta, phi;      // Spherical
    double r_cy, phi_cy, z_cy; // Cylindrical
};

// Grid Setup
struct Grid
{
    // -- Dimensionality --
    int dim; ///< Number of spatial dimensions (1, 2 or 3)

    // -- Global Domain Block Count (Root level) --
    int nblockx1;
    int nblockx2;
    int nblockx3;

    int ng; ///< Number of ghost cells (guard cells) on each side

    // -- Memory Layout Strides --
    int stride_y;   ///< Stride for moving 1 index in y-direction
    int stride_z;   ///< Stride for moving 1 index in z-direction
    int total_size; ///< Total number of cells allocated in memory

    // Set by AmrTree::UpdateNeighbors.  Flux reconstruction uses this to
    // distinguish true coarse-fine interfaces from ordinary patch faces.
    bool amr_coarse_fine_face[6] = {false, false, false, false, false, false};

    double dx1; ///< Spatial step size (cell width in x1)
    double dx2; ///< Spatial step size (cell width in x2)
    double dx3; ///< Spatial step size (cell width in x3)

    // -- Physical Domain Boundaries --
    double x1_min; ///< Coordinate of the left physical boundary (x1)
    double x2_min; ///< Coordinate of the left physical boundary (x2)
    double x3_min; ///< Coordinate of the left physical boundary (x3)
    double x1_max; ///< Coordinate of the right physical boundary (x1)
    double x2_max; ///< Coordinate of the right physical boundary (x2)
    double x3_max; ///< Coordinate of the right physical boundary (x3)

    /**
     * @brief Constructor initializes grid parameters and computes cell width (dx1).
     */
    std::string geometry = "cartesian"; ///< "cartesian", "cylindrical", "spherical"

    Grid() : dim(3), nblockx1(1), nblockx2(1), nblockx3(1), ng(0), x1_min(0), x2_min(0), x3_min(0), x1_max(0), x2_max(0), x3_max(0), geometry("cartesian") {}

    Grid(int ng_in,
         double x1_min_in, double x1_max_in,
         double x2_min_in = 0.0, double x2_max_in = 0.0,
         double x3_min_in = 0.0, double x3_max_in = 0.0,
         int nb1 = 1, int nb2 = 1, int nb3 = 1)
        : ng(ng_in),
          x1_min(x1_min_in), x1_max(x1_max_in),
          x2_min(x2_min_in), x2_max(x2_max_in),
          x3_min(x3_min_in), x3_max(x3_max_in),
          nblockx1(nb1), nblockx2(nb2), nblockx3(nb3),
          geometry("cartesian")
    {
        // dim is expected to be explicitly set before InitializeTopology() is called manually
        // Or InitializeTopology() uses the externally set dim.
    }


private:
    /**
     * @brief Validate physical-domain bounds and reject invalid geometry.
     */
    void ValidateDomain() const
    {
        // 0. Dimensionality and topology checks
        if (amr::BLOCK_NX < 1 || amr::BLOCK_NY < 1 || amr::BLOCK_NZ < 1)
            throw std::invalid_argument("Grid Error: BLOCK dimensions must be >= 1.");
        if (amr::BLOCK_NY == 1 && amr::BLOCK_NZ > 1)
            throw std::invalid_argument("Grid Error: Cross-dimensional topology anomaly. NY == 1 but NZ > 1 is not allowed.");

        // Every active coordinate must have a strictly positive extent.
        if (amr::BLOCK_NX > 0 && x1_max <= x1_min)
            throw std::invalid_argument("Grid Error: x1_max must be strictly greater than x1_min.");
        if (dim >= 2 && amr::BLOCK_NY > 1 && x2_max <= x2_min)
            throw std::invalid_argument("Grid Error: x2_max must be strictly greater than x2_min.");
        if (dim == 3 && amr::BLOCK_NZ > 1 && x3_max <= x3_min)
            throw std::invalid_argument("Grid Error: x3_max must be strictly greater than x3_min.");

        // 1e-10 absorbs decimal-to-binary rounding at angular bounds such as pi.
        const double eps = 1e-10;

        // Apply coordinate-system-specific physical bounds.
        if (geometry == "spherical")
        {
            if (x1_min < 0.0)
                throw std::invalid_argument("Domain Error: r_min cannot be negative.");

            if (dim == 2)
            {
                // Two-dimensional spherical geometry uses the polar (r, phi) plane.
                if ((x2_max - x2_min) > 2.0 * arch::constants::math::pi + eps)
                    throw std::invalid_argument("Domain Error (2D Polar): Azimuthal angle phi (y bounds) cannot exceed 2*pi.");
            }
            else if (dim == 3)
            {
                // In 3D spherical coordinates, x2 is theta in [0,pi] and x3 is phi in [0,2pi].
                if (x2_min < -eps || x2_max > arch::constants::math::pi + eps)
                    throw std::invalid_argument("Domain Error (Spherical): Polar angle theta (y bounds) must be within [0, pi].");
                if ((x3_max - x3_min) > 2.0 * arch::constants::math::pi + eps)
                    throw std::invalid_argument("Domain Error (Spherical): Azimuthal angle phi range cannot exceed 2*pi.");
            }
        }
        else if (geometry == "cylindrical")
        {
            if (x1_min < 0.0)
                throw std::invalid_argument("Domain Error: R_min cannot be negative.");

            // In 2D spherical coordinates, x2 represents phi and may span 2pi.
            if (dim == 2)
            {
                if ((x2_max - x2_min) > 2.0 * arch::constants::math::pi + eps)
                    throw std::invalid_argument("Domain Error (2D Polar): Azimuthal angle phi (y bounds) cannot exceed 2*pi.");
            }
            else if (dim == 3)
            {
                if ((x3_max - x3_min) > 2.0 * arch::constants::math::pi + eps)
                    throw std::invalid_argument("Domain Error (Cylindrical): Azimuthal angle phi (z bounds) cannot exceed 2*pi.");
            }
        }
    }
public:
    /**
     * @brief Computes dimensions, steps, and memory strides.
     * Centralized to avoid duplicated code in constructors.
     */
    void InitializeTopology()
    {
        // Step Length
        dx1 = (amr::BLOCK_NX > 0) ? (x1_max - x1_min) / amr::BLOCK_NX : 0.0;
        dx2 = (amr::BLOCK_NY > 1 && dim >= 2) ? (x2_max - x2_min) / amr::BLOCK_NY : 0.0;
        dx3 = (amr::BLOCK_NZ > 1 && dim == 3) ? (x3_max - x3_min) / amr::BLOCK_NZ : 0.0;

        // Total length including NG cells (only for logical info if needed, memory is fixed)
        total_x_ = amr::BLOCK_NX + 2 * ng;
        total_y_ = (dim >= 2) ? amr::BLOCK_NY + 2 * ng : 1;
        total_z_ = (dim == 3) ? amr::BLOCK_NZ + 2 * ng : 1;

        // Calculate strides (USING AMR CONSTANTS)
        stride_y = amr::PAD_NX;
        stride_z = amr::PAD_NX * total_y_;
        total_size = amr::PAD_NX * total_y_ * total_z_;

        // Check Domain
        ValidateDomain();
    }

    int GetTotalX() const { return total_x_; }
    int GetTotalY() const { return total_y_; }
    int GetTotalZ() const { return total_z_; }

private:
    int total_x_, total_y_, total_z_;
public:
    /**
     * @brief Return physical axis names for HDF5/XDMF post-processing.
     */
    std::vector<std::string> GetAxisNames() const
    {
        if (geometry == "spherical")
        {
            if (dim == 1)
                return {"r"};
            if (dim == 2)
                return {"r", "phi"}; // The 2D spherical case is a polar plane.
            return {"r", "theta", "phi"};
        }
        else if (geometry == "cylindrical")
        {
            if (dim == 1)
                return {"r_cy"};
            if (dim == 2)
                return {"r_cy", "phi_cy"}; // The 2D cylindrical case is a polar plane.
            return {"r_cy", "z_cy", "phi_cy"};
        }

        // Cartesian axis names are the default.
        if (dim == 1)
            return {"x"};
        if (dim == 2)
            return {"x", "y"};
        return {"x", "y", "z"};
    }

    /**
     * @brief Return {x,y,z,r,theta,phi} for one cell center.
     * The result exposes a common physical-coordinate view independent of the
     * grid's native Cartesian, cylindrical, or spherical coordinates.
     */
    PointCoords GetPhysicalCoords(int i, int j = 0, int k = 0) const
    {
        PointCoords coords;
        // Start with coordinates in the grid's native computational system.
        double cx = GetCellCenterX(i);
        double cy = GetCellCenterY(j);
        double cz = GetCellCenterZ(k);

        // Supply deterministic inactive-coordinate values for 1D and 2D grids.
        if (dim == 1)
        {
            cy = (geometry == "spherical") ? arch::constants::math::pi / 2.0 : 0.0;
        } // A 1D spherical radial line is represented in the equatorial plane.
        if (dim <= 2)
        {
            cz = 0.0;
        }

        // Expand native coordinates into the complete physical-coordinate view.
        if (geometry == "cartesian")
        {
            coords.x = cx;
            coords.y = cy;
            coords.z = cz;

            coords.r = std::sqrt(cx * cx + cy * cy + cz * cz);
            coords.theta = (coords.r > 1e-14) ? std::acos(cz / coords.r) : 0.0;
            coords.phi = std::atan2(cy, cx);

            // Derive cylindrical coordinates from Cartesian coordinates.
            coords.r_cy = std::sqrt(cx * cx + cy * cy);
            coords.phi_cy = coords.phi;
            coords.z_cy = cz;
        }
        else if (geometry == "spherical")
        {
            coords.r = cx;
            // In two dimensions, cy is the azimuthal angle phi.
            if (dim == 2)
            {
                coords.theta = arch::constants::math::pi / 2.0; // Two-dimensional spherical grids lie in the equatorial plane.
                coords.phi = cy;           // The second native coordinate is azimuth phi.
            }
            else
            {
                coords.theta = cy;
                coords.phi = cz;
            }

            coords.x = coords.r * std::sin(coords.theta) * std::cos(coords.phi);
            coords.y = coords.r * std::sin(coords.theta) * std::sin(coords.phi);
            coords.z = coords.r * std::cos(coords.theta);

            // Derive cylindrical coordinates for geometry-independent case shapes.
            coords.r_cy = coords.r * std::sin(coords.theta);
            coords.phi_cy = coords.phi;
            coords.z_cy = coords.z;
        }
        else if (geometry == "cylindrical")
        {
            // In two dimensions, cy is the azimuthal angle phi.
            if (dim == 2)
            {
                coords.r_cy = cx;
                coords.phi_cy = cy; // The second native coordinate is azimuth.
                coords.z_cy = 0.0;  // A 2D cylindrical grid lies in the z=0 plane.
            }
            else
            {
                coords.r_cy = cx;
                coords.z_cy = cy;
                coords.phi_cy = cz;
            }

            coords.x = coords.r_cy * std::cos(coords.phi_cy);
            coords.y = coords.r_cy * std::sin(coords.phi_cy);
            coords.z = coords.z_cy;

            // Derive spherical coordinates from cylindrical coordinates.
            coords.r = std::sqrt(coords.r_cy * coords.r_cy + coords.z_cy * coords.z_cy);
            coords.theta = (coords.r > 1e-14) ? std::acos(coords.z_cy / coords.r) : 0.0;
            coords.phi = coords.phi_cy;
        }

        return coords;
    }
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
        // Formula: x1_min + (local_index * dx1) + half_cell
        return x1_min + (i - ng) * dx1 + 0.5 * dx1;
    }
    double GetCellCenterY(int j) const
    {
        if (dim < 2)
            return 0.0;
        return x2_min + (j - ng) * dx2 + 0.5 * dx2;
    }

    double GetCellCenterZ(int k) const
    {
        if (dim < 3)
            return 0.0;
        return x3_min + (k - ng) * dx3 + 0.5 * dx3;
    }
    /// Left face position of cell i in x-direction (r_{i-1/2})
    double GetFacePosL(int i) const { return x1_min + (i - ng) * dx1; }
    /// Right face position of cell i in x-direction (r_{i+1/2})
    double GetFacePosR(int i) const { return x1_min + (i - ng + 1) * dx1; }

    // -- Loop Bounds for Physical Domain --

    // X-direction bounds
    int Is() const { return ng; }
    int Ie() const { return amr::BLOCK_NX + ng; }

    // Y-direction bounds (if 1D, loop will run exactly once: from 0 to 1)
    int Js() const { return (dim >= 2) ? ng : 0; }
    int Je() const { return (dim >= 2) ? amr::BLOCK_NY + ng : 1; }

    // Z-direction bounds
    int Ks() const { return (dim == 3) ? ng : 0; }
    int Ke() const { return (dim == 3) ? amr::BLOCK_NZ + ng : 1; }
};
