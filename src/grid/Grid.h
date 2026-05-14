/**
 * @file Grid.h
 * @brief Defines the 1D spatial grid topology and geometry.
 * * Manages mesh parameters (size, spacing, boundaries) and provides utilities
 * * to map between array indices and physical coordinates.
 */

#pragma once

#include <cmath>
#include <stdexcept>
#include <vector>
#include <string>

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
    std::string geometry = "cartesian"; ///< "cartesian", "cylindrical", "spherical"

    Grid(int nx_in, int ny_in, int nz_in, int ng_in,
         double x_min_in, double x_max_in,
         double y_min_in = 0.0, double y_max_in = 0.0,
         double z_min_in = 0.0, double z_max_in = 0.0)
        : nx(nx_in), ny(ny_in), nz(nz_in), ng(ng_in),
          x_min(x_min_in), x_max(x_max_in),
          y_min(y_min_in), y_max(y_max_in),
          z_min(z_min_in), z_max(z_max_in),
          geometry("cartesian")
    {
        InitializeTopology();
    }

    Grid(const GridConfig &cfg, int ng_required)
        : nx(cfg.nx), ny(cfg.ny > 0 ? cfg.ny : 1), nz(cfg.nz > 0 ? cfg.nz : 1), ng(ng_required),
          x_min(cfg.x_min), x_max(cfg.x_max),
          y_min(cfg.y_min), y_max(cfg.y_max),
          z_min(cfg.z_min), z_max(cfg.z_max),
          geometry(cfg.geometry)
    {
        InitializeTopology();
    }

private:
    /**
     * @brief 严格校验物理域的合法性。违反几何定义的边界将直接导致程序抛出异常终止。
     */
    void ValidateDomain() const
    {
        // 1. 通用基础校验：Max 必须大于 Min (针对激活的维度)
        if (nx > 0 && x_max <= x_min)
            throw std::invalid_argument("Grid Error: x_max must be strictly greater than x_min.");
        if (ny > 1 && y_max <= y_min)
            throw std::invalid_argument("Grid Error: y_max must be strictly greater than y_min.");
        if (nz > 1 && z_max <= z_min)
            throw std::invalid_argument("Grid Error: z_max must be strictly greater than z_min.");

        // 容差值，防止浮点数精度导致误判 (例如 3.141592653589793 vs M_PI)
        const double eps = 1e-10;

        // 2. 针对特定坐标系的物理域校验
        if (geometry == "spherical")
        {
            if (x_min < 0.0)
                throw std::invalid_argument("Domain Error: r_min cannot be negative.");

            if (dim == 2)
            {
                // 【核心修改】：2D下，回退为极坐标 (r, phi)，允许 2pi
                if ((y_max - y_min) > 2.0 * M_PI + eps)
                    throw std::invalid_argument("Domain Error (2D Polar): Azimuthal angle phi (y bounds) cannot exceed 2*pi.");
            }
            else if (dim == 3)
            {
                // 3D下，y是theta (0到pi)，z是phi (0到2pi)
                if (y_min < -eps || y_max > M_PI + eps)
                    throw std::invalid_argument("Domain Error (Spherical): Polar angle theta (y bounds) must be within [0, pi].");
                if ((z_max - z_min) > 2.0 * M_PI + eps)
                    throw std::invalid_argument("Domain Error (Spherical): Azimuthal angle phi range cannot exceed 2*pi.");
            }
        }
        else if (geometry == "cylindrical")
        {
            if (x_min < 0.0)
                throw std::invalid_argument("Domain Error: R_min cannot be negative.");

            // 同样，2D下 y 变为 phi，允许 2pi
            if (dim == 2)
            {
                if ((y_max - y_min) > 2.0 * M_PI + eps)
                    throw std::invalid_argument("Domain Error (2D Polar): Azimuthal angle phi (y bounds) cannot exceed 2*pi.");
            }
            else if (dim == 3)
            {
                if ((z_max - z_min) > 2.0 * M_PI + eps)
                    throw std::invalid_argument("Domain Error (Cylindrical): Azimuthal angle phi (z bounds) cannot exceed 2*pi.");
            }
        }
    }
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

        // Check Domain
        ValidateDomain();
    }

public:
    /**
     * @brief 为 HDF5/XDMF 后处理提供当前网格的物理坐标轴名称
     */
    std::vector<std::string> GetAxisNames() const
    {
        if (geometry == "spherical")
        {
            if (dim == 1)
                return {"r"};
            if (dim == 2)
                return {"r", "phi"}; // 根据你的逻辑，2D退化为极坐标面
            return {"r", "theta", "phi"};
        }
        else if (geometry == "cylindrical")
        {
            if (dim == 1)
                return {"r_cy"};
            if (dim == 2)
                return {"r_cy", "phi_cy"}; // 根据你的逻辑，2D退化为极坐标面
            return {"r_cy", "z_cy", "phi_cy"};
        }

        // 默认 Cartesian 坐标系
        if (dim == 1)
            return {"x"};
        if (dim == 2)
            return {"x", "y"};
        return {"x", "y", "z"};
    }

    /**
     * @brief  获取指定网格单元的全息物理坐标 {x,y,z,r,theta,phi}
     * 无论底层网格是笛卡尔还是球坐标，该函数均提供一致的物理映射。
     */
    PointCoords GetPhysicalCoords(int i, int j = 0, int k = 0) const
    {
        PointCoords coords;
        // 1. 获取网格的逻辑计算坐标 (Logical Compute Coordinates)
        double cx = GetCellCenterX(i);
        double cy = GetCellCenterY(j);
        double cz = GetCellCenterZ(k);

        // 降维情况下的默认值处理 (非常重要：处理 1D 或 2D 模拟)
        if (dim == 1)
        {
            cy = (geometry == "spherical") ? M_PI / 2.0 : 0.0;
        } // 1D球坐标默认在赤道平面
        if (dim <= 2)
        {
            cz = 0.0;
        }

        // 2. 根据当前的计算网格拓扑，计算全息坐标
        if (geometry == "cartesian")
        {
            coords.x = cx;
            coords.y = cy;
            coords.z = cz;

            coords.r = std::sqrt(cx * cx + cy * cy + cz * cz);
            coords.theta = (coords.r > 1e-14) ? std::acos(cz / coords.r) : 0.0;
            coords.phi = std::atan2(cy, cx);

            // 补全柱坐标
            coords.r_cy = std::sqrt(cx * cx + cy * cy);
            coords.phi_cy = coords.phi;
            coords.z_cy = cz;
        }
        else if (geometry == "spherical")
        {
            coords.r = cx;
            // 【核心适配】：当 dim==2 时，退化为极坐标面，cy 代表 phi！
            if (dim == 2)
            {
                coords.theta = M_PI / 2.0; // 锁定在赤道面
                coords.phi = cy;           // 第二维变身为方位角 phi
            }
            else
            {
                coords.theta = cy;
                coords.phi = cz;
            }

            coords.x = coords.r * std::sin(coords.theta) * std::cos(coords.phi);
            coords.y = coords.r * std::sin(coords.theta) * std::sin(coords.phi);
            coords.z = coords.r * std::cos(coords.theta);

            // 补全柱坐标（为了让 Sod.cpp 中的 shape_type=3 能无缝调用）
            coords.r_cy = coords.r * std::sin(coords.theta);
            coords.phi_cy = coords.phi;
            coords.z_cy = coords.z;
        }
        else if (geometry == "cylindrical")
        {
            // 【核心适配】：当 dim==2 时，退化为极坐标面，cy 代表 phi！
            if (dim == 2)
            {
                coords.r_cy = cx;
                coords.phi_cy = cy; // 第二维变身为方位角
                coords.z_cy = 0.0;  // Z平面锁定
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

            // 补全球坐标
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
    /// Left face position of cell i in x-direction (r_{i-1/2})
    double GetFacePosL(int i) const { return x_min + (i - ng) * dx; }
    /// Right face position of cell i in x-direction (r_{i+1/2})
    double GetFacePosR(int i) const { return x_min + (i - ng + 1) * dx; }

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
