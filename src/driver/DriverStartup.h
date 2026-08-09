/**
 * @file DriverStartup.h
 * @brief Formats AMR startup diagnostics from the canonical runtime configuration.
 *
 * Workflow:
 * 1. Derive per-level logical cell widths from the root-block topology.
 * 2. Label active axes using the configured geometry and dimensionality.
 * 3. Print the complete Level 0..lrefinemax resolution table before initial regridding.
 */

#pragma once

#include <cmath>
#include <iostream>
#include <string>

#include "../amr/AmrDefines.h"
#include "../data/GlobalDefs.h"

namespace DriverStartup {

inline const char* axis_label(const SimConfig& config, int axis)
{
    if (config.grid.geometry == "cartesian") {
        static constexpr const char* cartesian[] = {"x", "y", "z"};
        return cartesian[axis];
    }
    if (axis == 0) return "r";
    if (config.grid.dim == 2) return "theta";
    if (config.grid.geometry == "cylindrical") {
        static constexpr const char* cylindrical[] = {"r", "z", "phi"};
        return cylindrical[axis];
    }
    static constexpr const char* spherical[] = {"r", "theta", "phi"};
    return spherical[axis];
}

/**
 * @brief Prints the refinement-resolution table used by the forthcoming AMR setup.
 *
 * The displayed widths are logical-coordinate widths. In curvilinear geometries,
 * physical arc lengths additionally depend on the local metric and are handled by
 * GridMetrics inside finite-volume operators.
 */
inline void print_amr_resolution_summary(const SimConfig& config)
{
    const double dx1 = (config.grid.x1_max - config.grid.x1_min) /
                       (config.grid.nblockx1 * amr::BLOCK_NX);
    const double dx2 = config.grid.dim >= 2
        ? (config.grid.x2_max - config.grid.x2_min) / (config.grid.nblockx2 * amr::BLOCK_NY)
        : 0.0;
    const double dx3 = config.grid.dim == 3
        ? (config.grid.x3_max - config.grid.x3_min) / (config.grid.nblockx3 * amr::BLOCK_NZ)
        : 0.0;

    std::cout << ">>> AMR Levels  | Max Blocks: " << config.grid.amr_max_blocks
              << " | Finest Level: " << config.amr.lrefinemax << std::endl;
    for (int level = 0; level <= config.amr.lrefinemax; ++level) {
        const double refinement = std::ldexp(1.0, level);
        std::cout << "    Level " << level << "   | dx1(" << axis_label(config, 0)
                  << "): " << dx1 / refinement;
        if (config.grid.dim >= 2) {
            std::cout << ", dx2(" << axis_label(config, 1) << "): "
                      << dx2 / refinement;
        }
        if (config.grid.dim == 3) {
            std::cout << ", dx3(" << axis_label(config, 2) << "): "
                      << dx3 / refinement;
        }
        std::cout << std::endl;
    }
}

} // namespace DriverStartup