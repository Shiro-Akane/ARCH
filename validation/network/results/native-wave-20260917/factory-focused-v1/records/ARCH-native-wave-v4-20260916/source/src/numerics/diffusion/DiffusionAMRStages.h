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
#include "../../driver/StageScheduler.h"

namespace Numerics::Diffusion {

namespace detail {

// These expression leaves intentionally remain macro-expanded at Host
// call sites. GCC's -ffast-math changes FMA grouping when the recurrences
// cross a function boundary, even after inlining. Keeping one expansion source
// preserves the Host expression grouping while the Host/device cell
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
    const auto& binding = arch::scheduler::current_stage_binding();
    const auto& active_blocks = amr_ctrl.tree->GetActiveBlocks();
    if (binding.handles.size() != active_blocks.size())
        throw std::logic_error("RKL exchange handle count mismatch");
#pragma omp parallel for schedule(dynamic, 1)
    for (size_t index = 0; index < active_blocks.size(); ++index) {
        amr::Block& block = amr_ctrl.pool->GetBlock(active_blocks[index]);
        boundary_condition.apply(block.*state_ptr, block.grid);
    }
    amr_ctrl.ghost_exchange.ExecuteExchange(amr_ctrl.pool, amr_ctrl.tree,
                                            amr_ctrl.tree->GetRootGridDim(),
                                            state_ptr, binding.handles);
}

inline FluidState& state_for(amr::Block& block,
                             arch::state::StateSlot slot)
{
    using arch::state::StateSlot;
    switch (slot) {
    case StateSlot::Current: return block.fluid_state;
    case StateSlot::Next: return block.state_next;
    case StateSlot::Scratch: return block.state_scratch;
    }
    throw std::logic_error("RKL descriptor selected unknown slot");
}

inline FluidState amr::Block::* member_for(arch::state::StateSlot slot)
{
    using arch::state::StateSlot;
    switch (slot) {
    case StateSlot::Current: return &amr::Block::fluid_state;
    case StateSlot::Next: return &amr::Block::state_next;
    case StateSlot::Scratch: return &amr::Block::state_scratch;
    }
    throw std::logic_error("RKL descriptor selected unknown slot member");
}

inline DiffFunction::RKLOrder order_for(arch::scheduler::RklMethod method)
{
    using arch::scheduler::RklMethod;
    if (method == RklMethod::RKL1)
        return DiffFunction::RKLOrder::First;
    if (method == RklMethod::RKL2)
        return DiffFunction::RKLOrder::Second;
    throw std::invalid_argument("unknown shared RKL method");
}

inline void rotate_single_block(amr::Block& block,
                                arch::state::SlotRotation rotation)
{
    using arch::state::StateSlot;
    if (rotation.current_from == StateSlot::Next
        && rotation.next_from == StateSlot::Current
        && rotation.scratch_from == StateSlot::Scratch) {
        std::swap(block.fluid_state, block.state_next);
        return;
    }
    if (rotation.current_from == StateSlot::Scratch
        && rotation.next_from == StateSlot::Next
        && rotation.scratch_from == StateSlot::Current) {
        std::swap(block.fluid_state, block.state_scratch);
        return;
    }
    throw std::logic_error("RKL physical rotation descriptor mismatch");
}

template <typename EosType, typename BCPolicy>
inline void advance_single_rkl(
    amr::Block& block, const EosType& eos, const Grid& grid,
    const SimConfig& config, double dt, double dt_diff_fe,
    BCPolicy& boundary_condition, arch::scheduler::RklMethod method)
{
    using namespace arch::scheduler;
    using arch::state::StateSlot;
    const DiffFunction::RKLOrder order = order_for(method);
    const int stages = DiffFunction::compute_stages(
        order, dt, dt_diff_fe, config.physics.diffusion.diff_cfl,
        config.physics.diffusion.max_stages);
    if (stages <= 0) return;

    const StageBinding& binding = current_stage_binding();
    if (binding.handles.size() != 1)
        throw std::logic_error("single RKL requires one scheduler handle");
    const auto current = binding.context.ledger.inspect(
        {binding.handles.front(), StateSlot::Current});
    if (!arch::state::side_can_read(current.ghost.residency,
                                    binding.context.side)
        || current.ghost.version != current.interior.version
        || current.ghost_source_version != current.interior.version) {
        boundary_condition.apply(block.fluid_state, grid);
        (void)complete_boundary(
            binding.context, binding.handles, StateSlot::Current,
            current.interior.version,
            [](StateSlot, arch::state::StateVersion,
               arch::state::CompletionToken token) { return token; });
    }

    (void)copy_slot(
        binding.context, binding.handles, StateSlot::Current,
        StateSlot::Scratch,
        [&] { block.state_scratch = block.fluid_state; });
    (void)copy_slot(
        binding.context, binding.handles, StateSlot::Current,
        StateSlot::Next,
        [&] { block.state_next = block.fluid_state; });

    FluidState increment_previous;
    FluidState increment_initial;
    for (FluidState* workspace : {&increment_previous, &increment_initial}) {
        workspace->Preallocate(grid.GetTotalSize());
        workspace->InitSpecies(block.fluid_state.GetNumSpecies());
    }

    const auto executor =
            [&](const RklPlan& selected_plan,
                const RklStageDescriptor& descriptor,
                arch::state::CompletionToken token) {
                FluidState& state_n = state_for(block, descriptor.state_n_slot);
                FluidState& previous = state_for(block, descriptor.previous_slot);
                FluidState& older = state_for(block, descriptor.older_slot);
                FluidState& output = state_for(block, descriptor.output_slot);
                const DiffFunction::RKLCoeffs coefficients =
                    DiffFunction::get_rkl_coeffs(
                        order, descriptor.stage, stages);

                if (descriptor.stage == 1) {
                    DiffFlux::compute_diffusion_operator(
                        state_n, increment_initial, eos, grid, config);
#pragma omp parallel for schedule(static)
                    for (int index = 0; index < grid.GetTotalSize(); ++index) {
                        output.rho[index] = ARCH_DIFFUSION_FIRST_RKL_COMPONENT(
                            state_n.rho[index], coefficients.tilde_mu * dt,
                            increment_initial.rho[index]);
                        output.mom_u[index] = ARCH_DIFFUSION_FIRST_RKL_COMPONENT(
                            state_n.mom_u[index], coefficients.tilde_mu * dt,
                            increment_initial.mom_u[index]);
                        output.mom_v[index] = ARCH_DIFFUSION_FIRST_RKL_COMPONENT(
                            state_n.mom_v[index], coefficients.tilde_mu * dt,
                            increment_initial.mom_v[index]);
                        output.mom_w[index] = ARCH_DIFFUSION_FIRST_RKL_COMPONENT(
                            state_n.mom_w[index], coefficients.tilde_mu * dt,
                            increment_initial.mom_w[index]);
                        output.eng[index] = ARCH_DIFFUSION_FIRST_RKL_COMPONENT(
                            state_n.eng[index], coefficients.tilde_mu * dt,
                            increment_initial.eng[index]);
                        for (int species = 0;
                             species < state_n.GetNumSpecies(); ++species) {
                            const double rho_x =
                                ARCH_DIFFUSION_FIRST_RKL_SPECIES_EXPRESSION(
                                    state_n.rho[index],
                                    state_n.X(species, index),
                                    coefficients.tilde_mu * dt,
                                    increment_initial.X(species, index));
                            output.X(species, index) =
                                rho_x / output.rho[index];
                        }
                    }
                    return token;
                }

                DiffFlux::compute_diffusion_operator(
                    previous, increment_previous, eos, grid, config);
                const double initial_weight = selected_plan.second_order
                    ? 1.0 - coefficients.mu - coefficients.nu : 0.0;
#pragma omp parallel for schedule(static)
                for (int index = 0; index < grid.GetTotalSize(); ++index) {
                    const double rho_older = older.rho[index];
                    if (selected_plan.second_order) {
                        output.rho[index] =
                            ARCH_DIFFUSION_UNSCALED_RKL2_COMPONENT(
                                coefficients, previous.rho[index],
                                older.rho[index], initial_weight,
                                state_n.rho[index],
                                increment_previous.rho[index],
                                increment_initial.rho[index], dt);
                        output.mom_u[index] =
                            ARCH_DIFFUSION_UNSCALED_RKL2_COMPONENT(
                                coefficients, previous.mom_u[index],
                                older.mom_u[index], initial_weight,
                                state_n.mom_u[index],
                                increment_previous.mom_u[index],
                                increment_initial.mom_u[index], dt);
                        output.mom_v[index] =
                            ARCH_DIFFUSION_UNSCALED_RKL2_COMPONENT(
                                coefficients, previous.mom_v[index],
                                older.mom_v[index], initial_weight,
                                state_n.mom_v[index],
                                increment_previous.mom_v[index],
                                increment_initial.mom_v[index], dt);
                        output.mom_w[index] =
                            ARCH_DIFFUSION_UNSCALED_RKL2_COMPONENT(
                                coefficients, previous.mom_w[index],
                                older.mom_w[index], initial_weight,
                                state_n.mom_w[index],
                                increment_previous.mom_w[index],
                                increment_initial.mom_w[index], dt);
                        output.eng[index] =
                            ARCH_DIFFUSION_UNSCALED_RKL2_COMPONENT(
                                coefficients, previous.eng[index],
                                older.eng[index], initial_weight,
                                state_n.eng[index],
                                increment_previous.eng[index],
                                increment_initial.eng[index], dt);
                    } else {
                        output.rho[index] =
                            ARCH_DIFFUSION_UNSCALED_RKL1_COMPONENT(
                                coefficients, previous.rho[index],
                                older.rho[index],
                                increment_previous.rho[index], dt);
                        output.mom_u[index] =
                            ARCH_DIFFUSION_UNSCALED_RKL1_COMPONENT(
                                coefficients, previous.mom_u[index],
                                older.mom_u[index],
                                increment_previous.mom_u[index], dt);
                        output.mom_v[index] =
                            ARCH_DIFFUSION_UNSCALED_RKL1_COMPONENT(
                                coefficients, previous.mom_v[index],
                                older.mom_v[index],
                                increment_previous.mom_v[index], dt);
                        output.mom_w[index] =
                            ARCH_DIFFUSION_UNSCALED_RKL1_COMPONENT(
                                coefficients, previous.mom_w[index],
                                older.mom_w[index],
                                increment_previous.mom_w[index], dt);
                        output.eng[index] =
                            ARCH_DIFFUSION_UNSCALED_RKL1_COMPONENT(
                                coefficients, previous.eng[index],
                                older.eng[index],
                                increment_previous.eng[index], dt);
                    }
                    for (int species = 0;
                         species < state_n.GetNumSpecies(); ++species) {
                        const double rho_x = selected_plan.second_order
                            ? ARCH_DIFFUSION_UNSCALED_RKL2_SPECIES_EXPRESSION(
                                  coefficients, previous.rho[index],
                                  previous.X(species, index),
                                  rho_older, older.X(species, index),
                                  initial_weight, state_n.rho[index],
                                  state_n.X(species, index),
                                  increment_previous.X(species, index),
                                  increment_initial.X(species, index), dt)
                            : ARCH_DIFFUSION_UNSCALED_RKL1_SPECIES_EXPRESSION(
                                  coefficients, previous.rho[index],
                                  previous.X(species, index),
                                  rho_older, older.X(species, index),
                                  increment_previous.X(species, index), dt);
                        output.X(species, index) = rho_x / output.rho[index];
                    }
                }
                return token;
            };
    const auto reflux =
            [](const RklPlan&, const RklStageDescriptor&,
               arch::state::CompletionToken token) { return token; };
    const auto boundary =
            [&](StateSlot output, arch::state::StateVersion,
                arch::state::CompletionToken token) {
                boundary_condition.apply(state_for(block, output), grid);
                return token;
            };
    const auto rotate = [&](arch::state::SlotRotation rotation) {
        rotate_single_block(block, rotation);
    };
    if (method == RklMethod::RKL1) {
        (void)execute_single_rkl1_lane(
            binding.context, binding.handles, stages, executor, reflux,
            boundary, rotate);
    } else {
        (void)execute_single_rkl2_lane(
            binding.context, binding.handles, stages, executor, reflux,
            boundary, rotate);
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
                            const SimConfig& config,
                            arch::scheduler::RklMethod method)
{
    using namespace arch::scheduler;
    using arch::state::StateSlot;
    const DiffFunction::RKLOrder order = detail::order_for(method);
    const auto& active_blocks = amr_ctrl.tree->GetActiveBlocks();
    if (active_blocks.empty()) return;

    const int max_stages = config.physics.diffusion.max_stages;
    const double diff_cfl = config.physics.diffusion.diff_cfl;
    const int stages = DiffFunction::compute_stages(order, dt, dt_diff_fe, diff_cfl, max_stages);
    if (stages <= 0) return;

    const StageBinding& binding = current_stage_binding();
    if (binding.handles.size() != active_blocks.size())
        throw std::logic_error("AMR RKL scheduler handle count mismatch");
    amr_ctrl.flux_register.EnsureSpecies(
        amr_ctrl.pool->GetBlock(active_blocks.front()).fluid_state.GetNumSpecies());
    detail::synchronize(amr_ctrl, boundary_condition, &amr::Block::fluid_state);
    const arch::state::StateVersion current_version =
        binding.context.ledger.inspect(
            {binding.handles.front(), StateSlot::Current}).interior.version;
    (void)complete_boundary(
        binding.context, binding.handles, StateSlot::Current,
        current_version,
        [](StateSlot, arch::state::StateVersion,
           arch::state::CompletionToken token) { return token; });

    (void)copy_slot(
        binding.context, binding.handles, StateSlot::Current,
        StateSlot::Scratch, [&] {
#pragma omp parallel for schedule(static)
            for (size_t index = 0; index < active_blocks.size(); ++index) {
                amr::Block& block =
                    amr_ctrl.pool->GetBlock(active_blocks[index]);
                block.state_scratch = block.fluid_state;
            }
        });
    (void)copy_slot(
        binding.context, binding.handles, StateSlot::Current,
        StateSlot::Next, [&] {
#pragma omp parallel for schedule(static)
            for (size_t index = 0; index < active_blocks.size(); ++index) {
                amr::Block& block =
                    amr_ctrl.pool->GetBlock(active_blocks[index]);
                block.state_next = block.fluid_state;
            }
        });

    const auto executor =
            [&](const RklPlan& selected_plan,
                const RklStageDescriptor& descriptor,
                arch::state::CompletionToken token) {
                const DiffFunction::RKLCoeffs coefficients =
                    DiffFunction::get_rkl_coeffs(
                        order, descriptor.stage, stages);
                amr_ctrl.flux_register.Clear();
                for (const int block_id : active_blocks) {
                    amr::Block& block = amr_ctrl.pool->GetBlock(block_id);
                    FluidState& state_n =
                        detail::state_for(block, descriptor.state_n_slot);
                    FluidState& previous =
                        detail::state_for(block, descriptor.previous_slot);
                    FluidState& older =
                        detail::state_for(block, descriptor.older_slot);
                    FluidState& output =
                        detail::state_for(block, descriptor.output_slot);
                    std::vector<FluidVector> d_previous;
                    std::vector<double> d_species_previous;
                    detail::evaluate_diffusion_increment(
                        amr_ctrl, block_id, previous, eos, block.grid,
                        config, dt, coefficients.tilde_mu, d_previous,
                        d_species_previous);

                    if (descriptor.stage == 1) {
                        detail::apply_first_rkl_stage(
                            state_n, output, d_previous,
                            d_species_previous, block.grid,
                            coefficients.tilde_mu);
                        continue;
                    }

                    std::vector<FluidVector> d_initial;
                    std::vector<double> d_species_initial;
                    if (selected_plan.second_order) {
                        detail::evaluate_diffusion_increment(
                            amr_ctrl, block_id, state_n, eos, block.grid,
                            config, dt, coefficients.gamma, d_initial,
                            d_species_initial);
                    } else {
                        d_initial.assign(block.grid.GetTotalSize(),
                                         FluidVector{});
                        d_species_initial.assign(
                            static_cast<size_t>(state_n.GetNumSpecies())
                                * block.grid.GetTotalSize(),
                            0.0);
                    }
                    detail::apply_recursive_rkl_stage(
                        state_n, previous, older, output, d_previous,
                        d_species_previous, d_initial, d_species_initial,
                        block.grid, coefficients,
                        selected_plan.second_order);
                }
                return token;
            };
    const auto reflux =
            [&](const RklPlan&, const RklStageDescriptor& descriptor,
                arch::state::CompletionToken token) {
                amr_ctrl.ApplyReflux(
                    dt, detail::member_for(descriptor.output_slot));
                return token;
            };
    const auto boundary =
            [&](StateSlot output, arch::state::StateVersion,
                arch::state::CompletionToken token) {
                detail::synchronize(amr_ctrl, boundary_condition,
                                    detail::member_for(output));
                return token;
            };
    const auto rotate = [&](arch::state::SlotRotation rotation) {
#pragma omp parallel for schedule(static)
                for (size_t index = 0; index < active_blocks.size(); ++index) {
                    amr::Block& block =
                        amr_ctrl.pool->GetBlock(active_blocks[index]);
                    detail::rotate_single_block(block, rotation);
                }
            };
    if (method == RklMethod::RKL1) {
        (void)execute_multi_rkl1_lane(
            binding.context, binding.handles, stages, executor, reflux,
            boundary, rotate);
    } else {
        (void)execute_multi_rkl2_lane(
            binding.context, binding.handles, stages, executor, reflux,
            boundary, rotate);
    }
    detail::synchronize(amr_ctrl, boundary_condition, &amr::Block::fluid_state);
    const arch::state::StateVersion final_version =
        binding.context.ledger.inspect(
            {binding.handles.front(), StateSlot::Current}).interior.version;
    (void)complete_boundary(
        binding.context, binding.handles, StateSlot::Current, final_version,
        [](StateSlot, arch::state::StateVersion,
           arch::state::CompletionToken token) { return token; });
}

} // namespace Numerics::Diffusion
