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

// These expression leaves intentionally remain macro-expanded at legacy CPU
// call sites. GCC's -ffast-math changes FMA grouping when the recurrences
// cross a function boundary, even after inlining. Keeping one expansion source
// lets CPU paths retain their frozen raw bits while the host/device cell
// wrappers below consume the identical authority.
#define ARCH_DIFFUSION_FIRST_RKL_COMPONENT(                                \
    state_n, coefficient, increment)                                       \
    ((state_n) + (coefficient) * (increment))

#define ARCH_DIFFUSION_FIRST_RKL_SPECIES_EXPRESSION(                       \
    rho_n, species_n, coefficient, species_increment)                      \
    ((rho_n) * (species_n) + (coefficient) * (species_increment))

#define ARCH_DIFFUSION_UNSCALED_RKL1_COMPONENT(                            \
    coefficients, state_previous, state_older, increment_previous, dt)     \
    ((coefficients).mu * (state_previous)                                  \
     + (coefficients).nu * (state_older)                                   \
     + (coefficients).tilde_mu * (dt) * (increment_previous))

#define ARCH_DIFFUSION_UNSCALED_RKL1_SPECIES_EXPRESSION(                   \
    coefficients, rho_previous, species_previous, rho_older,              \
    species_older, species_increment_previous, dt)                         \
    ((coefficients).mu * (rho_previous) * (species_previous)               \
     + (coefficients).nu * (rho_older) * (species_older)                   \
     + (coefficients).tilde_mu * (dt) * (species_increment_previous))

#define ARCH_DIFFUSION_UNSCALED_RKL2_COMPONENT(                            \
    coefficients, state_previous, state_older, initial_weight, state_n,    \
    increment_previous, increment_initial, dt)                             \
    ((coefficients).mu * (state_previous)                                  \
     + (coefficients).nu * (state_older)                                   \
     + (initial_weight) * (state_n)                                        \
     + (dt) * ((coefficients).tilde_mu * (increment_previous)              \
               + (coefficients).gamma * (increment_initial)))

#define ARCH_DIFFUSION_UNSCALED_RKL2_SPECIES_EXPRESSION(                   \
    coefficients, rho_previous, species_previous, rho_older,              \
    species_older, initial_weight, rho_n, species_n,                       \
    species_increment_previous, species_increment_initial, dt)             \
    ((coefficients).mu * (rho_previous) * (species_previous)               \
     + (coefficients).nu * (rho_older) * (species_older)                   \
     + (initial_weight) * (rho_n) * (species_n)                            \
     + (dt) * ((coefficients).tilde_mu * (species_increment_previous)      \
               + (coefficients).gamma * (species_increment_initial)))

#define ARCH_DIFFUSION_SCALED_RKL_HYDRO_EXPRESSION(                        \
    coefficients, state_previous, state_older, state_n,                    \
    increment_previous, increment_initial, initial_weight,                 \
    initial_operator_weight)                                               \
    ((coefficients).mu * (state_previous)                                  \
     + (coefficients).nu * (state_older)                                   \
     + (initial_weight) * (state_n)                                        \
     + (coefficients).tilde_mu * (increment_previous)                      \
     + (initial_operator_weight) * (increment_initial))

#define ARCH_DIFFUSION_SCALED_RKL_SPECIES_EXPRESSION(                      \
    coefficients, rho_previous, species_previous, rho_older, species_older,\
    initial_weight, rho_n, species_n, increment_previous,                  \
    initial_operator_weight, increment_initial)                            \
    ((coefficients).mu * (rho_previous) * (species_previous)               \
     + (coefficients).nu * (rho_older) * (species_older)                   \
     + (initial_weight) * (rho_n) * (species_n)                            \
     + (coefficients).tilde_mu * (increment_previous)                      \
     + (initial_operator_weight) * (increment_initial))

inline void initialize_amr_rkl_stage_buffers(
    const FluidState& source, FluidState& state_scratch,
    FluidState& state_next)
{
    state_scratch = source;
    state_next = source;
}

ARCH_INLINE void apply_first_rkl_stage_cell(
    const FluidVector& state_n, const double* species_n,
    const FluidVector& increment, const double* species_increment,
    int species_count, int species_stride, double coefficient,
    FluidVector& destination, double* destination_species)
{
    destination = {
        ARCH_DIFFUSION_FIRST_RKL_COMPONENT(
            state_n.rho, coefficient, increment.rho),
        ARCH_DIFFUSION_FIRST_RKL_COMPONENT(
            state_n.mom_u, coefficient, increment.mom_u),
        ARCH_DIFFUSION_FIRST_RKL_COMPONENT(
            state_n.mom_v, coefficient, increment.mom_v),
        ARCH_DIFFUSION_FIRST_RKL_COMPONENT(
            state_n.mom_w, coefficient, increment.mom_w),
        ARCH_DIFFUSION_FIRST_RKL_COMPONENT(
            state_n.eng, coefficient, increment.eng)};
    for (int species = 0; species < species_count; ++species) {
        const int offset = species * species_stride;
        const double rhoX = ARCH_DIFFUSION_FIRST_RKL_SPECIES_EXPRESSION(
            state_n.rho, species_n[offset], coefficient,
            species_increment[offset]);
        destination_species[offset] = rhoX / destination.rho;
    }
}

template <bool IncrementsAreScaled>
ARCH_INLINE double evaluate_recursive_rkl_component(
    double state_n, double state_previous, double state_older,
    double increment_previous, double increment_initial,
    const DiffFunction::RKLCoeffs& coefficients, bool second_order,
    double dt)
{
    const double initial_weight = second_order
        ? 1.0 - coefficients.mu - coefficients.nu : 0.0;
    const double initial_operator_weight = second_order
        ? coefficients.gamma : 0.0;
    if constexpr (IncrementsAreScaled) {
        return ARCH_DIFFUSION_SCALED_RKL_HYDRO_EXPRESSION(
            coefficients, state_previous, state_older, state_n,
            increment_previous, increment_initial, initial_weight,
            initial_operator_weight);
    } else if (second_order) {
        return ARCH_DIFFUSION_UNSCALED_RKL2_COMPONENT(
            coefficients, state_previous, state_older, initial_weight,
            state_n, increment_previous, increment_initial, dt);
    }
    return ARCH_DIFFUSION_UNSCALED_RKL1_COMPONENT(
        coefficients, state_previous, state_older, increment_previous, dt);
}

template <bool IncrementsAreScaled>
ARCH_INLINE FluidVector evaluate_recursive_rkl_hydro_cell(
    FluidVector state_n, FluidVector state_previous,
    FluidVector state_older, FluidVector increment_previous,
    FluidVector increment_initial,
    const DiffFunction::RKLCoeffs& coefficients, bool second_order,
    double dt)
{
    return {
        evaluate_recursive_rkl_component<IncrementsAreScaled>(
            state_n.rho, state_previous.rho, state_older.rho,
            increment_previous.rho, increment_initial.rho,
            coefficients, second_order, dt),
        evaluate_recursive_rkl_component<IncrementsAreScaled>(
            state_n.mom_u, state_previous.mom_u, state_older.mom_u,
            increment_previous.mom_u, increment_initial.mom_u,
            coefficients, second_order, dt),
        evaluate_recursive_rkl_component<IncrementsAreScaled>(
            state_n.mom_v, state_previous.mom_v, state_older.mom_v,
            increment_previous.mom_v, increment_initial.mom_v,
            coefficients, second_order, dt),
        evaluate_recursive_rkl_component<IncrementsAreScaled>(
            state_n.mom_w, state_previous.mom_w, state_older.mom_w,
            increment_previous.mom_w, increment_initial.mom_w,
            coefficients, second_order, dt),
        evaluate_recursive_rkl_component<IncrementsAreScaled>(
            state_n.eng, state_previous.eng, state_older.eng,
            increment_previous.eng, increment_initial.eng,
            coefficients, second_order, dt)};
}

template <bool IncrementsAreScaled>
ARCH_INLINE double evaluate_recursive_rkl_species_cell(
    double rho_n, double species_n, double rho_previous,
    double species_previous, double rho_older, double species_older,
    double increment_previous, double increment_initial,
    const DiffFunction::RKLCoeffs& coefficients, bool second_order,
    double dt)
{
    const double initial_weight = second_order
        ? 1.0 - coefficients.mu - coefficients.nu : 0.0;
    const double initial_operator_weight = second_order
        ? coefficients.gamma : 0.0;
    if constexpr (IncrementsAreScaled) {
        return ARCH_DIFFUSION_SCALED_RKL_SPECIES_EXPRESSION(
            coefficients, rho_previous, species_previous,
            rho_older, species_older, initial_weight, rho_n, species_n,
            increment_previous, initial_operator_weight, increment_initial);
    } else if (second_order) {
        return ARCH_DIFFUSION_UNSCALED_RKL2_SPECIES_EXPRESSION(
            coefficients, rho_previous, species_previous, rho_older,
            species_older, initial_weight, rho_n, species_n,
            increment_previous, increment_initial, dt);
    }
    return ARCH_DIFFUSION_UNSCALED_RKL1_SPECIES_EXPRESSION(
        coefficients, rho_previous, species_previous, rho_older,
        species_older, increment_previous, dt);
}

template <bool IncrementsAreScaled>
ARCH_INLINE void apply_recursive_rkl_stage_cell_impl(
    FluidVector state_n, const double* species_n,
    FluidVector state_previous, const double* species_previous,
    FluidVector state_older, const double* species_older,
    FluidVector increment_previous,
    const double* species_increment_previous,
    FluidVector increment_initial,
    const double* species_increment_initial,
    int species_count, int species_stride,
    const DiffFunction::RKLCoeffs& coefficients, bool second_order,
    double dt,
    FluidVector& destination, double* destination_species)
{
    destination = evaluate_recursive_rkl_hydro_cell<IncrementsAreScaled>(
        state_n, state_previous, state_older, increment_previous,
        increment_initial, coefficients, second_order, dt);
    for (int species = 0; species < species_count; ++species) {
        const int offset = species * species_stride;
        const double rhoX =
            evaluate_recursive_rkl_species_cell<IncrementsAreScaled>(
                state_n.rho, species_n[offset], state_previous.rho,
                species_previous[offset], state_older.rho,
                species_older[offset], species_increment_previous[offset],
                species_increment_initial == nullptr
                    ? 0.0 : species_increment_initial[offset],
                coefficients, second_order, dt);
        destination_species[offset] = rhoX / destination.rho;
    }
}

ARCH_INLINE void apply_recursive_rkl_stage_cell(
    const FluidVector& state_n, const double* species_n,
    const FluidVector& state_previous, const double* species_previous,
    const FluidVector& state_older, const double* species_older,
    const FluidVector& increment_previous,
    const double* species_increment_previous,
    const FluidVector& increment_initial,
    const double* species_increment_initial,
    int species_count, int species_stride,
    const DiffFunction::RKLCoeffs& coefficients, bool second_order,
    double dt, bool increments_are_scaled,
    FluidVector& destination, double* destination_species)
{
    if (increments_are_scaled) {
        apply_recursive_rkl_stage_cell_impl<true>(
            state_n, species_n, state_previous, species_previous,
            state_older, species_older, increment_previous,
            species_increment_previous, increment_initial,
            species_increment_initial, species_count, species_stride,
            coefficients, second_order, dt, destination,
            destination_species);
    } else {
        apply_recursive_rkl_stage_cell_impl<false>(
            state_n, species_n, state_previous, species_previous,
            state_older, species_older, increment_previous,
            species_increment_previous, increment_initial,
            species_increment_initial, species_count, species_stride,
            coefficients, second_order, dt, destination,
            destination_species);
    }
}

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
            FluidVector updated;
            apply_first_rkl_stage_cell(
                state_n.get(idx),
                n_species > 0 ? state_n.mass_fractions.data() + idx : nullptr,
                dU[idx],
                n_species > 0 ? d_species.data() + idx : nullptr,
                n_species, total_size, coefficient, updated,
                n_species > 0
                    ? destination.mass_fractions.data() + idx : nullptr);
            destination.set(idx, updated);
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
            const FluidVector updated =
                ARCH_DIFFUSION_SCALED_RKL_HYDRO_EXPRESSION(
                    coeffs, state_previous.get(idx), state_older.get(idx),
                    state_n.get(idx), d_previous[idx], d_initial[idx],
                    initial_weight, initial_operator_weight);
            // destination may alias state_older.  Read every old rhoX before
            // changing its conserved fields, otherwise the recurrence would
            // mix Y_j with Y_{j-2} for composition-dependent EOS states.
            for (int species = 0; species < n_species; ++species) {
                const double rhoX =
                    ARCH_DIFFUSION_SCALED_RKL_SPECIES_EXPRESSION(
                        coeffs, state_previous.rho[idx],
                        state_previous.X(species, idx), state_older.rho[idx],
                        state_older.X(species, idx), initial_weight,
                        state_n.rho[idx], state_n.X(species, idx),
                        d_species_previous[species * total_size + idx],
                        initial_operator_weight,
                        d_species_initial[species * total_size + idx]);
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
        detail::initialize_amr_rkl_stage_buffers(
            block.fluid_state, block.state_scratch, block.state_next);
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
