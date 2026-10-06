/**
 * @file ResourceEstimates.h
 * @brief Declare conservative preview resource calculations.
 *
 * Workflow:
 * 1. Accept a bounded, verified request at the read-only API boundary.
 * 2. Declare conservative preview resource calculations.
 * 3. Return typed evidence or an explicit error; do not start the simulation Driver.
 */

#pragma once

#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

#include "api/Configuration.h"
#include "amr/topology/Morton.h"
#include "driver/dispatch/PolicyDescriptor.h"

namespace arch::api {
/** Count active root blocks without overflowing before a mesh allocation. */
inline std::optional<std::int64_t> RootBlockCount(int dimension, const std::array<int, 3>& blocks) {
    if (dimension < 1 || dimension > 3) return {};
    std::int64_t count = 1;
    for (int axis = 0; axis < dimension; ++axis) {
        if (blocks[axis] <= 0
            || count > std::numeric_limits<std::int64_t>::max() / blocks[axis]) return {};
        count *= blocks[axis];
    }
    return count;
}

/** Validate active-axis topology and capacity before any initial mesh construction. */
inline void ValidateInitialPreviewGrid(const GridConfig& g, const AmrConfig& amr_config) {
    if (g.dim < 1 || g.dim > 3)
        throw std::invalid_argument("Initial preview dimension must be 1, 2 or 3");
    if (amr_config.lrefinemin < 0 || amr_config.lrefinemax < amr_config.lrefinemin
        || amr_config.lrefinemax > amr::kMaxRefinementLevel)
        throw std::invalid_argument("AMR levels must satisfy 0 <= lrefinemin <= lrefinemax <= 15");
    const std::array<int, 3> blocks{g.nblockx1, g.nblockx2, g.nblockx3};
    const double lo[] = {g.x1_min, g.x2_min, g.x3_min};
    const double hi[] = {g.x1_max, g.x2_max, g.x3_max};
    const std::string lower[] = {g.x1l_boundary_type, g.x2l_boundary_type, g.x3l_boundary_type};
    const std::string upper[] = {g.x1r_boundary_type, g.x2r_boundary_type, g.x3r_boundary_type};
    for (int axis = 0; axis < g.dim; ++axis) {
        if (blocks[axis] <= 0 || !std::isfinite(lo[axis]) || !std::isfinite(hi[axis])
            || !(hi[axis] > lo[axis]) || !std::isfinite(hi[axis] - lo[axis]))
            throw std::invalid_argument("Active axis bounds must be finite and ordered, with positive root blocks");
        const std::uint64_t extent = std::uint64_t(blocks[axis]) << amr_config.lrefinemax;
        if (extent - 1 > amr::kMortonCoordinateMask)
            throw std::invalid_argument("AMR root extent exceeds the supported coordinate range");
        if (!dispatch::parse_boundary(lower[axis]).ok || !dispatch::parse_boundary(upper[axis]).ok)
            throw std::invalid_argument("Unsupported active-axis boundary type");
    }
    const auto roots = RootBlockCount(g.dim, blocks);
    const int capacity = g.amr_max_blocks > 0 ? g.amr_max_blocks : 10000;
    if (!roots || *roots > capacity)
        throw std::invalid_argument("max_blocks cannot hold the configured root blocks");
}

std::int64_t PaddedCells(int dimension);
detail::Json AmrResourceMetadata(const SimConfig&, int species_count = -1);
PreviewResponse EstimateAmrResources(const PreviewRequest&);
}
