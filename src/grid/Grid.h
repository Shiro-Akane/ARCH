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
    int n1; ///< Number of active physical cells in x1
    int n2; ///< Number of active physical cells in x2
    int n3; ///< Number of active physical cells in x3
    int ng; ///< Number of ghost cells (guard cells) on each side

    // -- Memory Layout Strides --
    int stride_y;   ///< Stride for moving 1 index in y-direction
    int stride_z;   ///< Stride for moving 1 index in z-direction
    int total_size; ///< Total number of cells allocated in memory

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

    Grid(int n1_in, int n2_in, int n3_in, int ng_in,
         double x1_min_in, double x1_max_in,
         double x2_min_in = 0.0, double x2_max_in = 0.0,
         double x3_min_in = 0.0, double x3_max_in = 0.0)
        : n1(n1_in), n2(n2_in), n3(n3_in), ng(ng_in),
          x1_min(x1_min_in), x1_max(x1_max_in),
          x2_min(x2_min_in), x2_max(x2_max_in),
          x3_min(x3_min_in), x3_max(x3_max_in),
          geometry("cartesian")
    {
        InitializeTopology();
    }

    Grid(const GridConfig &cfg, int ng_required)
        : n1(cfg.n1), n2(cfg.n2), n3(cfg.n3), ng(ng_required),
          x1_min(cfg.x1_min), x1_max(cfg.x1_max),
          x2_min(cfg.x2_min), x2_max(cfg.x2_max),
          x3_min(cfg.x3_min), x3_max(cfg.x3_max),
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
        // 0. Dimensionality and topology checks
        if (n1 < 1 || n2 < 1 || n3 < 1)
            throw std::invalid_argument("Grid Error: n1, n2, and n3 must be >= 1. Dimensionality is controlled by setting n_i = 1.");
        if (n2 == 1 && n3 > 1)
            throw std::invalid_argument("Grid Error: Cross-dimensional topology anomaly. n2 == 1 but n3 > 1 is not allowed.");

        // 1. 通用基础校验：Max 必须大于 Min (针对激活的维度)
        if (n1 > 0 && x1_max <= x1_min)
            throw std::invalid_argument("Grid Error: x1_max must be strictly greater than x1_min.");
        if (n2 > 1 && x2_max <= x2_min)
            throw std::invalid_argument("Grid Error: x2_max must be strictly greater than x2_min.");
        if (n3 > 1 && x3_max <= x3_min)
            throw std::invalid_argument("Grid Error: x3_max must be strictly greater than x3_min.");

        // 容差值，防止浮点数精度导致误判 (例如 3.141592653589793 vs M_PI)
        const double eps = 1e-10;

        // 2. 针对特定坐标系的物理域校验
        if (geometry == "spherical")
        {
            if (x1_min < 0.0)
                throw std::invalid_argument("Domain Error: r_min cannot be negative.");

            if (dim == 2)
            {
                // 【核心修改】：2D下，回退为极坐标 (r, phi)，允许 2pi
                if ((x2_max - x2_min) > 2.0 * M_PI + eps)
                    throw std::invalid_argument("Domain Error (2D Polar): Azimuthal angle phi (y bounds) cannot exceed 2*pi.");
            }
            else if (dim == 3)
            {
                // 3D下，y是theta (0到pi)，z是phi (0到2pi)
                if (x2_min < -eps || x2_max > M_PI + eps)
                    throw std::invalid_argument("Domain Error (Spherical): Polar angle theta (y bounds) must be within [0, pi].");
                if ((x3_max - x3_min) > 2.0 * M_PI + eps)
                    throw std::invalid_argument("Domain Error (Spherical): Azimuthal angle phi range cannot exceed 2*pi.");
            }
        }
        else if (geometry == "cylindrical")
        {
            if (x1_min < 0.0)
                throw std::invalid_argument("Domain Error: R_min cannot be negative.");

            // 同样，2D下 y 变为 phi，允许 2pi
            if (dim == 2)
            {
                if ((x2_max - x2_min) > 2.0 * M_PI + eps)
                    throw std::invalid_argument("Domain Error (2D Polar): Azimuthal angle phi (y bounds) cannot exceed 2*pi.");
            }
            else if (dim == 3)
            {
                if ((x3_max - x3_min) > 2.0 * M_PI + eps)
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
        dim = (n3 > 1) ? 3 : ((n2 > 1) ? 2 : 1);

        // Step Length
        dx1 = (n1 > 0) ? (x1_max - x1_min) / n1 : 0.0;
        dx2 = (n2 > 1) ? (x2_max - x2_min) / n2 : 0.0;
        dx3 = (n3 > 1) ? (x3_max - x3_min) / n3 : 0.0;

        // Total length including NG cells
        int total_x = n1 + 2 * ng;
        int total_y = (dim >= 2) ? n2 + 2 * ng : 1;
        int total_z = (dim == 3) ? n3 + 2 * ng : 1;

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
    int Ie() const { return n1 + ng; }

    // Y-direction bounds (if 1D, loop will run exactly once: from 0 to 1)
    int Js() const { return (dim >= 2) ? ng : 0; }
    int Je() const { return (dim >= 2) ? n2 + ng : 1; }

    // Z-direction bounds
    int Ks() const { return (dim == 3) ? ng : 0; }
    int Ke() const { return (dim == 3) ? n3 + ng : 1; }
};
