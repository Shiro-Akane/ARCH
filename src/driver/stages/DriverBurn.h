/**
 * @file DriverBurn.h
 * @brief Host traversal for the shared split burn operation.
 *
 * Workflow:
 * 1. Bind active patches after their boundary states have been prepared.
 * 2. Distribute bounded cell ranges across one host worker team. Every cell
 *    uses DriverBurnPolicy preparation, the original ODE, and checked commit.
 * 3. Reduce each patch's timestep advice with the shared reduction contract.
 *
 * A range is an execution unit only: density, composition, temperature,
 * reaction integration and energy handoff retain their per-cell ownership.
 * The two Strang half-steps remain separate scheduler operations.
 */
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "data/FluidState.h"
#include "driver/stages/DriverBurnPolicy.h"
#include "grid/Grid.h"

namespace DriverBurn {

struct HostBurnPatch {
    FluidState* state;
    const Grid* grid;
};

/** Resolve the selected ODE extent without imposing a compact-network limit. */
template<class Burner>
int host_burn_state_size(const Burner& burn)
{
    if constexpr (requires { burn.state_size(); }) return burn.state_size();
    else if constexpr (requires { Burner::NEQ; }) return Burner::NEQ;
    else return 0;
}

/** Bounded exact-input reuse within one worker of ONE burn half-step.
 * The surrounding batch fixes EOS/table ownership, network, controls and dt.
 * Every rho and packed-state bit participates in lookup; collisions require
 * a full bitwise key match. Successful ODE outputs alone are reusable.
 */
class HostBurnMemo {
public:
    explicit HostBurnMemo(int extent)
        : extent_(extent), inputs_(slots * extent), outputs_(slots * extent) {}

    /** Solve once or replay the same accepted ODE result at an identical state. */
    template<class Eos, class Burner>
    bool integrate(std::vector<double>& packed, double rho, double interval,
                   const Eos& eos, Burner& burn, const BurnConfig& controls,
                   double& recommended, double& energy)
    {
        constexpr bool reusable = [] {
            if constexpr (requires { Burner::exact_input_reusable; })
                return Burner::exact_input_reusable;
            else return false;
        }();
        if constexpr (!reusable)
            return burn.integrate(packed.data(), rho, interval, eos, controls,
                                  recommended, &energy);
        std::uint64_t hash = mix(std::bit_cast<std::uint64_t>(rho))
                           ^ mix(std::bit_cast<std::uint64_t>(interval));
        for (double value : packed)
            hash = mix(hash ^ std::bit_cast<std::uint64_t>(value));
        const std::size_t slot = hash % slots, offset = slot * extent_;
        bool same = ready_[slot] && fingerprints_[slot] == hash
                 && densities_[slot] == std::bit_cast<std::uint64_t>(rho)
                 && intervals_[slot] == std::bit_cast<std::uint64_t>(interval);
        for (int i = 0; same && i < extent_; ++i)
            same = inputs_[offset + i] == std::bit_cast<std::uint64_t>(packed[i]);
        if (same) {
            std::copy_n(outputs_.data() + offset, extent_, packed.data());
            energy = energy_[slot]; recommended = recommended_[slot];
            return true;
        }
        ready_[slot] = false;
        for (int i = 0; i < extent_; ++i)
            inputs_[offset + i] = std::bit_cast<std::uint64_t>(packed[i]);
        if (!burn.integrate(packed.data(), rho, interval, eos, controls,
                            recommended, &energy)) return false;
        fingerprints_[slot] = hash;
        densities_[slot] = std::bit_cast<std::uint64_t>(rho);
        intervals_[slot] = std::bit_cast<std::uint64_t>(interval);
        std::copy_n(packed.data(), extent_, outputs_.data() + offset);
        energy_[slot] = energy; recommended_[slot] = recommended;
        ready_[slot] = true;
        return true;
    }

private:
    /** Mix key bits for slot selection; equality never depends on this hash. */
    static std::uint64_t mix(std::uint64_t value)
    {
        value ^= value >> 30;
        value *= 0xbf58476d1ce4e5b9ULL;
        value ^= value >> 27;
        value *= 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    }
    static constexpr std::size_t slots = 256;
    int extent_;
    std::vector<std::uint64_t> inputs_;
    std::vector<double> outputs_;
    std::array<std::uint64_t, slots> fingerprints_{}, densities_{}, intervals_{};
    std::array<double, slots> energy_{}, recommended_{};
    std::array<bool, slots> ready_{};
};

/** Integrate and publish one cell only after all physical checks have passed. */
template<class Eos, class Burner>
double advance_host_burn_cell(FluidState& state, int cell, double interval,
    const Eos& eos, Burner& burn, const SimConfig& config,
    const BurnConfigView& controls, std::vector<double>& packed, HostBurnMemo& memo)
{
    FluidVector fluid = state.get(cell);
    if (check_burn_density(fluid, controls) == BurnCellDisposition::BelowDensity)
        return INACTIVE_LIMITER_CANDIDATE;
    std::fill(packed.begin(), packed.end(), 0.0);
    state.get_species_to_buffer(cell, packed.data());
    const int species = state.GetNumSpecies();
    const auto prepared = prepare_burn_cell(
        fluid, packed.data(), species, eos, controls);
    if (prepared.disposition == BurnCellDisposition::BelowTemperature)
        return INACTIVE_LIMITER_CANDIDATE;
    if (prepared.disposition == BurnCellDisposition::InvalidComposition)
        throw std::runtime_error("Invalid complete composition before burn: cell="
            + std::to_string(cell) + ", sum(X)=" + std::to_string(prepared.composition_sum)
            + ", network=" + config.physics.burn.network_name);
    if (prepared.disposition == BurnCellDisposition::SolverFailed)
        throw std::runtime_error("Invalid thermodynamic state before burn at cell "
                                 + std::to_string(cell));

    double recommended = interval, energy_change = 0.0;
    if (!memo.integrate(packed, fluid.rho, interval, eos, burn,
                        config.physics.burn, recommended, energy_change))
        throw std::runtime_error("Burn solver failed at cell " + std::to_string(cell));
    // The ODE owns integral(delta e_nuc); the existing first-law handoff
    // combines it with the initial thermal energy and unchanged kinetic part.
    const auto handoff = compute_burn_energy_handoff(
        fluid, packed.data(), species, prepared.internal_energy,
        prepared.kinetic_energy, interval, eos, controls, energy_change);
    if (!handoff.valid)
        throw std::runtime_error("Invalid burn energy at cell " + std::to_string(cell));
    commit_burn_energy(fluid, handoff);
    if (arch::state::validate(fluid, packed.data(), species, 1,
            config.numerics.sml_rho, config.numerics.min_eint,
            config.numerics.max_eint) != arch::state::Status::valid)
        throw std::runtime_error("Burn state violates configured bounds at cell "
                                 + std::to_string(cell));
    state.set_species_from_buffer(cell, packed.data());
    state.eng[cell] = fluid.eng;
    state.enuc_rate[cell] = handoff.enuc_rate;
    return handoff.limiter_candidate;
}

/** Execute a batch with one ODE workspace per worker and one result per patch. */
template<class Eos, class Burner>
void execute_host_burn_batch(std::span<const HostBurnPatch> patches,
    double interval, const Eos& eos, Burner& burn, const SimConfig& config,
    std::span<double> patch_limits)
{
    if (patch_limits.size() != patches.size())
        throw std::logic_error("Host burn batch lost a patch result");
    struct Range { std::size_t patch; int begin, end; };
    std::vector<Range> ranges;
    const int extent = host_burn_state_size(burn);
    // Thirty-two consecutive cells amortize scheduling while allowing hot
    // and cold regions within a single AMR patch to use different workers.
    constexpr int range_cells = 32;
    for (std::size_t index = 0; index < patches.size(); ++index) {
        auto& state = *patches[index].state;
        const auto& grid = *patches[index].grid;
        if (state.enuc_rate.size() != state.rho.size())
            throw std::runtime_error("Burn diagnostic storage is not initialized.");
        std::fill(state.enuc_rate.begin(), state.enuc_rate.end(), 0.0);
        if (!config.physics.burn.use_burn) continue;
        if (extent <= state.GetNumSpecies())
            throw std::logic_error("Active CPU burner has no valid packed ODE state extent.");
        const int cells = (grid.Ie() - grid.Is()) * (grid.Je() - grid.Js())
                        * (grid.Ke() - grid.Ks());
        for (int begin = 0; begin < cells; begin += range_cells)
            ranges.push_back({index, begin, std::min(begin + range_cells, cells)});
    }
    if (!config.physics.burn.use_burn || ranges.empty()) return;
    const auto controls = make_burn_config_view(config.physics.burn);
    const auto spec = arch::reduction::minimum_spec(INACTIVE_LIMITER_CANDIDATE);
    const auto initial = arch::reduction::begin_reduction(spec);
    std::vector<decltype(arch::reduction::begin_reduction(spec))> reductions(patches.size(), initial);
    arch::state::HostFailure failure;
#pragma omp parallel
    {
        std::vector<double> packed(static_cast<std::size_t>(extent), 0.0);
        HostBurnMemo memo(extent);
        auto local = reductions;
        // Preparation and the ODE may ask for the same strict inverse. Reuse
        // the EOS-owned exact keys only within this worker/half-step; no state
        // or table owner can cross the lexical lifetime, including failures.
        const auto process_ranges = [&] {
#pragma omp for schedule(dynamic, 1)
            for (std::size_t range_index = 0; range_index < ranges.size(); ++range_index) {
                const auto range = ranges[range_index];
                auto& state = *patches[range.patch].state;
                const auto& grid = *patches[range.patch].grid;
                const int nx = grid.Ie() - grid.Is(), ny = grid.Je() - grid.Js();
                try {
                    for (int linear = range.begin; linear < range.end; ++linear) {
                        const int i = grid.Is() + linear % nx;
                        const int j = grid.Js() + (linear / nx) % ny;
                        const int k = grid.Ks() + linear / (nx * ny);
                        const double candidate = advance_host_burn_cell(state,
                            grid.GetIndex(i, j, k), interval, eos, burn, config, controls, packed, memo);
                        amr::CellLogicalKey key{};
                        key.logical_i = i; key.logical_j = j; key.logical_k = k;
                        key.component = BURN_LIMITER_COMPONENT;
                        arch::reduction::combine_candidate(spec, local[range.patch],
                                                            {candidate, key, true});
                    }
                } catch (...) { failure.capture_current(); }
            }
        };
        if constexpr (requires { typename Eos::HostHydroScope; }) {
            typename Eos::HostHydroScope inverse_workspace(eos);
            process_ranges();
        } else process_ranges();
#pragma omp critical(burn_limiter_reduction)
        {
            for (std::size_t index = 0; index < patches.size(); ++index)
                arch::reduction::combine_state(spec, reductions[index], local[index]);
        }
    }
    failure.rethrow();
    for (std::size_t index = 0; index < patches.size(); ++index) {
        const auto result = arch::reduction::finalize_reduction(spec, reductions[index]);
        if (result.status != arch::reduction::ReductionStatus::Ok)
            throw std::runtime_error("Invalid burn limiter reduction");
        patch_limits[index] = combine_burn_minimum(patch_limits[index], result.value);
    }
}
} // namespace DriverBurn

/** Preserve the single-patch caller through the same batch/cell implementation. */
template<class Eos, class Burner>
void execute_burn_step(FluidState& state, double interval, const Eos& eos,
                       Burner& burn, const Grid& grid, const SimConfig& config,
                       double& limit)
{
    const DriverBurn::HostBurnPatch patch{&state, &grid};
    DriverBurn::execute_host_burn_batch({&patch, 1}, interval, eos, burn, config, {&limit, 1});
}
