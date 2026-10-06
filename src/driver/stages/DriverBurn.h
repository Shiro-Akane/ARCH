/**
 * @file DriverBurn.h
 * @brief Host traversal for the shared split burn operation.
 *
 * Workflow:
 * 1. Bind active patches after their boundary states have been prepared.
 * 2. Distribute bounded cell ranges across one host worker team. Every cell
 *    uses DriverBurnPolicy preparation, the original ODE, and checked commit.
 * 3. Reduce each patch's timestep advice with the shared reduction contract.
 * 4. A patch may request the native full-ring RZ geometry view. The shared
 *    density/closure leaves then supply the ordinary thermodynamic mean while
 *    the native conserved moments stay authoritative and unwritten.
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
#include "grid/GridMetrics.h"
#include "numerics/state/RzNativeClosure.h"

namespace DriverBurn {

struct HostBurnPatch {
    FluidState* state;
    const Grid* grid;
    GridMetrics::GeometrySemantics geometry_semantics =
        GridMetrics::GeometrySemantics::Existing;
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

/** Integrate and publish one cell only after all physical checks have passed.
 * The native cell state is authoritative for rho, the momenta, eng and X and is
 * never replaced. When a native RZ geometry view is supplied, the shared
 * density reconstruction and RzThermodynamics closure derive ONLY the ordinary
 * mean the strict DriverBurnPolicy preparation/handoff consume; the chosen
 * energy and the original composition/enuc diagnostics are then published back
 * onto the untouched native moments. A null view is the legacy path and keeps
 * the exact original operations and call counts.
 */
template<class Eos, class Burner>
double advance_host_burn_cell(FluidState& state, int cell, double interval,
    const Eos& eos, Burner& burn, const SimConfig& config,
    const BurnConfigView& controls, std::vector<double>& packed, HostBurnMemo& memo,
    const GridMetrics::GeometryView* native_geometry = nullptr,
    int radial_index = 0)
{
    FluidVector fluid = state.get(cell);
    // Legacy input is the ordinary fluid; the native RZ path replaces this
    // mean with the closure of the actual center density of this cell.
    FluidVector thermo = fluid;
    if (native_geometry != nullptr) {
        if (native_geometry->semantics
                != GridMetrics::GeometrySemantics::AxisymmetricRz)
            throw std::runtime_error(
                "Native burn geometry requires AxisymmetricRz semantics at cell "
                + std::to_string(cell));
        // Read ONLY the immutable density storage. state.get() of a neighbor
        // would race parallel workers rewriting their own eng/species, so the
        // reader returns rho alone and leaves every other moment zero.
        const auto density_reader = [&state](int native_index) {
            FluidVector sample{};
            sample.rho = state.rho[native_index];
            return sample;
        };
        const auto density = RzDensity::density_cell(
            density_reader, cell, *native_geometry, radial_index);
        const auto closure = RzThermodynamics::from_density(fluid, density,
            {config.numerics.sml_rho, config.numerics.min_eint,
             config.numerics.max_eint});
        if (!closure.valid())
            throw std::runtime_error(
                "Invalid native RZ thermodynamic closure at cell "
                + std::to_string(cell));
        thermo = closure.effective_mean;
    }
    if (check_burn_density(thermo, controls) == BurnCellDisposition::BelowDensity)
        return INACTIVE_LIMITER_CANDIDATE;
    std::fill(packed.begin(), packed.end(), 0.0);
    state.get_species_to_buffer(cell, packed.data());
    const int species = state.GetNumSpecies();
    const auto prepared = prepare_burn_cell(
        thermo, packed.data(), species, eos, controls);
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
    // The ODE still integrates the actual cell density with the unmodified
    // packed X/T and interval; no closure quantity reaches the ODE or the memo.
    if (!memo.integrate(packed, fluid.rho, interval, eos, burn,
                        config.physics.burn, recommended, energy_change))
        throw std::runtime_error("Burn solver failed at cell " + std::to_string(cell));
    // The ODE owns integral(delta e_nuc); the existing first-law handoff
    // combines it with the initial thermal energy and unchanged kinetic part.
    const auto handoff = compute_burn_energy_handoff(
        thermo, packed.data(), species, prepared.internal_energy,
        prepared.kinetic_energy, interval, eos, controls, energy_change);
    if (!handoff.valid)
        throw std::runtime_error("Invalid burn energy at cell " + std::to_string(cell));
    // Validate the ordinary thermodynamic candidate with the same configured
    // bounds and the same solved composition before anything is published.
    FluidVector candidate = thermo;
    commit_burn_energy(candidate, handoff);
    if (arch::state::validate(candidate, packed.data(), species, 1,
            config.numerics.sml_rho, config.numerics.min_eint,
            config.numerics.max_eint) != arch::state::Status::valid)
        throw std::runtime_error("Burn state violates configured bounds at cell "
                                 + std::to_string(cell));
    // Publish only the accepted thermodynamic energy, the original enuc rate
    // and the original composition. Native momenta are never overwritten.
    state.set_species_from_buffer(cell, packed.data());
    state.eng[cell] = candidate.eng;
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
                const auto semantics = patches[range.patch].geometry_semantics;
                const int nx = grid.Ie() - grid.Is(), ny = grid.Je() - grid.Js();
                try {
                    // One real view per patch, built by the shared factory from
                    // the actual grid and requested semantics; an unknown
                    // enumeration is rejected there instead of inventing a
                    // coordinate. Legacy semantics never expose a native view,
                    // so no density or closure read is introduced for them.
                    const auto native_view =
                        GridMetrics::make_geometry_view(grid, semantics);
                    const GridMetrics::GeometryView* native_geometry =
                        semantics == GridMetrics::GeometrySemantics::AxisymmetricRz
                        ? &native_view : nullptr;
                    for (int linear = range.begin; linear < range.end; ++linear) {
                        const int i = grid.Is() + linear % nx;
                        const int j = grid.Js() + (linear / nx) % ny;
                        const int k = grid.Ks() + linear / (nx * ny);
                        const double candidate = advance_host_burn_cell(state,
                            grid.GetIndex(i, j, k), interval, eos, burn, config, controls,
                            packed, memo, native_geometry, i);
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

/** Preserve the single-patch caller through the same batch/cell implementation.
 * The optional semantics argument keeps every existing call unchanged; the
 * stage owner that already guarantees same-level RZ state and ghost layout
 * passes the real semantics so the batch can expose the native geometry view.
 */
template<class Eos, class Burner>
void execute_burn_step(FluidState& state, double interval, const Eos& eos,
                       Burner& burn, const Grid& grid, const SimConfig& config,
                       double& limit,
                       GridMetrics::GeometrySemantics semantics =
                           GridMetrics::GeometrySemantics::Existing)
{
    const DriverBurn::HostBurnPatch patch{&state, &grid, semantics};
    DriverBurn::execute_host_burn_batch({&patch, 1}, interval, eos, burn, config, {&limit, 1});
}
