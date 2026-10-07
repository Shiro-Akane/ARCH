/**
 * @file AMRFluxRegistering.h
 * @brief Execution of shared topology-scoped AMR flux routes.
 * Workflow:
 * 1. Read oriented hydro face fluxes and AMR level interfaces.
 * 2. Construct or apply conservative flux-register contributions.
 * 3. Return coarse-fine corrections to the stage conservation update.
 */

#pragma once

#include <cstddef>
#include <stdexcept>
#include <vector>

#include "amr/AMRControl.h"
#include "physics/gravity/NativeSelfStage.h"

namespace amr {

/** Gather and execute one already-indexed route without scanning the tree.
 * Workflow: validate original native route/storage; gather all scalar values;
 * optionally form SAME-stage FE+(Phi_face-Phi_destination_coarse)*Frho through
 * its private self receipt; apply the original plan once; publish route receipt.
 * Flux input arrays remain immutable. This function adds no dt/RK/area factor.
 */
inline void RegisterCoarseFineFluxes(
    AMRControl& amr_ctrl, const AmrFluxTopologyPlan& topology,
    int block_id, const Grid& grid, int direction,
    const std::vector<FluidVector>& flux_buffer,
    const std::vector<double>& species_flux_buffer,
    int species_count, double stage_weight,
    Physical::Gravity::NativeSelfStageFrame::PatchReceipt* native_self = nullptr)
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

    if (!topology.native_grids.empty() && !matches_amr_flux_geometry(
            topology,*amr_ctrl.pool,amr_ctrl.tree->GetActiveBlocks(),
            topology.semantics,block_id))
        throw std::invalid_argument("AMR face-flux native geometry drifted");
    const Block& block = amr_ctrl.pool->GetBlock(block_id);
    if (!flux_plan_detail::same_grid_contract(block.grid, grid)
        || block.fluid_state.GetNumSpecies() != species_count)
        throw std::invalid_argument("coarse-fine flux source layout drifted");
    const auto* route = topology.find(block_id, direction);
    if (route == nullptr || stage_weight == 0.0) return;

    std::vector<double> values;
    values.reserve(route->plan.operations.size());
    for (std::size_t operation_index=0;operation_index<route->plan.operations.size();++operation_index) {
        const auto& operation=route->plan.operations[operation_index];
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
            values.push_back(flux_math::angular_registered_flux(
                flux_buffer[flux_index].mom_w,
                angular_registration_lever(topology,operation.source,
                    operation.source_box,operation.axis)));
            break;
        case AmrField::Energy:
            // Pair this ORIGINAL operation's face Phi and recipient coarse
            // Phi with its SAME-stage mass flux before any register mutation.
            // The receipt returns FE+psi*Frho; original sign/area/RK/dt stay out.
            values.push_back(native_self ? native_self->registered_energy(
                topology,*route,operation_index,flux_index,flux_buffer[flux_index].eng,
                flux_buffer[flux_index].rho,stage_weight) : flux_buffer[flux_index].eng);
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
        route->plan, values, topology.pool_lowering, stage_weight,topology.fingerprint);
    // Publish a route receipt only AFTER original registration truly succeeds.
    // Empty routes and stage_weight==0 above remain literal no-consumption.
    if(native_self)native_self->commit_registration(*route);
}

/**
 * Cached-plan entry used by Host hydro/diffusion. AMRControl owns the
 * epoch cache, so the first call after publication builds O(blocks*surface)
 * plans and every later stage/direction call is an indexed lookup.
 */
inline void RegisterCoarseFineFluxes(
    AMRControl& amr_ctrl, int block_id, const Grid& grid, int direction,
    const std::vector<FluidVector>& flux_buffer,
    const std::vector<double>& species_flux_buffer,
    int species_count, double stage_weight,
    GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing,
    bool angular_transport = false,
    Physical::Gravity::NativeSelfStageFrame::PatchReceipt* native_self = nullptr)
{
    const auto& topology =
        amr_ctrl.RequireFluxTopologyPlan(species_count,semantics,block_id,angular_transport);
    RegisterCoarseFineFluxes(
        amr_ctrl, topology, block_id, grid, direction, flux_buffer,
        species_flux_buffer, species_count, stage_weight,native_self);
}

} // namespace amr
