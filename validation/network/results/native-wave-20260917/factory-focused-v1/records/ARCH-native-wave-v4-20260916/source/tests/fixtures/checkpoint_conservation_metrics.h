/**
 * @file checkpoint_conservation_metrics.h
 * @brief Integrate conserved checkpoint fields over the AMR leaf cells.
 *
 * Each total is Q = sum_i V_i q_i, where q_i is the stored density of mass,
 * momentum, energy or a species. With run parameters, GridMetrics supplies the
 * physical volume V_i. The parameter-free Cartesian mode instead reports
 * totals normalized to the root-cell volume, using V_i = 2^(-d level_i).
 */
#pragma once

#include "amr/Block.h"
#include "grid/GridMetrics.h"
#include "io/hdf5/HDF5Writer.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace checkpoint_metrics {

struct Totals {
    long double mass = 0.0L;
    long double mom_u = 0.0L;
    long double mom_v = 0.0L;
    long double mom_w = 0.0L;
    long double energy = 0.0L;
    std::vector<long double> species;
};

inline void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

// A version-3 checkpoint stores logical leaves, but not domain bounds or root
// block counts. The actual run's parsed GridConfig is therefore mandatory for
// physical measures. Grid/Block still own coordinate construction and metrics.
inline Grid checkpoint_root_grid(const GridConfig& config)
{
    const int dimension = config.nblockx2 <= 0 ? 1 : config.nblockx3 <= 0 ? 2 : 3;
    require(config.dim == dimension && config.nblockx1 > 0
                && !(config.nblockx2 <= 0 && config.nblockx3 > 0),
            "conservation parameter grid topology is invalid");
    const double bounds[]{config.x1_min, config.x1_max,
                          config.x2_min, config.x2_max,
                          config.x3_min, config.x3_max};
    for (int index = 0; index < 2 * config.dim; ++index)
        require(std::isfinite(bounds[index]), "conservation parameter bounds are nonfinite");
    Grid root(amr::MAX_NG, config.x1_min, config.x1_max,
              config.x2_min, config.x2_max, config.x3_min, config.x3_max,
              config.nblockx1, config.nblockx2, config.nblockx3);
    root.dim = config.dim;
    root.geometry = config.geometry;
    require(GridMetrics::geometry_kind(root) != GridMetrics::Geometry::Unsupported,
            "conservation parameter geometry is unsupported");
    root.InitializeTopology(); // shared domain/geometry validation
    return root;
}

inline Totals compute(const io::CheckpointData& checkpoint,
                      const GridConfig* physical_grid = nullptr)
{
    require(checkpoint.dim >= 1 && checkpoint.dim <= 3
                && !checkpoint.levels.empty() && checkpoint.cells_per_block > 0
                && checkpoint.num_species >= 0,
            "conservation checkpoint layout is invalid");
    require(physical_grid != nullptr || checkpoint.geometry == "cartesian",
            "non-Cartesian conservation metrics require --parameters ACTUAL_RUN.par");
    const std::size_t blocks = checkpoint.levels.size();
    require(checkpoint.cells_per_block <= std::numeric_limits<std::size_t>::max() / blocks,
            "conservation checkpoint cell count overflow");
    const std::size_t cells = blocks * checkpoint.cells_per_block;
    const auto species_count = static_cast<std::size_t>(checkpoint.num_species);
    require(species_count <= std::numeric_limits<std::size_t>::max() / cells,
            "conservation checkpoint species count overflow");
    require(checkpoint.rho.size() == cells && checkpoint.mom_u.size() == cells
                && checkpoint.mom_v.size() == cells && checkpoint.mom_w.size() == cells
                && checkpoint.eng.size() == cells
                && checkpoint.rhoX.size() == species_count * cells,
            "conservation checkpoint field shape drifted");
    Grid root;
    double root_dx1 = 0.0, root_dx2 = 0.0, root_dx3 = 0.0;
    if (physical_grid != nullptr) {
        require(checkpoint.geometry == physical_grid->geometry && checkpoint.dim == physical_grid->dim,
                "conservation checkpoint/parameter geometry or dimension mismatch");
        root = checkpoint_root_grid(*physical_grid);
        const std::size_t local_cells = static_cast<std::size_t>(amr::BLOCK_NX)
            * (checkpoint.dim >= 2 ? amr::BLOCK_NY : 1)
            * (checkpoint.dim == 3 ? amr::BLOCK_NZ : 1);
        require(checkpoint.cells_per_block == local_cells
                    && checkpoint.logical_x1.size() == blocks
                    && checkpoint.logical_x2.size() == blocks
                    && checkpoint.logical_x3.size() == blocks,
                "conservation checkpoint block layout is unsupported");
        // Layout reconstruction from the supplied domain, exactly as AmrTree's
        // root spacing. Curvature, child bounds, and volumes are not rederived.
        root_dx1 = (root.x1_max - root.x1_min) / (static_cast<double>(root.nblockx1) * amr::BLOCK_NX);
        root_dx2 = root.nblockx2 > 0
            ? (root.x2_max - root.x2_min) / (static_cast<double>(root.nblockx2) * amr::BLOCK_NY) : 0.0;
        root_dx3 = root.nblockx3 > 0
            ? (root.x3_max - root.x3_min) / (static_cast<double>(root.nblockx3) * amr::BLOCK_NZ) : 0.0;
    }
    Totals result;
    result.species.resize(species_count, 0.0L);
    for (std::size_t block = 0; block < blocks; ++block) {
        require(checkpoint.levels[block] >= 0 && checkpoint.levels[block] <= amr::kMaxRefinementLevel,
                "conservation checkpoint level is invalid");
        amr::Block geometry_block;
        if (physical_grid != nullptr) {
            const std::uint64_t scale = std::uint64_t{1} << checkpoint.levels[block];
            const std::uint32_t coordinates[]{checkpoint.logical_x1[block],
                checkpoint.logical_x2[block], checkpoint.logical_x3[block]};
            const int root_blocks[]{root.nblockx1, root.nblockx2, root.nblockx3};
            for (int direction = 0; direction < 3; ++direction)
                require(direction < checkpoint.dim
                            ? coordinates[direction] < static_cast<std::uint64_t>(root_blocks[direction]) * scale
                            : coordinates[direction] == 0,
                        "conservation checkpoint logical block lies outside parameter domain");
            geometry_block.level = checkpoint.levels[block];
            geometry_block.logical_x1 = coordinates[0];
            geometry_block.logical_x2 = coordinates[1];
            geometry_block.logical_x3 = coordinates[2];
            geometry_block.InitGeometry(root, root_dx1, root_dx2, root_dx3);
        }
        for (std::size_t local = 0; local < checkpoint.cells_per_block; ++local) {
            // Without domain parameters, Cartesian totals use root-cell units.
            // Parameter-bound runs use the shared physical cell measure,
            // including the two-dimensional polar convention.
            long double measure = std::ldexp(1.0L, -checkpoint.dim * checkpoint.levels[block]);
            if (physical_grid != nullptr) {
                const int i = static_cast<int>(local % amr::BLOCK_NX) + amr::MAX_NG;
                const int j = checkpoint.dim >= 2
                    ? static_cast<int>((local / amr::BLOCK_NX) % amr::BLOCK_NY) + amr::MAX_NG : 0;
                const int k = checkpoint.dim == 3
                    ? static_cast<int>(local / (amr::BLOCK_NX * amr::BLOCK_NY)) + amr::MAX_NG : 0;
                measure = GridMetrics::CellVolume(geometry_block.grid, i, j, k);
            }
            require(std::isfinite(measure) && measure > 0.0L,
                    "conservation checkpoint cell volume is invalid");
            const std::size_t index = block * checkpoint.cells_per_block + local;
            result.mass += measure * checkpoint.rho[index];
            result.mom_u += measure * checkpoint.mom_u[index];
            result.mom_v += measure * checkpoint.mom_v[index];
            result.mom_w += measure * checkpoint.mom_w[index];
            result.energy += measure * checkpoint.eng[index];
            for (std::size_t component = 0; component < species_count; ++component)
                result.species[component] += measure * checkpoint.rhoX[component * cells + index];
        }
    }
    return result;
}

} // namespace checkpoint_metrics
