/**
 * @file AMRFluxRegistering.h
 * @brief Execution of shared topology-scoped AMR flux routes.
 */

#pragma once

#include "AMRControl.h"

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace amr {

/** Gather and execute one already-indexed route without scanning the tree. */
inline void RegisterCoarseFineFluxes(
    AMRControl& amr_ctrl, const AmrFluxTopologyPlan& topology,
    int block_id, const Grid& grid, int direction,
    const std::vector<FluidVector>& flux_buffer,
    const std::vector<double>& species_flux_buffer,
    int species_count, double stage_weight)
{
    const int total_size = grid.GetTotalSize();
    if (direction < 0 || direction >= grid.dim || species_count < 0
        || species_count != topology.species_count
        || species_count != amr_ctrl.flux_register.GetNumSpecies()
        || topology.dimension != grid.dim
        || topology.epoch != amr_ctrl.ActiveHandles().front().epoch
        || flux_buffer.size() != static_cast<std::size_t>(total_size)
        || species_flux_buffer.size()
            != static_cast<std::size_t>(species_count) * total_size
        || !amr_plan_detail::is_finite_binary64(stage_weight))
        throw std::invalid_argument("invalid coarse-fine flux inputs");

    const Block& block = amr_ctrl.pool->GetBlock(block_id);
    if (!flux_plan_detail::same_grid_contract(block.grid, grid)
        || block.fluid_state.GetNumSpecies() != species_count)
        throw std::invalid_argument("coarse-fine flux source layout drifted");
    const auto* route = topology.find(block_id, direction);
    if (route == nullptr || stage_weight == 0.0) return;

    std::vector<double> values;
    values.reserve(route->plan.operations.size());
    for (const auto& operation : route->plan.operations) {
        const int flux_index = grid.GetIndex(
            grid.Is() + operation.source_box.first[0],
            grid.Js() + operation.source_box.first[1],
            grid.Ks() + operation.source_box.first[2]);
        if (flux_index < 0 || flux_index >= total_size)
            throw std::invalid_argument(
                "AMR face-flux source is outside scratch storage");
        switch (operation.field) {
        case AmrField::Rho:
            values.push_back(flux_buffer[flux_index].rho);
            break;
        case AmrField::MomU:
            values.push_back(flux_buffer[flux_index].mom_u);
            break;
        case AmrField::MomV:
            values.push_back(flux_buffer[flux_index].mom_v);
            break;
        case AmrField::MomW:
            values.push_back(flux_buffer[flux_index].mom_w);
            break;
        case AmrField::Energy:
            values.push_back(flux_buffer[flux_index].eng);
            break;
        case AmrField::Species:
            values.push_back(species_flux_buffer[
                static_cast<std::size_t>(operation.component) * total_size
                + flux_index]);
            break;
        case AmrField::EnucRate:
            throw std::logic_error("ENUC appeared in a face-flux plan");
        }
    }
    amr_ctrl.flux_register.ApplyRegistrationPlan(
        route->plan, values, topology.pool_lowering, stage_weight);
}

/**
 * Compatibility entry used by Host hydro/diffusion.  AMRControl owns the
 * epoch cache, so the first call after publication builds O(blocks*surface)
 * plans and every later stage/direction call is an indexed lookup.
 */
inline void RegisterCoarseFineFluxes(
    AMRControl& amr_ctrl, int block_id, const Grid& grid, int direction,
    const std::vector<FluidVector>& flux_buffer,
    const std::vector<double>& species_flux_buffer,
    int species_count, double stage_weight)
{
    const auto& topology =
        amr_ctrl.RequireFluxTopologyPlan(species_count);
    RegisterCoarseFineFluxes(
        amr_ctrl, topology, block_id, grid, direction, flux_buffer,
        species_flux_buffer, species_count, stage_weight);
}

} // namespace amr
