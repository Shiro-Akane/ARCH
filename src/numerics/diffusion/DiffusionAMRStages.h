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
 * 3. Evaluate the same RKL polynomial in centered conserved increments,
 *    retaining exact stationary identities and original operator dt grouping.
 * 4. Return a conservative composite hierarchy state to the driver.
 * Native RZ requires a Host scheduler binding and Runtime post-boundary EOS
 * owner before any stage copy or flux mutation. Its provisional stage/reflux
 * checks preserve V/W means and RzVolumeAngular receipts without point-energy
 * repair; actual completed boundary/exchange supplies thermal acceptance.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "amr/flux/AMRFluxRegistering.h"
#include "driver/schedule/StageScheduler.h"
#include "numerics/diffusion/DiffFlux.h"
#include "numerics/diffusion/DiffFunction.h"
#include "numerics/integrator/TimeIntegratorHelper.h"

namespace Numerics::Diffusion {

namespace detail {

/** Require native execution ownership before any RKL output or flux mutation.
 * Callback presence is a preflight only; the actual Runtime callback checks
 * its ledger, slot/version, domain and bound EOS after real boundary exchange.
 */
inline void require_native_stage_binding(GridMetrics::GeometrySemantics semantics,
                                         std::size_t expected_handles)
{
    if(semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz) return;
    const auto& binding=arch::scheduler::current_stage_binding();
    if(binding.context.side!=arch::state::ExecutionSide::Host)
        throw std::logic_error("Native RZ RKL requires a Host stage binding");
    if(!binding.context.post_boundary_acceptance)
        throw std::logic_error("Native RZ RKL requires post-boundary acceptance");
    if(expected_handles==0||binding.handles.size()!=expected_handles)
        throw std::logic_error("Native RZ RKL stage domain extent mismatch");
}

template<typename BCPolicy>
inline void validate_geometry(const Grid& grid,const BCPolicy& boundary,
    GridMetrics::GeometrySemantics semantics)
{
    (void)GridMetrics::make_geometry_view(grid,semantics);
    if constexpr(requires { boundary.geometry_semantics(); }) {
        if(boundary.geometry_semantics()!=semantics)
            throw std::invalid_argument("Diffusion/boundary chart mismatch");
    } else if(semantics==GridMetrics::GeometrySemantics::AxisymmetricRz)
        throw std::invalid_argument("RZ diffusion boundary identity missing");
    if constexpr(requires { boundary.logical_plan(grid); })
        (void)boundary.logical_plan(grid);
    else if(semantics==GridMetrics::GeometrySemantics::AxisymmetricRz)
        throw std::invalid_argument("RZ diffusion boundary preflight missing");
}

// First-stage expression leaves retain their original arithmetic. Recursive
// Host and device consumers use the shared centered scalar authority below.
#define ARCH_DIFFUSION_FIRST_RKL_COMPONENT(                                \
    state_n, coefficient, increment)                                       \
    ((state_n) + (coefficient) * (increment))

#define ARCH_DIFFUSION_FIRST_RKL_SPECIES_EXPRESSION(                       \
    rho_n, species_n, coefficient, species_increment)                      \
    ((rho_n) * (species_n) + (coefficient) * (species_increment))

/** Check finite arithmetic controls before preserving a stationary value.
 * Workflow: first stage checks its actual coefficient only; recursion checks
 * mu/nu/tilde_mu and RKL2 gamma. Unscaled recursion also uses dt; scaled
 * recursion and RKL1 initial operator controls are mathematically unused.
 * This exact finite guard prevents malformed controls from gaining a finite
 * output through the stationary shortcut; no magnitude bound is introduced.
 */
ARCH_INLINE bool finite_rkl_arithmetic_controls(
    const DiffFunction::RKLCoeffs& coefficients, double dt,
    double first_coefficient, bool recursive, bool second_order,
    bool increments_are_scaled)
{
    if (!recursive) return std::isfinite(first_coefficient);
    return std::isfinite(coefficients.mu) && std::isfinite(coefficients.nu)
        && std::isfinite(coefficients.tilde_mu)
        && (!second_order || std::isfinite(coefficients.gamma))
        && (increments_are_scaled || std::isfinite(dt));
}

/** Exact stationary-polynomial identity shared by every Host/device adapter.
 * Workflow: inspect only finite seed, genuinely used history and zero RHS.
 * For recursive RKL, mu+nu+(1-mu-nu)=1 (RKL1: mu+nu=1), so
 * equal stage values with zero operators reproduce the seed exactly.
 * No numerical magnitude test is used. The shared used-controls guard must
 * succeed. First-stage history and first-order initial RHS are mathematically
 * unused inputs and are deliberately ignored.
 */
ARCH_INLINE bool exact_stationary_rkl_value(
    double seed, double previous, double older,
    double increment_previous, double increment_initial,
    bool recursive, bool second_order, bool controls_valid)
{
    if (!controls_valid || !std::isfinite(seed) || increment_previous != 0.0)
        return false;
    if (!recursive) return true;
    return previous == seed && older == seed
        && (!second_order || increment_initial == 0.0);
}

/** Preserve each independently stationary conserved component after the
 * original affine expression; all active values retain its exact grouping.
 * Histories are passed by value so destination may alias an older buffer.
 */
ARCH_INLINE FluidVector preserve_stationary_rkl_hydro(
    FluidVector candidate, FluidVector seed, FluidVector previous,
    FluidVector older, FluidVector increment_previous,
    FluidVector increment_initial, bool recursive, bool second_order,
    bool controls_valid)
{
    if (exact_stationary_rkl_value(seed.rho, previous.rho, older.rho,
            increment_previous.rho, increment_initial.rho, recursive, second_order,
            controls_valid))
        candidate.rho = seed.rho;
    if (exact_stationary_rkl_value(seed.mom_u, previous.mom_u, older.mom_u,
            increment_previous.mom_u, increment_initial.mom_u, recursive, second_order,
            controls_valid))
        candidate.mom_u = seed.mom_u;
    if (exact_stationary_rkl_value(seed.mom_v, previous.mom_v, older.mom_v,
            increment_previous.mom_v, increment_initial.mom_v, recursive, second_order,
            controls_valid))
        candidate.mom_v = seed.mom_v;
    if (exact_stationary_rkl_value(seed.mom_w, previous.mom_w, older.mom_w,
            increment_previous.mom_w, increment_initial.mom_w, recursive, second_order,
            controls_valid))
        candidate.mom_w = seed.mom_w;
    if (exact_stationary_rkl_value(seed.eng, previous.eng, older.eng,
            increment_previous.eng, increment_initial.eng, recursive, second_order,
            controls_valid))
        candidate.eng = seed.eng;
    return candidate;
}

/** Check the exact conserved-species identity before fraction publication.
 * Positive finite seed density, unchanged output density and all used rho/X
 * histories are required. Callers retain the original rhoX/rho division for
 * active composition at its original expression site; no arithmetic moves
 * across this predicate boundary. No normalization or simplex validation.
 */
ARCH_INLINE bool exact_stationary_rkl_fraction(
    double output_rho,
    double seed_rho, double seed_fraction,
    double previous_rho, double previous_fraction,
    double older_rho, double older_fraction,
    double increment_previous, double increment_initial,
    bool recursive, bool second_order, bool controls_valid)
{
    return controls_valid && std::isfinite(seed_rho) && seed_rho > 0.0
        && output_rho == seed_rho
        && (!recursive || (previous_rho == seed_rho && older_rho == seed_rho))
        && exact_stationary_rkl_value(seed_fraction, previous_fraction,
            older_fraction, increment_previous, increment_initial,
            recursive, second_order, controls_valid);
}

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
    const FluidVector seed = state_n;
    const FluidVector source_increment = increment;
    const bool controls_valid = finite_rkl_arithmetic_controls(
        DiffFunction::RKLCoeffs{}, 0.0, coefficient, false, false, true);
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
    destination = preserve_stationary_rkl_hydro(destination, seed, seed, seed,
        source_increment, FluidVector{}, false, false, controls_valid);
    for (int species = 0; species < species_count; ++species) {
        const int offset = species * species_stride;
        const double seed_fraction = species_n[offset];
        const double species_rhs = species_increment[offset];
        const double rhoX = ARCH_DIFFUSION_FIRST_RKL_SPECIES_EXPRESSION(
            seed.rho, seed_fraction, coefficient, species_rhs);
        destination_species[offset] = exact_stationary_rkl_fraction(destination.rho,
            seed.rho, seed_fraction, seed.rho, seed_fraction,
            seed.rho, seed_fraction, species_rhs, 0.0, false, false, controls_valid)
            ? seed_fraction : rhoX / destination.rho;
    }
}

/** Evaluate the original recursive polynomial around its conserved base.
 * Workflow: reject every nonfinite used control/history/RHS first; preserve
 * the exact stationary identity; then use RKL2 U0+mu*(Up-U0)+nu*(Uo-U0),
 * or RKL1 Up+nu*(Uo-Up). RKL1 does not inspect U0, L0 or gamma.
 * Scaled and unscaled operator terms retain their original multiplication
 * and addition grouping. Only finite-input intermediate range failure uses
 * the equivalent original affine form; no clipping or normalization occurs.
 */
template <bool IncrementsAreScaled>
ARCH_INLINE double evaluate_recursive_rkl_component(
    double state_n, double state_previous, double state_older,
    double increment_previous, double increment_initial,
    const DiffFunction::RKLCoeffs& coefficients, bool second_order,
    double dt)
{
    const bool controls_valid = finite_rkl_arithmetic_controls(
        coefficients, dt, 0.0, true, second_order, IncrementsAreScaled);
    if (!controls_valid || !std::isfinite(state_previous)
        || !std::isfinite(state_older) || !std::isfinite(increment_previous)
        || (second_order && (!std::isfinite(state_n)
                             || !std::isfinite(increment_initial))))
        return arch::state::invalid();
    const double seed = second_order ? state_n : state_previous;
    if (exact_stationary_rkl_value(seed, state_previous, state_older,
            increment_previous, increment_initial, true, second_order, controls_valid))
        return seed;
    const double difference_previous = second_order ? state_previous - seed : 0.0;
    const double difference_older = state_older - seed;
    double centered = second_order
        ? seed + coefficients.mu * difference_previous
               + coefficients.nu * difference_older
        : seed + coefficients.nu * difference_older;
    if constexpr (IncrementsAreScaled) {
        centered = centered + coefficients.tilde_mu * increment_previous;
        if (second_order) centered = centered + coefficients.gamma * increment_initial;
    } else if (second_order) {
        centered = centered + dt * (coefficients.tilde_mu * increment_previous
                                    + coefficients.gamma * increment_initial);
    } else {
        centered = centered + coefficients.tilde_mu * dt * increment_previous;
    }
    if (std::isfinite(difference_previous) && std::isfinite(difference_older)
        && std::isfinite(centered)) return centered;

    // Finite differences may overflow although the original affine sum is
    // representable. Keep that range fallback's original operator grouping.
    double affine = coefficients.mu * state_previous + coefficients.nu * state_older;
    if (second_order)
        affine = affine + (1.0 - coefficients.mu - coefficients.nu) * state_n;
    if constexpr (IncrementsAreScaled) {
        affine = affine + coefficients.tilde_mu * increment_previous;
        if (second_order) affine = affine + coefficients.gamma * increment_initial;
    } else if (second_order) {
        affine = affine + dt * (coefficients.tilde_mu * increment_previous
                                + coefficients.gamma * increment_initial);
    } else {
        affine = affine + coefficients.tilde_mu * dt * increment_previous;
    }
    return std::isfinite(affine) ? affine : arch::state::invalid();
}

/** Apply the same conserved scalar recurrence to all five fluid components. */
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

/** Form each used rho*X before the identical conserved scalar recurrence.
 * Fractions are neither normalized nor clipped. RKL1 does not inspect its
 * initial rho/X or operator; publication divides by the actual updated rho.
 */
template <bool IncrementsAreScaled>
ARCH_INLINE double evaluate_recursive_rkl_species_cell(
    double rho_n, double species_n, double rho_previous,
    double species_previous, double rho_older, double species_older,
    double increment_previous, double increment_initial,
    const DiffFunction::RKLCoeffs& coefficients, bool second_order,
    double dt)
{
    if (!std::isfinite(rho_previous) || !std::isfinite(species_previous)
        || !std::isfinite(rho_older) || !std::isfinite(species_older)
        || (second_order && (!std::isfinite(rho_n) || !std::isfinite(species_n))))
        return arch::state::invalid();
    const double previous = rho_previous * species_previous;
    const double older = rho_older * species_older;
    const double seed = second_order ? rho_n * species_n : 0.0;
    return evaluate_recursive_rkl_component<IncrementsAreScaled>(
        seed, previous, older, increment_previous, increment_initial,
        coefficients, second_order, dt);
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
    const bool controls_valid = finite_rkl_arithmetic_controls(
        coefficients, dt, 0.0, true, second_order, IncrementsAreScaled);
    destination = evaluate_recursive_rkl_hydro_cell<IncrementsAreScaled>(
        state_n, state_previous, state_older, increment_previous,
        increment_initial, coefficients, second_order, dt);
    for (int species = 0; species < species_count; ++species) {
        const int offset = species * species_stride;
        const double previous_fraction = species_previous[offset];
        const double seed_fraction = second_order ? species_n[offset] : previous_fraction;
        const double older_fraction = species_older[offset];
        const double species_rhs = species_increment_previous[offset];
        const double initial_rhs = second_order && species_increment_initial != nullptr
            ? species_increment_initial[offset] : 0.0;
        const double rhoX =
            evaluate_recursive_rkl_species_cell<IncrementsAreScaled>(
                state_n.rho, seed_fraction, state_previous.rho,
                previous_fraction, state_older.rho, older_fraction,
                species_rhs, initial_rhs, coefficients, second_order, dt);
        destination_species[offset] = exact_stationary_rkl_fraction(destination.rho,
            second_order ? state_n.rho : state_previous.rho, seed_fraction,
            state_previous.rho, previous_fraction, state_older.rho, older_fraction,
            species_rhs, initial_rhs, true, second_order, controls_valid)
            ? seed_fraction : rhoX / destination.rho;
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
                                         std::vector<double>& d_species,
                                         bool capture_budget = true,
                                         GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing)
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
        DiffFlux::compute_fluxes(state, eos, grid, config, flux_buffer, species_flux_buffer, dir, capture_budget, semantics);
        TimeIntegration::accumulate_divergence(dU, d_species, flux_buffer, species_flux_buffer,
                                               grid, dt, dir, n_species, semantics,
            semantics==GridMetrics::GeometrySemantics::AxisymmetricRz);
        amr::RegisterCoarseFineFluxes(amr_ctrl, block_id, grid, dir, flux_buffer,
                                      species_flux_buffer, n_species, flux_weight, semantics,
            semantics==GridMetrics::GeometrySemantics::AxisymmetricRz);
    }

    DiffFlux::add_geometric_sources(dU, state, eos, grid, config, dt, semantics);
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

    // One logical row has one independent task: its i cells stay in this
    // thread. Avoid a team whose other workers have no row to publish.
#pragma omp parallel for schedule(static) if ((ke - ks) * (je - js) > 1)
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
    // Match the first-stage row ownership without changing cell arithmetic.
#pragma omp parallel for schedule(static) if ((ke - ks) * (je - js) > 1)
    for (int kj = 0; kj < (ke - ks) * (je - js); ++kj) {
        const int k = ks + kj / (je - js);
        const int j = js + kj % (je - js);
        for (int i = grid.Is(); i < grid.Ie(); ++i) {
            const int idx = grid.GetIndex(i, j, k);
            FluidVector updated;
            // The adapter captures all fluid values by value; every old rhoX
            // is read before destination.set can replace an aliased older rho.
            apply_recursive_rkl_stage_cell_impl<true>(
                second_order ? state_n.get(idx) : FluidVector{},
                second_order && n_species > 0 ? state_n.mass_fractions.data() + idx : nullptr,
                state_previous.get(idx),
                n_species > 0 ? state_previous.mass_fractions.data() + idx : nullptr,
                state_older.get(idx),
                n_species > 0 ? state_older.mass_fractions.data() + idx : nullptr,
                d_previous[idx], n_species > 0 ? d_species_previous.data() + idx : nullptr,
                second_order ? d_initial[idx] : FluidVector{},
                second_order && n_species > 0 ? d_species_initial.data() + idx : nullptr,
                n_species, total_size, coeffs, second_order, 0.0, updated,
                n_species > 0 ? destination.mass_fractions.data() + idx : nullptr);
            destination.set(idx, updated);
        }
    }
}

template <typename BCPolicy>
inline void synchronize(amr::AMRControl& amr_ctrl, BCPolicy& boundary_condition,
                        FluidState amr::Block::* state_ptr,
                        GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing,
                        arch::state::Bounds bounds = {})
{
    const auto& binding = arch::scheduler::current_stage_binding();
    const auto& active_blocks = amr_ctrl.tree->GetActiveBlocks();
    if (binding.handles.size() != active_blocks.size())
        throw std::logic_error("RKL exchange handle count mismatch");
    TimeIntegration::synchronize_domain_boundary(amr_ctrl, boundary_condition,
        state_ptr, binding.handles, semantics, bounds);
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
    BCPolicy& boundary_condition, arch::scheduler::RklMethod method,
    GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing,
    amr::AMRControl* native_owner = nullptr)
{
    validate_geometry(grid,boundary_condition,semantics);
    require_native_stage_binding(semantics,1);
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
    // Native single-patch synchronization borrows the actual domain owner.
    // Validate it after the original native scheduler preflight and before
    // any slot, ghost, or copy writes. Existing never requires this owner.
    if (semantics==GridMetrics::GeometrySemantics::AxisymmetricRz) {
        if (!native_owner || !native_owner->pool || !native_owner->tree
            || native_owner->tree->GetRootGridDim()!=2
            || native_owner->tree->GetActiveBlocks().size()!=1
            || &native_owner->pool->GetBlock(native_owner->tree->GetActiveBlocks().front())!=&block
            || &grid!=&block.grid)
            throw std::logic_error("Native single RKL requires its actual one-patch AMR owner");
        const auto actual_handles=native_owner->ActiveHandles();
        if (actual_handles.size()!=1 || actual_handles.front()!=binding.handles.front())
            throw std::logic_error("Native single RKL owner/scheduler handle mismatch");
        const int extent=grid.GetTotalSize();
        const int species=block.fluid_state.GetNumSpecies();
        const auto& state=block.fluid_state;
        if (extent<=0 || species<0 || state.block_total_size_!=extent
            || state.rho.size()!=static_cast<std::size_t>(extent)
            || state.mom_u.size()!=state.rho.size() || state.mom_v.size()!=state.rho.size()
            || state.mom_w.size()!=state.rho.size() || state.eng.size()!=state.rho.size()
            || state.enuc_rate.size()!=state.rho.size()
            || state.mass_fractions.size()!=static_cast<std::size_t>(species)*extent)
            throw std::logic_error("Native single RKL actual state/grid layout mismatch");
    }
    /** Synchronize the selected actual slot; only Existing uses the old local fill. */
    const auto synchronize_single=[&](StateSlot slot) {
        if (semantics==GridMetrics::GeometrySemantics::AxisymmetricRz)
            TimeIntegration::synchronize_domain_boundary(*native_owner, boundary_condition,
                TimeIntegration::hydro_boundary_state_member(slot), binding.handles, semantics,
                {config.numerics.sml_rho,config.numerics.min_eint,config.numerics.max_eint});
        else boundary_condition.apply(state_for(block,slot),grid);
    };
    const auto current = binding.context.ledger.inspect(
        {binding.handles.front(), StateSlot::Current});
    if (!arch::state::side_can_read(current.ghost.residency,
                                    binding.context.side)
        || current.ghost.version != current.interior.version
        || current.ghost_source_version != current.interior.version) {
        synchronize_single(StateSlot::Current);
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
                output.stage_repairs.reset(output.GetNumSpecies(),
                    semantics==GridMetrics::GeometrySemantics::AxisymmetricRz
                        ? arch::state::RepairSemantics::RzVolumeAngular
                        : arch::state::RepairSemantics::ExistingVolume);
                const DiffFunction::RKLCoeffs coefficients =
                    DiffFunction::get_rkl_coeffs(
                        order, descriptor.stage, stages);
                const bool controls_valid = finite_rkl_arithmetic_controls(
                    coefficients, dt,
                    descriptor.stage == 1 ? coefficients.tilde_mu * dt : 0.0,
                    descriptor.stage != 1, selected_plan.second_order, false);

                if (descriptor.stage == 1) {
                    DiffFlux::compute_diffusion_operator(
                        state_n, increment_initial, eos, grid, config, semantics);
#pragma omp parallel for schedule(static)
                    for (int index = 0; index < grid.GetTotalSize(); ++index) {
                        const FluidVector seed = state_n.get(index);
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
                        output.set(index, preserve_stationary_rkl_hydro(
                            output.get(index), seed, seed, seed,
                            increment_initial.get(index), FluidVector{}, false, false,
                            controls_valid));
                        for (int species = 0;
                             species < state_n.GetNumSpecies(); ++species) {
                            const double rho_x =
                                ARCH_DIFFUSION_FIRST_RKL_SPECIES_EXPRESSION(
                                    state_n.rho[index],
                                    state_n.X(species, index),
                                    coefficients.tilde_mu * dt,
                                    increment_initial.X(species, index));
                            output.X(species, index) = exact_stationary_rkl_fraction(
                                output.rho[index], seed.rho,
                                state_n.X(species, index), seed.rho,
                                state_n.X(species, index), seed.rho,
                                state_n.X(species, index),
                                increment_initial.X(species, index), 0.0, false, false,
                                controls_valid) ? state_n.X(species, index) : rho_x / output.rho[index];
                        }
                    }
                    return token;
                }

                DiffFlux::compute_diffusion_operator(
                    previous, increment_previous, eos, grid, config, semantics);
#pragma omp parallel for schedule(static)
                for (int index = 0; index < grid.GetTotalSize(); ++index) {
                    const int species_count = state_n.GetNumSpecies();
                    FluidVector updated;
                    apply_recursive_rkl_stage_cell_impl<false>(
                        selected_plan.second_order ? state_n.get(index) : FluidVector{},
                        selected_plan.second_order && species_count > 0
                            ? state_n.mass_fractions.data() + index : nullptr,
                        previous.get(index), species_count > 0
                            ? previous.mass_fractions.data() + index : nullptr,
                        older.get(index), species_count > 0
                            ? older.mass_fractions.data() + index : nullptr,
                        increment_previous.get(index), species_count > 0
                            ? increment_previous.mass_fractions.data() + index : nullptr,
                        selected_plan.second_order ? increment_initial.get(index) : FluidVector{},
                        selected_plan.second_order && species_count > 0
                            ? increment_initial.mass_fractions.data() + index : nullptr,
                        species_count, grid.GetTotalSize(), coefficients,
                        selected_plan.second_order, dt, updated, species_count > 0
                            ? output.mass_fractions.data() + index : nullptr);
                    output.set(index, updated);
                }
                return token;
            };
    const auto reflux =
            [&](const RklPlan&, const RklStageDescriptor& descriptor,
                arch::state::CompletionToken token) {
                TimeIntegration::accept_stage_state(state_for(block, descriptor.output_slot),
                    grid, config.numerics,semantics);
                return token;
            };
    const auto boundary =
            [&](StateSlot output, arch::state::StateVersion,
                arch::state::CompletionToken token) {
                synchronize_single(output);
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
                            arch::scheduler::RklMethod method,
                            GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing)
{
    detail::require_native_stage_binding(semantics,amr_ctrl.tree->GetActiveBlocks().size());
    for(int id:amr_ctrl.tree->GetActiveBlocks())
        detail::validate_geometry(amr_ctrl.pool->GetBlock(id).grid,
            boundary_condition,semantics);
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
    detail::synchronize(amr_ctrl, boundary_condition, &amr::Block::fluid_state, semantics,
        {config.numerics.sml_rho,config.numerics.min_eint,config.numerics.max_eint});
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
                    output.stage_repairs.reset(output.GetNumSpecies(),
                        semantics==GridMetrics::GeometrySemantics::AxisymmetricRz
                            ? arch::state::RepairSemantics::RzVolumeAngular
                            : arch::state::RepairSemantics::ExistingVolume);
                    std::vector<FluidVector> d_previous;
                    std::vector<double> d_species_previous;
                    detail::evaluate_diffusion_increment(
                        amr_ctrl, block_id, previous, eos, block.grid,
                        config, dt, coefficients.tilde_mu, d_previous,
                        d_species_previous, /*capture_budget=*/true, semantics);

                    if (descriptor.stage == 1) {
                        detail::apply_first_rkl_stage(
                            state_n, output, d_previous,
                            d_species_previous, block.grid,
                            coefficients.tilde_mu);
                        TimeIntegration::accept_stage_state(output, block.grid, config.numerics,semantics);
                        continue;
                    }

                    std::vector<FluidVector> d_initial;
                    std::vector<double> d_species_initial;
                    if (selected_plan.second_order) {
                        detail::evaluate_diffusion_increment(
                            amr_ctrl, block_id, state_n, eos, block.grid,
                            config, dt, coefficients.gamma, d_initial,
                            d_species_initial, /*capture_budget=*/false, semantics);
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
                    TimeIntegration::accept_stage_state(output, block.grid, config.numerics,semantics);
                }
                return token;
            };
    const auto reflux =
            [&](const RklPlan&, const RklStageDescriptor& descriptor,
                arch::state::CompletionToken token) {
                amr_ctrl.ApplyReflux(
                    dt, detail::member_for(descriptor.output_slot), semantics,
                    semantics==GridMetrics::GeometrySemantics::AxisymmetricRz);
                TimeIntegration::accept_reflux_state(amr_ctrl,config.numerics,
                    detail::member_for(descriptor.output_slot), false,semantics);
                return token;
            };
    const auto boundary =
            [&](StateSlot output, arch::state::StateVersion,
                arch::state::CompletionToken token) {
                detail::synchronize(amr_ctrl, boundary_condition,
                                    detail::member_for(output), semantics,
                    {config.numerics.sml_rho,config.numerics.min_eint,config.numerics.max_eint});
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
    detail::synchronize(amr_ctrl, boundary_condition, &amr::Block::fluid_state, semantics,
        {config.numerics.sml_rho,config.numerics.min_eint,config.numerics.max_eint});
    const arch::state::StateVersion final_version =
        binding.context.ledger.inspect(
            {binding.handles.front(), StateSlot::Current}).interior.version;
    (void)complete_boundary(
        binding.context, binding.handles, StateSlot::Current, final_version,
        [](StateSlot, arch::state::StateVersion,
           arch::state::CompletionToken token) { return token; });
}

} // namespace Numerics::Diffusion
