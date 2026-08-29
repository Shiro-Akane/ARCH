/**
 * @file AMRFluxRegistering.h
 * @brief Shared coarse-fine flux registration for conservative AMR operators.
 */

/**
 * Workflow:
 * 1. Build or query topology using the single hierarchy and memory-pool ownership model.
 * 2. Synchronize state or face data with the documented 2:1 AMR index convention.
 * 3. Return conservative leaf data to the driver for refluxing, regridding, or timestep work.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

#include "../grid/GridMetrics.h"
#include "AMRControl.h"

namespace amr {

/**
 * Register flux densities from one block face in the common flux register.
 *
 * Flux solvers store a face flux at the index of the cell on its right in the
 * sweep direction.  This routine is the single owner of that convention for
 * hydro and diffusion, including child-to-parent tangential index mapping.
 */
inline void RegisterCoarseFineFluxes(AMRControl& amr_ctrl, int block_id,
                                     const Grid& grid, int dir,
                                     const std::vector<FluidVector>& flux_buffer,
                                     const std::vector<double>& species_flux_buffer,
                                     int num_species, double flux_weight)
{
    const Block& block = amr_ctrl.pool->GetBlock(block_id);
    const int nx = BLOCK_NX;
    const int ny = (grid.dim >= 2) ? BLOCK_NY : 1;
    const int nz = (grid.dim == 3) ? BLOCK_NZ : 1;
    const int total_size = grid.GetTotalSize();

    if (dir < 0 || dir >= grid.dim || num_species < 0
        || num_species != amr_ctrl.flux_register.GetNumSpecies()
        || flux_buffer.size() != static_cast<std::size_t>(total_size)
        || species_flux_buffer.size()
            != static_cast<std::size_t>(num_species * total_size)
        || !amr_plan_detail::is_finite_binary64(flux_weight))
        throw std::invalid_argument("invalid coarse-fine flux inputs");

    const auto& active_blocks = amr_ctrl.tree->GetActiveBlocks();
    const auto active_handles = amr_ctrl.ActiveHandles();
    if (active_handles.size() != active_blocks.size())
        throw std::logic_error("AMR flux handles do not match active blocks");

    const auto logical_key = [&](const Block& value) {
        return LogicalBlockKey{
            grid.dim, value.level, value.logical_x1,
            value.logical_x2, value.logical_x3};
    };
    std::map<int, std::size_t> active_index;
    std::map<AmrEndpoint, int> pool_lowering;
    for (std::size_t index = 0; index < active_blocks.size(); ++index) {
        const int active_id = active_blocks[index];
        const Block& active_block = amr_ctrl.pool->GetBlock(active_id);
        const AmrEndpoint endpoint{
            logical_key(active_block), active_handles[index]};
        if (!active_index.emplace(active_id, index).second
            || !pool_lowering.emplace(endpoint, active_id).second)
            throw std::logic_error("AMR flux lowering is not bijective");
    }
    const auto source_index = active_index.find(block_id);
    if (source_index == active_index.end())
        throw std::invalid_argument("AMR flux source block is not active");
    const AmrEndpoint source_endpoint{
        logical_key(block), active_handles[source_index->second]};

    struct PendingContribution {
        AmrTransferOperation operation{};
        double value = 0.0;
    };
    std::vector<PendingContribution> pending;
    const auto append_fields = [&](const AmrTransferOperation& prototype,
                                   const FluidVector& flux,
                                   int flux_index, double coefficient_sign) {
        const auto append = [&](AmrField field, int component, double value) {
            AmrTransferOperation operation = prototype;
            operation.field = field;
            operation.component = component;
            pending.push_back({operation, value * coefficient_sign});
        };
        append(AmrField::Rho, -1, flux.rho);
        append(AmrField::MomU, -1, flux.mom_u);
        append(AmrField::MomV, -1, flux.mom_v);
        append(AmrField::MomW, -1, flux.mom_w);
        append(AmrField::Energy, -1, flux.eng);
        for (int species = 0; species < num_species; ++species) {
            append(AmrField::Species, species,
                   species_flux_buffer[species * total_size + flux_index]);
        }
    };

    const auto coarse_face_area = [](const Block& coarse, int normal_dir,
                                     int coarse_face, int face_cell_idx) {
        const int coarse_ny = (coarse.grid.dim >= 2) ? BLOCK_NY : 1;
        int i = coarse.grid.Is();
        int j = coarse.grid.Js();
        int k = coarse.grid.Ks();
        if (normal_dir == 0) {
            j += face_cell_idx % coarse_ny;
            k += face_cell_idx / coarse_ny;
            i += (coarse_face % 2 == 0) ? 0 : BLOCK_NX - 1;
        } else if (normal_dir == 1) {
            i += face_cell_idx % BLOCK_NX;
            k += face_cell_idx / BLOCK_NX;
            j += (coarse_face % 2 == 0) ? 0 : BLOCK_NY - 1;
        } else {
            i += face_cell_idx % BLOCK_NX;
            j += face_cell_idx / BLOCK_NX;
            k += (coarse_face % 2 == 0) ? 0 : BLOCK_NZ - 1;
        }
        return GridMetrics::FaceArea(coarse.grid, normal_dir, i, j, k,
                                     (coarse_face % 2) == 1);
    };

    for (int side = 0; side < 2; ++side) {
        const int face = 2 * dir + side;
        const auto& neighbours = block.face_neighbors[face];
        if (neighbours.count == 0 || neighbours.level_diff == 0) continue;

        for (int k = 0; k < nz; ++k) {
            for (int j = 0; j < ny; ++j) {
                for (int i = 0; i < nx; ++i) {
                    if ((dir == 0 && i != (side == 0 ? 0 : nx - 1)) ||
                        (dir == 1 && j != (side == 0 ? 0 : ny - 1)) ||
                        (dir == 2 && k != (side == 0 ? 0 : nz - 1))) continue;

                    const int flux_idx = grid.GetIndex(
                        grid.Is() + i + ((dir == 0 && side == 1) ? 1 : 0),
                        grid.Js() + j + ((dir == 1 && side == 1) ? 1 : 0),
                        grid.Ks() + k + ((dir == 2 && side == 1) ? 1 : 0));
                    const double fine_area = GridMetrics::FaceArea(
                        grid, dir, grid.Is() + i, grid.Js() + j, grid.Ks() + k, side == 1);

                    int coarse_cell_idx = 0;
                    if (neighbours.level_diff == -1) {
                        const int coarse_id = neighbours.ids[0];
                        if (coarse_id < 0) continue;
                        const auto coarse_index = active_index.find(coarse_id);
                        if (coarse_index == active_index.end())
                            throw std::invalid_argument(
                                "coarse flux destination is not active");

                        const int child_x = block.logical_x1 & 1;
                        const int child_y = block.logical_x2 & 1;
                        const int child_z = block.logical_x3 & 1;
                        int c_i = child_x * (nx / 2) + i / 2;
                        int c_j = (grid.dim >= 2) ? child_y * (ny / 2) + j / 2 : 0;
                        int c_k = (grid.dim == 3) ? child_z * (nz / 2) + k / 2 : 0;

                        if (dir == 0) coarse_cell_idx = c_k * ny + c_j;
                        else if (dir == 1) coarse_cell_idx = c_k * nx + c_i;
                        else coarse_cell_idx = c_j * nx + c_i;

                        const int coarse_face = 2 * dir + (side == 0 ? 1 : 0);
                        const Block& coarse = amr_ctrl.pool->GetBlock(coarse_id);
                        const double area = coarse_face_area(coarse, dir, coarse_face, coarse_cell_idx);
                        if (area <= 0.0) continue;
                        const double area_weight = flux_weight * fine_area / area;
                        const double coefficient_sign = std::signbit(area_weight)
                            ? -1.0 : 1.0;
                        if (dir == 0) c_i = coarse_face % 2 == 0 ? 0 : nx - 1;
                        else if (dir == 1) c_j = coarse_face % 2 == 0 ? 0 : ny - 1;
                        else c_k = coarse_face % 2 == 0 ? 0 : nz - 1;
                        const AmrEndpoint destination_endpoint{
                            logical_key(coarse),
                            active_handles[coarse_index->second]};
                        AmrTransferOperation prototype{
                            0, source_endpoint, destination_endpoint,
                            {{i, j, k}, {1, 1, 1}},
                            {{c_i, c_j, c_k}, {1, 1, 1}},
                            static_cast<AmrAxis>(dir),
                            static_cast<AmrSide>(coarse_face % 2),
                            AmrField::Rho, -1,
                            RefinementRule::FineFluxContribution,
                            std::abs(area_weight), 1.0};
                        append_fields(prototype, flux_buffer[flux_idx],
                                      flux_idx, coefficient_sign);
                    } else if (neighbours.level_diff == 1) {
                        if (dir == 0) coarse_cell_idx = k * ny + j;
                        else if (dir == 1) coarse_cell_idx = k * nx + i;
                        else coarse_cell_idx = j * nx + i;
                        const double coefficient_sign = std::signbit(flux_weight)
                            ? -1.0 : 1.0;
                        AmrTransferOperation prototype{
                            0, source_endpoint, source_endpoint,
                            {{i, j, k}, {1, 1, 1}},
                            {{i, j, k}, {1, 1, 1}},
                            static_cast<AmrAxis>(dir),
                            static_cast<AmrSide>(side),
                            AmrField::Rho, -1,
                            RefinementRule::CoarseFluxContribution,
                            std::abs(flux_weight), -1.0};
                        append_fields(prototype, flux_buffer[flux_idx],
                                      flux_idx, coefficient_sign);
                    }
                }
            }
        }
    }

    if (pending.empty()) return;
    std::sort(pending.begin(), pending.end(),
              [](const PendingContribution& lhs,
                 const PendingContribution& rhs) {
                  return canonical_operation_less(
                      lhs.operation, rhs.operation);
              });
    FluxRegistrationPlan plan{};
    plan.dimension = grid.dim;
    plan.scope = {0, active_handles.front().epoch,
                  active_handles.front().epoch};
    std::vector<double> values;
    plan.operations.reserve(pending.size());
    values.reserve(pending.size());
    for (auto& contribution : pending) {
        plan.operations.push_back(std::move(contribution.operation));
        values.push_back(contribution.value);
    }
    finalize_amr_plan(plan);
    amr_ctrl.flux_register.ApplyRegistrationPlan(
        plan, values, pool_lowering);
}

} // namespace amr
