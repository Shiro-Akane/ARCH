/**
 * @file DiffusionAMRStages.h
 * @brief Internal conservative AMR stage engine shared by RKL1 and RKL2.
 *
 * Every RKL stage performs a physical-boundary fill, AMR ghost exchange,
 * coarse-fine flux registration, reflux, and a second halo synchronization.
 * The implementation therefore keeps the STS polynomial global to the leaf
 * hierarchy instead of applying independent patch-local RKL updates.
 */

/**
 * Workflow:
 * 1. Evaluate the shared physical diffusion operator on synchronized AMR leaves.
 * 2. Register coarse-fine fluxes, reflux, and refresh halos at every RKL stage.
 * 3. Return a conservative composite hierarchy state to the driver.
 */

#pragma once

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "DiffFlux.h"
#include "DiffFunction.h"
#include "../../amr/AMRFluxRegistering.h"

namespace Numerics::Diffusion {

namespace detail {

template <typename EosType>
inline void evaluate_diffusion_increment(amr::AMRControl& amr_ctrl, int block_id,
                                         const FluidState& state, const EosType& eos,
                                         const Grid& grid, const SimConfig& config,
                                         double dt, double flux_weight,
                                         std::vector<FluidVector>& dU,
                                         std::vector<double>& d_species)
{
    const int n_species = state.GetNumSpecies();
    const int total_size = grid.GetTotalSize();
    dU.assign(total_size, FluidVector{});
    d_species.assign(static_cast<size_t>(n_species) * total_size, 0.0);
    std::vector<FluidVector> flux_buffer(total_size);
    std::vector<double> species_flux_buffer(static_cast<size_t>(n_species) * total_size, 0.0);

    for (int dir = 0; dir < grid.dim; ++dir) {
        std::fill(flux_buffer.begin(), flux_buffer.end(), FluidVector{});
        std::fill(species_flux_buffer.begin(), species_flux_buffer.end(), 0.0);
        DiffFlux::compute_fluxes(state, eos, grid, config, flux_buffer, species_flux_buffer, dir);
        TimeIntegration::accumulate_divergence(dU, d_species, flux_buffer, species_flux_buffer,
                                               grid, dt, dir, n_species);
        amr::RegisterCoarseFineFluxes(amr_ctrl, block_id, grid, dir, flux_buffer,
                                      species_flux_buffer, n_species, flux_weight);
    }

    DiffFlux::add_geometric_sources(dU, state, eos, grid, config, dt);
}

inline void apply_first_rkl_stage(const FluidState& state_n, FluidState& destination,
                                  const std::vector<FluidVector>& dU,
                                  const std::vector<double>& d_species, const Grid& grid,
                                  double coefficient)
{
    const int n_species = state_n.GetNumSpecies();
    const int total_size = grid.GetTotalSize();
    const int ks = grid.Ks(), ke = grid.Ke();
    const int js = grid.Js(), je = grid.Je();

#pragma omp parallel for schedule(static)
    for (int kj = 0; kj < (ke - ks) * (je - js); ++kj) {
        const int k = ks + kj / (je - js);
        const int j = js + kj % (je - js);
        for (int i = grid.Is(); i < grid.Ie(); ++i) {
            const int idx = grid.GetIndex(i, j, k);
            const FluidVector updated = state_n.get(idx) + coefficient * dU[idx];
            destination.set(idx, updated);
            for (int species = 0; species < n_species; ++species) {
                const double rhoX = state_n.rho[idx] * state_n.X(species, idx)
                                  + coefficient * d_species[species * total_size + idx];
                destination.X(species, idx) = rhoX / updated.rho;
            }
        }
    }
}

inline void apply_recursive_rkl_stage(const FluidState& state_n,
                                      const FluidState& state_previous,
                                      const FluidState& state_older,
                                      FluidState& destination,
                                      const std::vector<FluidVector>& d_previous,
                                      const std::vector<double>& d_species_previous,
                                      const std::vector<FluidVector>& d_initial,
                                      const std::vector<double>& d_species_initial,
                                      const Grid& grid, const DiffFunction::RKLCoeffs& coeffs,
                                      bool second_order)
{
    const int n_species = state_n.GetNumSpecies();
    const int total_size = grid.GetTotalSize();
    const int ks = grid.Ks(), ke = grid.Ke();
    const int js = grid.Js(), je = grid.Je();
    const double initial_weight = second_order ? 1.0 - coeffs.mu - coeffs.nu : 0.0;
    const double initial_operator_weight = second_order ? coeffs.gamma : 0.0;

#pragma omp parallel for schedule(static)
    for (int kj = 0; kj < (ke - ks) * (je - js); ++kj) {
        const int k = ks + kj / (je - js);
        const int j = js + kj % (je - js);
        for (int i = grid.Is(); i < grid.Ie(); ++i) {
            const int idx = grid.GetIndex(i, j, k);
            const FluidVector updated = coeffs.mu * state_previous.get(idx)
                                      + coeffs.nu * state_older.get(idx)
                                      + initial_weight * state_n.get(idx)
                                      + coeffs.tilde_mu * d_previous[idx]
                                      + initial_operator_weight * d_initial[idx];
            // destination may alias state_older.  Read every old rhoX before
            // changing its conserved fields, otherwise the recurrence would
            // mix Y_j with Y_{j-2} for composition-dependent EOS states.
            for (int species = 0; species < n_species; ++species) {
                const double rhoX = coeffs.mu * state_previous.rho[idx] * state_previous.X(species, idx)
                                  + coeffs.nu * state_older.rho[idx] * state_older.X(species, idx)
                                  + initial_weight * state_n.rho[idx] * state_n.X(species, idx)
                                  + coeffs.tilde_mu * d_species_previous[species * total_size + idx]
                                  + initial_operator_weight * d_species_initial[species * total_size + idx];
                destination.X(species, idx) = rhoX / updated.rho;
            }
            destination.set(idx, updated);
        }
    }
}

template <typename BCPolicy>
inline void synchronize(amr::AMRControl& amr_ctrl, BCPolicy& boundary_condition,
                        FluidState amr::Block::* state_ptr)
{
    const auto& active_blocks = amr_ctrl.tree->GetActiveBlocks();
#pragma omp parallel for schedule(dynamic, 1)
    for (size_t index = 0; index < active_blocks.size(); ++index) {
        amr::Block& block = amr_ctrl.pool->GetBlock(active_blocks[index]);
        boundary_condition.apply(block.*state_ptr, block.grid);
    }
    amr_ctrl.ghost_exchange.ExecuteExchange(amr_ctrl.pool, amr_ctrl.tree,
                                            amr_ctrl.tree->GetRootGridDim(), state_ptr);
}

inline void copy_stage_to_solution(amr::AMRControl& amr_ctrl, FluidState amr::Block::* source_ptr)
{
    const auto& active_blocks = amr_ctrl.tree->GetActiveBlocks();
#pragma omp parallel for schedule(static)
    for (size_t index = 0; index < active_blocks.size(); ++index) {
        amr::Block& block = amr_ctrl.pool->GetBlock(active_blocks[index]);
        block.fluid_state = block.*source_ptr;
    }
}

} // namespace detail

/**
 * Advance all AMR leaves through an RKL polynomial.  RKL2 re-evaluates the
 * initial operator at each recurrence stage so the matching coarse-fine flux
 * residual can be refluxed without an additional hierarchy-sized flux cache.
 */
template <typename EosType, typename BCPolicy>
inline void advance_amr_rkl(amr::AMRControl& amr_ctrl, double dt, double dt_diff_fe,
                            BCPolicy& boundary_condition, const EosType& eos,
                            const SimConfig& config, DiffFunction::RKLOrder order)
{
    const bool second_order = order == DiffFunction::RKLOrder::Second;
    const auto& active_blocks = amr_ctrl.tree->GetActiveBlocks();
    if (active_blocks.empty()) return;

    const int max_stages = config.physics.diffusion.max_stages;
    const double diff_cfl = config.physics.diffusion.diff_cfl;
    const int stages = DiffFunction::compute_stages(order, dt, dt_diff_fe, diff_cfl, max_stages);
    if (stages <= 0) return;

    amr_ctrl.flux_register.EnsureSpecies(
        amr_ctrl.pool->GetBlock(active_blocks.front()).fluid_state.GetNumSpecies());
    detail::synchronize(amr_ctrl, boundary_condition, &amr::Block::fluid_state);

    // fluid_state remains Y_0 for the full polynomial.  state_scratch and
    // state_next alternate as Y_{j-1}/Y_{j-2}; this preserves the block pool's
    // fixed allocation and avoids stage-history reallocations.
#pragma omp parallel for schedule(static)
    for (size_t index = 0; index < active_blocks.size(); ++index) {
        amr::Block& block = amr_ctrl.pool->GetBlock(active_blocks[index]);
        block.state_next = block.fluid_state;
    }

    const DiffFunction::RKLCoeffs first_coeffs = DiffFunction::get_rkl_coeffs(order, 1, stages);
    amr_ctrl.flux_register.Clear();
    for (const int block_id : active_blocks) {
        amr::Block& block = amr_ctrl.pool->GetBlock(block_id);
        std::vector<FluidVector> d_initial;
        std::vector<double> d_species_initial;
        detail::evaluate_diffusion_increment(amr_ctrl, block_id, block.fluid_state, eos,
                                             block.grid, config, dt,
                                             first_coeffs.tilde_mu,
                                             d_initial, d_species_initial);
        const double first_coefficient = first_coeffs.tilde_mu;
        detail::apply_first_rkl_stage(block.fluid_state, block.state_scratch,
                                      d_initial, d_species_initial, block.grid, first_coefficient);
    }
    amr_ctrl.ApplyReflux(dt, &amr::Block::state_scratch);
    detail::synchronize(amr_ctrl, boundary_condition, &amr::Block::state_scratch);

    if (stages == 1) {
        detail::copy_stage_to_solution(amr_ctrl, &amr::Block::state_scratch);
        detail::synchronize(amr_ctrl, boundary_condition, &amr::Block::fluid_state);
        return;
    }

    for (int stage = 2; stage <= stages; ++stage) {
        const DiffFunction::RKLCoeffs coeffs = DiffFunction::get_rkl_coeffs(order, stage, stages);
        const bool previous_in_scratch = (stage % 2) == 0;
        FluidState amr::Block::* previous_ptr = previous_in_scratch
            ? &amr::Block::state_scratch : &amr::Block::state_next;
        FluidState amr::Block::* older_ptr = previous_in_scratch
            ? &amr::Block::state_next : &amr::Block::state_scratch;

        amr_ctrl.flux_register.Clear();
        for (const int block_id : active_blocks) {
            amr::Block& block = amr_ctrl.pool->GetBlock(block_id);
            FluidState& previous = block.*previous_ptr;
            FluidState& older = block.*older_ptr;
            std::vector<FluidVector> d_previous;
            std::vector<double> d_species_previous;
            detail::evaluate_diffusion_increment(amr_ctrl, block_id, previous, eos, block.grid,
                                                 config, dt, coeffs.tilde_mu,
                                                 d_previous, d_species_previous);

            std::vector<FluidVector> d_initial;
            std::vector<double> d_species_initial;
            if (second_order) {
                detail::evaluate_diffusion_increment(amr_ctrl, block_id, block.fluid_state, eos,
                                                     block.grid, config, dt, coeffs.gamma,
                                                     d_initial, d_species_initial);
            } else {
                d_initial.assign(block.grid.GetTotalSize(), FluidVector{});
                d_species_initial.assign(static_cast<size_t>(block.fluid_state.GetNumSpecies())
                                         * block.grid.GetTotalSize(), 0.0);
            }
            detail::apply_recursive_rkl_stage(block.fluid_state, previous, older, older,
                                              d_previous, d_species_previous,
                                              d_initial, d_species_initial, block.grid,
                                              coeffs, second_order);
        }
        amr_ctrl.ApplyReflux(dt, older_ptr);
        detail::synchronize(amr_ctrl, boundary_condition, older_ptr);
    }

    FluidState amr::Block::* final_ptr = (stages % 2) == 0
        ? &amr::Block::state_next : &amr::Block::state_scratch;
    detail::copy_stage_to_solution(amr_ctrl, final_ptr);
    detail::synchronize(amr_ctrl, boundary_condition, &amr::Block::fluid_state);
}

} // namespace Numerics::Diffusion
