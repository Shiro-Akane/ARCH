/**
 * @file CoordinateSeamPlan.h
 * @brief Map zero-area polar/axis ghosts to the same physical point in another chart.
 *
 * Workflow:
 * 1. Inspect committed leaf geometry for an origin, cylindrical axis, or
 *    spherical pole on a full-azimuth domain.
 * 2. Map each singular-face ghost center through the regular coordinate chart
 *    and locate its active AMR donor. Record native-vector basis signs.
 * 3. On every Host stage, reconstruct conserved donor fields at that center
 *    and fill only singular-face ghosts after ordinary exchange. No source
 *    cell, active cell, or zero-area face flux changes ownership.
 *
 * This plan is independent of self-gravity: Hydro, diffusion, and regridding
 * consume the same ghost state. CUDA acceptance of curved coordinates remains
 * a separate stage.
 */
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "amr/storage/Block.h"
#include "amr/storage/MemoryPool.h"
#include "amr/transfer/RegridTransferMath.h"
#include "numerics/state/StateAdmissibility.h"

namespace amr {

struct CoordinateSeamTransfer {
    int source_id = -1;
    int destination_id = -1;
    int source_center = -1;
    int destination_cell = -1;
    std::array<int, 3> source_neighbor{};
    std::array<double, 3> neighbor_weight{};
    std::array<std::int8_t, 3> momentum_sign{1, 1, 1};
};

struct CoordinateSeamPlan {
    std::vector<CoordinateSeamTransfer> transfers;
};

namespace seam_detail {

/** Bring a ghost center into the regular chart and record native basis flips. */
inline std::array<double, 3> regular_position(
    std::array<double, 3> point, int dimension, bool spherical,
    double phi_lower, double phi_width, std::array<std::int8_t, 3>& signs)
{
    const double pi = std::acos(-1.);
    const int azimuth = dimension - 1;
    // Across r=0: (r,phi) -> (-r,phi+pi); for a sphere also
    // (theta,phi) -> (pi-theta,phi+pi). Basis signs follow those maps.
    if (point[0] < 0.) {
        point[0] = -point[0];
        point[azimuth] += pi;
        signs[0] = -signs[0];
        signs[azimuth] = -signs[azimuth];
        if (spherical && dimension == 3) point[1] = pi - point[1];
    }
    if (spherical && dimension == 3) {
        // Across either pole: theta -> -theta or 2*pi-theta,
        // phi -> phi+pi; e_theta and e_phi reverse orientation.
        if (point[1] < 0.) {
            point[1] = -point[1];
            point[2] += pi;
            signs[1] = -signs[1];
            signs[2] = -signs[2];
        } else if (point[1] > pi) {
            point[1] = 2. * pi - point[1];
            point[2] += pi;
            signs[1] = -signs[1];
            signs[2] = -signs[2];
        }
    }
    point[azimuth] = phi_lower
        + std::fmod(std::fmod(point[azimuth] - phi_lower, phi_width)
                    + phi_width, phi_width);
    return point;
}

/** Choose an active donor leaf at the mapped center, regardless of AMR level. */
inline int locate_donor(
    const std::map<std::tuple<int, int, int, int>, int>& leaves,
    std::array<double, 3> point, const std::array<double, 3>& lower,
    const std::array<double, 3>& upper,
    const std::array<int, 3>& root_blocks, int dimension, int max_level)
{
    for (int axis = 0; axis < dimension; ++axis) {
        if (!(point[axis] >= lower[axis] && point[axis] < upper[axis]))
            throw std::invalid_argument("Coordinate seam donor lies outside the physical domain");
    }
    for (int level = max_level; level >= 0; --level) {
        std::array<int, 3> key{};
        for (int axis = 0; axis < dimension; ++axis) {
            const int extent = root_blocks[axis] << level;
            const double fraction = (point[axis] - lower[axis])
                / (upper[axis] - lower[axis]);
            key[axis] = std::clamp(static_cast<int>(fraction * extent),
                                   0, extent - 1);
        }
        const auto found = leaves.find({level, key[0], key[1], key[2]});
        if (found != leaves.end()) return found->second;
    }
    throw std::invalid_argument("Coordinate seam has no active AMR donor");
}

/** Lower the donor center and three one-sided slopes to block-local storage. */
inline void donor_stencil(const Grid& grid,
    const std::array<double, 3>& point, CoordinateSeamTransfer& transfer)
{
    const std::array<double, 3> lower{grid.x1_min, grid.x2_min, grid.x3_min};
    const std::array<double, 3> step{grid.dx1, grid.dx2, grid.dx3};
    const std::array<int, 3> origin{grid.Is(), grid.Js(), grid.Ks()};
    const std::array<int, 3> extent{BLOCK_NX,
        grid.dim >= 2 ? BLOCK_NY : 1, grid.dim == 3 ? BLOCK_NZ : 1};
    std::array<int, 3> center=origin;
    std::array<int, 3> neighbor=origin;
    for (int axis = 0; axis < grid.dim; ++axis) {
        const double logical = (point[axis] - lower[axis]) / step[axis] - 0.5;
        const int local = std::clamp(static_cast<int>(std::floor(logical + 0.5)),
                                     0, extent[axis] - 1);
        center[axis] = origin[axis] + local;
        const double displacement = logical - local;
        int adjacent = local + (displacement < 0. ? -1 : 1);
        if (adjacent < 0 || adjacent >= extent[axis])
            adjacent = local + (displacement < 0. ? 1 : -1);
        neighbor[axis] = origin[axis] + adjacent;
        transfer.neighbor_weight[axis] = std::abs(displacement) < 1e-12
            ? 0. : displacement / static_cast<double>(adjacent - local);
    }
    transfer.source_center = grid.GetIndex(center[0], center[1], center[2]);
    for (int axis = 0; axis < 3; ++axis) {
        auto cell = center;
        cell[axis] = neighbor[axis];
        transfer.source_neighbor[axis] = grid.GetIndex(cell[0], cell[1], cell[2]);
    }
}

} // namespace seam_detail

/** Build immutable singular-face operations from the current active hierarchy. */
inline CoordinateSeamPlan make_coordinate_seam_plan(
    const std::shared_ptr<MemoryPool>& pool,
    std::span<const int> active_blocks, int dimension)
{
    CoordinateSeamPlan plan;
    if (active_blocks.empty() || dimension < 2 || dimension > 3) return plan;
    const Grid& first = pool->GetBlock(active_blocks.front()).grid;
    if (first.geometry != "cylindrical" && first.geometry != "spherical")
        return plan;
    const bool spherical = first.geometry == "spherical";
    const double pi = std::acos(-1.);
    std::array<double, 3> lower{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()};
    std::array<double, 3> upper{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()};
    std::array<int, 3> root_blocks{first.nblockx1, first.nblockx2,
                                    first.nblockx3};
    int max_level = 0;
    for (int id : active_blocks) {
        const Block& block = pool->GetBlock(id);
        const Grid& grid = block.grid;
        if (grid.geometry != first.geometry || grid.dim != dimension)
            throw std::invalid_argument("Coordinate seam blocks disagree on geometry");
        lower[0] = std::min(lower[0], grid.x1_min);
        upper[0] = std::max(upper[0], grid.x1_max);
        lower[1] = std::min(lower[1], grid.x2_min);
        upper[1] = std::max(upper[1], grid.x2_max);
        lower[2] = std::min(lower[2], grid.x3_min);
        upper[2] = std::max(upper[2], grid.x3_max);
        max_level = std::max(max_level, block.level);
    }
    const bool origin = lower[0] == 0.;
    const bool north = spherical && dimension == 3 && lower[1] == 0.;
    const bool south = spherical && dimension == 3
        && std::abs(upper[1] - pi) <= 1e-12;
    if (!origin && !north && !south) return plan;
    const int azimuth = dimension - 1;
    const double phi_width = upper[azimuth] - lower[azimuth];
    // A partial wedge is still a valid pre-existing Hydro boundary problem.
    // Only full turns have a physical phi+pi donor; self-gravity rejects a
    // partial turn separately in its configuration contract.
    if (std::abs(phi_width - 2. * pi) > 1e-12) return plan;

    // Leaf lookup is needed only for a full-turn singular domain. Ordinary
    // curved domains retain their previous exchange-plan construction cost.
    std::map<std::tuple<int, int, int, int>, int> leaves;
    for (int id : active_blocks) {
        const Block& block = pool->GetBlock(id);
        if (!leaves.emplace(std::make_tuple(block.level,
                static_cast<int>(block.logical_x1),
                static_cast<int>(block.logical_x2),
                static_cast<int>(block.logical_x3)), id).second)
            throw std::invalid_argument("Coordinate seam has duplicate active leaf");
    }

    const auto append = [&](int destination_id, int i, int j, int k) {
        const Grid& grid = pool->GetBlock(destination_id).grid;
        CoordinateSeamTransfer transfer;
        transfer.destination_id = destination_id;
        transfer.destination_cell = grid.GetIndex(i, j, k);
        const std::array<double, 3> ghost{
            grid.GetCellCenterX(i), grid.GetCellCenterY(j),
            grid.GetCellCenterZ(k)};
        const auto mapped = seam_detail::regular_position(ghost, dimension,
            spherical, lower[azimuth], phi_width, transfer.momentum_sign);
        transfer.source_id = seam_detail::locate_donor(leaves, mapped,
            lower, upper, root_blocks, dimension, max_level);
        seam_detail::donor_stencil(
            pool->GetBlock(transfer.source_id).grid, mapped, transfer);
        plan.transfers.push_back(transfer);
    };
    for (int id : active_blocks) {
        const Grid& grid = pool->GetBlock(id).grid;
        if (origin && grid.x1_min == 0.) {
            for (int k = grid.Ks(); k < grid.Ke(); ++k)
                for (int j = grid.Js(); j < grid.Je(); ++j)
                    for (int depth = 1; depth <= grid.ng; ++depth)
                        append(id, grid.Is() - depth, j, k);
        }
        if (north && std::abs(grid.x2_min) <= 1e-12) {
            for (int k = grid.Ks(); k < grid.Ke(); ++k)
                for (int i = grid.Is(); i < grid.Ie(); ++i)
                    for (int depth = 1; depth <= grid.ng; ++depth)
                        append(id, i, grid.Js() - depth, k);
        }
        if (south && std::abs(grid.x2_max - pi) <= 1e-12) {
            for (int k = grid.Ks(); k < grid.Ke(); ++k)
                for (int i = grid.Is(); i < grid.Ie(); ++i)
                    for (int depth = 1; depth <= grid.ng; ++depth)
                        append(id, i, grid.Je() - 1 + depth, k);
        }
    }
    return plan;
}

/** Apply cached donor stencils to one Host state slot after ordinary exchange. */
inline void execute_coordinate_seam_plan(const CoordinateSeamPlan& plan,
    const std::shared_ptr<MemoryPool>& pool, FluidState Block::* state_ptr)
{
    for (const auto& transfer : plan.transfers) {
        const FluidState& source = pool->GetBlock(transfer.source_id).*state_ptr;
        FluidState& destination = pool->GetBlock(transfer.destination_id).*state_ptr;
        if (source.GetNumSpecies() != destination.GetNumSpecies())
            throw std::invalid_argument("Coordinate seam species counts disagree");
        // u_g = u_c + sum_a [(x_g-x_c)/(x_n-x_c)] (u_n-u_c).
        // Indices and weights were lowered once from the AMR topology.
        const auto sample = [&](const auto& field) {
            double value = field[transfer.source_center];
            for (int axis = 0; axis < 3; ++axis)
                value += transfer.neighbor_weight[axis]
                    * (field[transfer.source_neighbor[axis]]
                       - field[transfer.source_center]);
            return value;
        };
        const int cell = transfer.destination_cell;
        const FluidVector candidate{
            sample(source.rho),
            transfer.momentum_sign[0] * sample(source.mom_u),
            transfer.momentum_sign[1] * sample(source.mom_v),
            transfer.momentum_sign[2] * sample(source.mom_w),
            sample(source.eng)};
        const double enuc = sample(source.enuc_rate);
        bool admissible = arch::state::recover(candidate).status
            == arch::state::Status::valid && std::isfinite(enuc);
        double species_sum = 0.;
        const double species_tolerance = regrid_math::composition_simplex_tolerance(
            source.GetNumSpecies());
        for (int species = 0; species < source.GetNumSpecies(); ++species) {
            double value = source.X(species, transfer.source_center);
            for (int axis = 0; axis < 3; ++axis)
                value += transfer.neighbor_weight[axis]
                    * (source.X(species, transfer.source_neighbor[axis])
                       - source.X(species, transfer.source_center));
            destination.X(species, cell) = value;
            species_sum += value;
            admissible &= std::isfinite(value) && value >= 0.
                && value <= 1.;
        }
        if (source.GetNumSpecies() > 0)
            admissible &= std::abs(species_sum - 1.) <= species_tolerance;
        if (admissible) {
            destination.set(cell, candidate);
            destination.enuc_rate[cell] = enuc;
            continue;
        }
        // The donor center is already a published active state. If a
        // cross-level one-sided slope leaves the admissible domain, keep its
        // chart transform and use piecewise-constant donor data as a local
        // ghost-only fallback. No active conservative quantity is repaired.
        FluidVector center = source.get(transfer.source_center);
        center.mom_u *= transfer.momentum_sign[0];
        center.mom_v *= transfer.momentum_sign[1];
        center.mom_w *= transfer.momentum_sign[2];
        if (arch::state::recover(center).status != arch::state::Status::valid)
            throw std::invalid_argument("Coordinate seam donor has invalid fluid state");
        destination.set(cell, center);
        destination.enuc_rate[cell] = source.enuc_rate[transfer.source_center];
        for (int species = 0; species < source.GetNumSpecies(); ++species)
            destination.X(species, cell) = source.X(species, transfer.source_center);
    }
}

} // namespace amr
