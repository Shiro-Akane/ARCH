/**
 * @file Grid.h
 * @brief Block-local grid extents, native coordinates, and padded array indexing.
 *
 * Three-dimensional native axes are Cartesian (x,y,z), cylindrical (R,z,phi),
 * or spherical (r,theta,phi). Both curved two-dimensional grids use the polar
 * plane (r,phi); angular coordinates are in radians. GridMetrics owns physical
 * volumes, face areas, and orthonormal spacing derived from this geometry.
 *
 * Array offsets are i + j*stride_y + k*stride_z. Axis indices include ghosts;
 * inactive dimensions retain one storage cell rather than ghost planes.
 */

#pragma once

#include <cmath>
#include <limits>
#include "physics/constant/PhysicalConstants.h"
#include <stdexcept>
#include <string>
#include <vector>

#include "amr/topology/AmrDefines.h"
#include "data/GlobalDefs.h"
#include "grid/GridGeometryView.h"

// One physical point expressed in each supported coordinate system.
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

    // Native coordinate system used by coordinate conversion and metric views.
    std::string geometry = "cartesian"; ///< "cartesian", "cylindrical", "spherical"

    // Value-only actual native AMR generation provenance; ordinary grids stay unbound.
    GridMetrics::DyadicGridIdentity dyadic_identity{};

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
    void RequireGeometrySemantics(GridMetrics::GeometrySemantics semantics) const
    {
        if (semantics == GridMetrics::GeometrySemantics::Existing) return;
        if (semantics != GridMetrics::GeometrySemantics::AxisymmetricRz ||
            dim != 2 || geometry != "cylindrical")
            throw std::invalid_argument("RZ coordinates require cylindrical dimension 2");
    }
    void ValidateDomain(GridMetrics::GeometrySemantics semantics) const
    {
        RequireGeometrySemantics(semantics);
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

            // Legacy 2D is polar; explicit RZ x2 is an unrestricted length.
            if (dim == 2 && semantics == GridMetrics::GeometrySemantics::Existing)
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
    void InitializeTopology(GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing)
    {
        RequireGeometrySemantics(semantics);
        // Step Length
        dx1 = (amr::BLOCK_NX > 0) ? (x1_max - x1_min) / amr::BLOCK_NX : 0.0;
        dx2 = (amr::BLOCK_NY > 1 && dim >= 2) ? (x2_max - x2_min) / amr::BLOCK_NY : 0.0;
        dx3 = (amr::BLOCK_NZ > 1 && dim == 3) ? (x3_max - x3_min) / amr::BLOCK_NZ : 0.0;

        // Workflow: ordinary spacing arithmetic remains unchanged. A bound
        // native block authenticates canonical endpoints/representative dx;
        // all actual faces, centers and metric widths use the shared root leaf.
        if (dyadic_identity.bound) {
            if (semantics != GridMetrics::GeometrySemantics::AxisymmetricRz
                || nblockx1 != dyadic_identity.root_blocks[0]
                || nblockx2 != dyadic_identity.root_blocks[1]
                || !GridMetrics::matches_identity(dyadic_identity,
                    {x1_min,x2_min},{x1_max,x2_max},{dx1,dx2}))
                throw std::invalid_argument("Native RZ grid generation identity does not match actual bounds/spacing");
        }

        // Total length including NG cells (only for logical info if needed, memory is fixed)
        total_x_ = amr::BLOCK_NX + 2 * ng;
        total_y_ = (dim >= 2) ? amr::BLOCK_NY + 2 * ng : 1;
        total_z_ = (dim == 3) ? amr::BLOCK_NZ + 2 * ng : 1;

        // Calculate strides (USING AMR CONSTANTS)
        stride_y = amr::PAD_NX;
        stride_z = amr::PAD_NX * total_y_;
        total_size = amr::PAD_NX * total_y_ * total_z_;

        // Check Domain
        ValidateDomain(semantics);
        if(dyadic_identity.bound) {
            // Reject every real interior/ghost cell whose canonical faces or
            // midpoint collapse. No nominal dx or floor repairs coordinates.
            for(int axis=0;axis<2;++axis) {
                const int cells=axis==0?amr::BLOCK_NX:amr::BLOCK_NY;
                for(std::int64_t cell=-std::int64_t(ng);cell<cells+std::int64_t(ng);++cell) {
                    double left=0.,right=0.,center=0.,width=0.;
                    if(!GridMetrics::canonical_axis_cell(dyadic_identity,axis,cell,
                        left,right,center,width))
                        throw std::invalid_argument("Native RZ canonical cell faces are not representable");
                }
            }
        }
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
    std::vector<std::string> GetAxisNames(GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing) const
    {
        RequireGeometrySemantics(semantics);
        if (semantics == GridMetrics::GeometrySemantics::AxisymmetricRz)
            return {"r_cy", "z_cy"};
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
    PointCoords GetPhysicalCoords(int i, int j = 0, int k = 0,
        GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing) const
    {
        return PhysicalCoordsFromNative(dim, geometry, GetCellCenterX(i),
                                        GetCellCenterY(j), GetCellCenterZ(k), semantics);
    }

    // Shared coordinate expansion for grid cells and initial-preview samples.
    // This does not allocate a grid or change sampling resolution.
    static PointCoords PhysicalCoordsFromNative(int dim, const std::string &geometry,
                                                double cx, double cy = 0.0, double cz = 0.0,
                                                GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing)
    {
        if (semantics != GridMetrics::GeometrySemantics::Existing) {
            if (semantics != GridMetrics::GeometrySemantics::AxisymmetricRz ||
                dim != 2 || geometry != "cylindrical")
                throw std::invalid_argument("RZ coordinates require cylindrical dimension 2");
            PointCoords rz{};
            rz.x=cx; rz.y=0.; rz.z=cy;
            rz.r_cy=cx; rz.z_cy=cy; rz.phi_cy=0.;
            rz.r=std::hypot(cx,cy); // Spherical radius, never the native r axis.
            rz.theta=std::atan2(cx,cy); rz.phi=0.;
            return rz;
        }
        PointCoords coords{};

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
     * @param i Block-local x1 index, including ghost cells (not a flat offset).
     * @return The physical x-coordinate.
     */
    /** Bound face wrapper; an invalid chart/index returns nonfinite geometry,
     * never an ordinary-grid fallback. Both backends borrow the same leaf.
     */
    double CanonicalFace(int axis,std::int64_t local_face) const
    {
        double result=0.;
        if(geometry!="cylindrical"||dim!=2||!GridMetrics::canonical_axis_face(dyadic_identity,axis,local_face,result))
            return std::numeric_limits<double>::quiet_NaN();
        return result;
    }

    /** Resolve one axial/radial cell endpoint with its represented cell owner.
     * Periodic axial ghosts select both source-domain endpoints together; the
     * upper face of the last alias cell stays root_upper. Descriptor faces use
     * CanonicalFace separately and never wrap their root/block identities.
     */
    double CanonicalCellFace(int axis,std::int64_t local_cell,bool upper) const
    {
        double left=0.,right=0.,middle=0.,width=0.;
        if(geometry!="cylindrical"||dim!=2||!GridMetrics::canonical_axis_cell(dyadic_identity,axis,
            local_cell,left,right,middle,width))return std::numeric_limits<double>::quiet_NaN();
        return upper?right:left;
    }

    /** Bound cell width/center wrapper with actual face representability. */
    double CanonicalCellValue(int axis,std::int64_t local_cell,bool center) const
    {
        double left=0.,right=0.,middle=0.,width=0.;
        if(geometry!="cylindrical"||dim!=2||!GridMetrics::canonical_axis_cell(dyadic_identity,axis,
            local_cell,left,right,middle,width))return std::numeric_limits<double>::quiet_NaN();
        return center?middle:width;
    }

    double GetCellCenterX(int i) const
    {
        // Formula: x1_min + (local_index * dx1) + half_cell
        if(dyadic_identity.bound)
            return CanonicalCellValue(0,std::int64_t(i)-ng,true);
        return x1_min + (i - ng) * dx1 + 0.5 * dx1;
    }
    double GetCellCenterY(int j) const
    {
        if(dyadic_identity.bound)
            return CanonicalCellValue(1,std::int64_t(j)-ng,true);
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
    double GetFacePosL(int i) const
    {
        if(dyadic_identity.bound)return CanonicalFace(0,std::int64_t(i)-ng);
        return x1_min + (i - ng) * dx1;
    }
    /// Right face position of cell i in x-direction (r_{i+1/2})
    double GetFacePosR(int i) const
    {
        if(dyadic_identity.bound)return CanonicalFace(0,std::int64_t(i)-ng+1);
        return x1_min + (i - ng + 1) * dx1;
    }

    /** Axial CELL bounds in the same represented chart as center and width.
     * Paired-periodic ghosts are source-domain aliases; real descriptor faces
     * remain unwrapped. Unbound grids retain their original local formula.
     */
    double GetAxialFacePosL(int j) const
    {
        if(dyadic_identity.bound)return CanonicalCellFace(1,std::int64_t(j)-ng,false);
        return x2_min + (j - ng) * dx2;
    }
    double GetAxialFacePosR(int j) const
    {
        if(dyadic_identity.bound)return CanonicalCellFace(1,std::int64_t(j)-ng,true);
        return x2_min + (j - ng + 1) * dx2;
    }

    /** Actual cell length for finite-volume metrics; dx stays representative. */
    double CellWidth(int axis,int index) const
    {
        if(dyadic_identity.bound)
            return CanonicalCellValue(axis,std::int64_t(index)-ng,false);
        return axis==0?dx1:axis==1?dx2:axis==2?dx3:
            std::numeric_limits<double>::quiet_NaN();
    }

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
