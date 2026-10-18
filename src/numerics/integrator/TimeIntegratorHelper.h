/**
 * @file TimeIntegratorHelper.h
 * @brief Common helper functions for dimensional sweeping and state updates.
 *
 * Shared cell leaves compute delta U = dt*(A_left*F_left - A_right*F_right)/V
 * and combine normalized stages as
 * U_new = U_old + w_flux*((U_current - U_old) + delta U), w_n+w_flux=1.
 * This is the same convex RK combination in increment form; it preserves a
 * finite stationary state without a repeated multiply/add normalization bias.
 * A and V are physical face/cell measures from GridMetrics. Species increments
 * carry rho*X, then recover mass fractions after the density update and state
 * admissibility repairs. Host traversal calls these leaves and registers
 * coarse-fine fluxes; the integrator supplies weights and the driver schedules exchange.
 */

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif

#include "amr/AMRControl.h"
#include "data/FluidState.h"
#include "driver/runtime/StateResidency.h"
#include "grid/Grid.h"
#include "numerics/curvilinear/GridMetrics.h"
#include "numerics/flux/InvariantDomainFlux.h"
#include "numerics/reconstruction/Reconstruction.h"
#include "numerics/curvilinear/GeometricSources.h"
#include "numerics/integrator/HydroCellUpdate.h"
#include "numerics/state/StateAdmissibility.h"
#include "physics/boundary/BoundaryDiagnostics.h"
#include "physics/boundary/BoundaryFlux.h"

namespace TimeIntegration
{
    /**
     * Resolve a Host Hydro output slot before physical boundary or ghost writes.
     * Current is the actual final storage after rotation and reflux; it is not
     * an alias for Next. Integrators reject any narrower storage-profile slot
     * before using this shared mapping. Unknown enum values always fail closed.
     */
    inline FluidState amr::Block::* hydro_boundary_state_member(
        arch::state::StateSlot slot)
    {
        switch (slot) {
        case arch::state::StateSlot::Current: return &amr::Block::fluid_state;
        case arch::state::StateSlot::Next: return &amr::Block::state_next;
        case arch::state::StateSlot::Scratch: return &amr::Block::state_scratch;
        }
        throw std::logic_error("Host Hydro boundary selected unknown state slot");
    }

    /** Fill block ghosts and propagate callback failures after workers join. */
    template<class BCPolicy>
    inline void apply_domain_boundary(amr::AMRControl& control, BCPolicy& boundary,
                                     FluidState amr::Block::* member)
    {
        const auto& active = control.tree->GetActiveBlocks();
        arch::state::HostFailure failure;
#pragma omp parallel for schedule(dynamic, 1)
        for (std::size_t i = 0; i < active.size(); ++i) {
            try {
                auto& block = control.pool->GetBlock(active[i]);
                boundary.apply(block.*member, block.grid);
            } catch (...) { failure.capture_current(); }
        }
        failure.rethrow();
    }

    /** Synchronize one actual Host domain without owning GhostValid publication. */
    template<class BCPolicy>
    inline void synchronize_domain_boundary(amr::AMRControl& control,BCPolicy& boundary,
        FluidState amr::Block::* member,std::span<const amr::BlockHandle> handles)
    {
        apply_domain_boundary(control,boundary,member);
        control.ghost_exchange.ExecuteExchange(control.pool,control.tree,
            control.tree->GetRootGridDim(),member,handles);
    }

    /** Describe a rejected composition without changing its values or tolerance.
     * Workflow: this cold error path reports finite/negative counts, min/max and
     * sum at full binary64 decimal precision. The same shared simplex rule
     * |sum(X)-1| <= 512*N*epsilon remains the acceptance authority.
     */
    inline std::string rejected_stage_composition(const FluidState& state, int cell)
    {
        const int count = state.GetNumSpecies();
        double sum = 0.0;
        double minimum = std::numeric_limits<double>::infinity();
        double maximum = -std::numeric_limits<double>::infinity();
        int nonfinite = 0, negative = 0;
        for (int species = 0; species < count; ++species) {
            const double value = state.X(species, cell);
            sum += value;
            if (!std::isfinite(value)) ++nonfinite;
            else {
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
                if (value < 0.0) ++negative;
            }
        }
        std::ostringstream message;
        message.precision(std::numeric_limits<double>::max_digits10);
        message << " composition_count=" << count << " nonfinite=" << nonfinite
                << " negative=" << negative << " Xi_min=" << minimum
                << " Xi_max=" << maximum << " Xi_sum=" << sum
                << " simplex_error=" << std::abs(sum - 1.0)
                << " simplex_limit=" << 512.0 * count
                    * std::numeric_limits<double>::epsilon();
        return message.str();
    }

    /** Repair trace errors with the existing conservative-state acceptance. */
    inline void accept_stage_state(FluidState& state, const Grid& grid,
                                   const NumericsConfig& config)
    {
        const auto geometry = GridMetrics::make_geometry_view(grid);
        for (int k = grid.Ks(); k < grid.Ke(); ++k)
            for (int j = grid.Js(); j < grid.Je(); ++j)
                for (int i = grid.Is(); i < grid.Ie(); ++i) {
                    const int cell = grid.GetIndex(i, j, k);
                    double* fractions = state.GetNumSpecies()
                        ? state.mass_fractions.data() + cell : nullptr;
                    const auto status = arch::state::accept_conservative_state(
                        state.get(cell), fractions, state.GetNumSpecies(),
                        grid.GetTotalSize(), config.sml_rho, config.min_eint, config.max_eint,
                        GridMetrics::CellVolume(geometry, i, j, k),
                        state.stage_repairs.view(), cell);
                    if (!arch::state::accepted(status))
                        throw std::runtime_error("Invalid accepted state: cell=" + std::to_string(cell)
                            + " status=" + std::to_string(static_cast<int>(status))
                            + (status == arch::state::Status::invalid_composition
                                ? rejected_stage_composition(state, cell) : std::string{}));
                }
    }

    /** Accept after all reflux faces; optionally retain the RKL-stage receipt. */
    inline void accept_reflux_state(amr::AMRControl& control,
                                    const NumericsConfig& config,
                                    FluidState amr::Block::* slot = &amr::Block::fluid_state,
                                    bool reset_receipt = true)
    {
        for (int id : control.tree->GetActiveBlocks()) {
            auto& block = control.pool->GetBlock(id);
            auto& state = block.*slot;
            if (reset_receipt) state.stage_repairs.reset(state.GetNumSpecies());
            try { accept_stage_state(state, block.grid, config); }
            catch (const std::exception& error) {
                throw std::runtime_error("AMR block=" + std::to_string(id) + ": " + error.what());
            }
        }
    }

    inline void validate_stage_state(const FluidState& state, const Grid& grid,
                                     const NumericsConfig& config)
    {
        for (int k = grid.Ks(); k < grid.Ke(); ++k)
            for (int j = grid.Js(); j < grid.Je(); ++j)
                for (int i = grid.Is(); i < grid.Ie(); ++i) {
                    const int cell = grid.GetIndex(i, j, k);
                    const auto status = arch::state::validate(state.get(cell),
                        state.GetNumSpecies() ? state.mass_fractions.data() + cell : nullptr,
                        state.GetNumSpecies(), grid.GetTotalSize(),
                        config.sml_rho, config.min_eint, config.max_eint);
                    if (status != arch::state::Status::valid)
                        throw std::runtime_error("Invalid accepted state: cell=" + std::to_string(cell)
                            + " status=" + std::to_string(static_cast<int>(status)));
                }
    }

    // Helper 1: Accumulate Flux Divergence
    inline void accumulate_divergence(
        std::vector<FluidVector> &dU, std::vector<double> &d_spec,
        const std::vector<FluidVector> &fluxes, const std::vector<double> &spec_fluxes,
        const Grid &grid, double dt, int dir, int n_spec)
    {
        const auto geometry=GridMetrics::make_geometry_view(grid);
        int stride = (dir == 0) ? 1 : ((dir == 1) ? grid.stride_y : grid.stride_z);
        int total_size = grid.GetTotalSize();

        const int ks = grid.Ks(), ke = grid.Ke();
        const int js = grid.Js(), je = grid.Je();
        const int nk = ke - ks, nj = je - js;

        arch::state::HostFailure failure;
        const auto accumulate_row = [&](int kj) {
            try {
            int k = ks + kj / nj;
            int j = js + kj % nj;
            for (int i = grid.Is(); i < grid.Ie(); ++i)
            {
                int idx = grid.GetIndex(i, j, k);

                const double volume = GridMetrics::CellVolume(geometry,i,j,k);
                const double area_l = GridMetrics::FaceArea(geometry,dir,i,j,k,false);
                const double area_r = GridMetrics::FaceArea(geometry,dir,i,j,k,true);

                const double* lower_species_flux = n_spec > 0
                    ? spec_fluxes.data() + idx : nullptr;
                const double* upper_species_flux = n_spec > 0
                    ? spec_fluxes.data() + idx + stride : nullptr;
                double* species_delta = n_spec > 0
                    ? d_spec.data() + idx : nullptr;
                accumulate_cell_divergence(
                    fluxes[idx], fluxes[idx + stride],
                    lower_species_flux, upper_species_flux,
                    n_spec, total_size, area_l, area_r, volume, dt,
                    dU[idx], species_delta);
            }
            } catch (...) { failure.capture_current(); }
        };
        bool parallel_rows = nk * nj > 1;
#ifdef _OPENMP
        parallel_rows = parallel_rows && !omp_in_parallel();
#endif
        if (parallel_rows) {
#pragma omp parallel for schedule(static)
            for (int kj = 0; kj < nk * nj; ++kj)
                accumulate_row(kj);
        } else {
            for (int kj = 0; kj < nk * nj; ++kj)
                accumulate_row(kj);
        }
        failure.rethrow();
    }

    // Helper: Geometric Source Terms (cylindrical / spherical)
    // Radial pressure source: S=+p/r in cylindrical coordinates and +2p/r
    // in spherical coordinates.
    template <typename EosType>
    inline void add_geometric_sources(
        std::vector<FluidVector> &dU,
        const FluidState &state,
        const EosType &eos,
        const Grid &grid,
        double dt)
    {
        const auto geometry=GridMetrics::make_geometry_view(grid);
        arch::state::HostFailure failure;
        if (grid.geometry == "cartesian")
            return;

        int n_spec = state.GetNumSpecies();
        const int ks = grid.Ks(), ke = grid.Ke();
        const int js = grid.Js(), je = grid.Je();
        const int nk = ke - ks, nj = je - js;

#pragma omp parallel
        {
            std::vector<double> Xi(n_spec);
#pragma omp for schedule(static)
            for (int kj = 0; kj < nk * nj; ++kj)
            {
                try {
                    int k = ks + kj / nj;
                    int j = js + kj % nj;
                    for (int i = grid.Is(); i < grid.Ie(); ++i)
                    {
                        int idx = grid.GetIndex(i, j, k);
                        state.get_species_to_buffer(idx, Xi.data());
                        add_geometric_source_cell(
                            state.get(idx), Xi.data(), eos, geometry, i, j, dt, dU[idx]);
                    }

                } catch (...) { failure.capture_current(); }
            }
        }

        failure.rethrow();
    }

    // Helper 2: Generalized Weighted RK Update
    inline void perform_stage_update(
        const FluidState &u_n, const FluidState &u_current, FluidState &u_dest,
        const std::vector<FluidVector> &dU, const std::vector<double> &d_spec,
        const Grid &grid, double weight_n, double weight_flux,
        double sml_rho, double min_eint, double max_eint)
    {
        const auto geometry = GridMetrics::make_geometry_view(grid);
        int n_spec = u_n.GetNumSpecies();
        int total_size = grid.GetTotalSize();

        const int ks = grid.Ks();
        const int ke = grid.Ke();
        const int js = grid.Js();
        const int je = grid.Je();
        const int nk = ke - ks;
        const int nj = je - js;

        u_dest.stage_repairs.reset(n_spec);
        int invalid_count = 0;
        const auto update_row = [&](int kj, arch::state::RepairBudget& local,
                                    int& local_invalid) {
            int k = ks + kj / nj;
            int j = js + kj % nj;
            for (int i = grid.Is(); i < grid.Ie(); ++i)
            {
                int idx = grid.GetIndex(i, j, k);

                FluidVector U_old = u_n.get(idx);
                FluidVector U_curr = u_current.get(idx);
                FluidVector U_new;
                const double* Xi_old = n_spec > 0
                    ? u_n.mass_fractions.data() + idx : nullptr;
                const double* Xi_curr = n_spec > 0
                    ? u_current.mass_fractions.data() + idx : nullptr;
                const double* species_delta = n_spec > 0
                    ? d_spec.data() + idx : nullptr;
                double* Xi_new = n_spec > 0
                    ? u_dest.mass_fractions.data() + idx : nullptr;
                const auto status = update_stage_cell(
                    U_old, U_curr, dU[idx], Xi_old, Xi_curr, species_delta,
                    n_spec, total_size, weight_n, weight_flux,
                    sml_rho, min_eint, max_eint, U_new, Xi_new, local.view(),
                    GridMetrics::CellVolume(geometry, i, j, k), idx);
                if (!arch::state::accepted(status)) ++local_invalid;
                u_dest.set(idx, U_new);
                // Hydro updates U/rhoX, not the last burn half's diagnostic.
                // Carry its actual input bits through slot rotation; a stale
                // output slot is never the source and no RK weight is applied.
                u_dest.enuc_rate[idx] = u_current.enuc_rate[idx];
            }
        };
        bool parallel_rows = nk * nj > 1;
#ifdef _OPENMP
        parallel_rows = parallel_rows && !omp_in_parallel();
#endif
        if (parallel_rows) {
#pragma omp parallel reduction(+:invalid_count)
            {
                arch::state::RepairBudget local(n_spec);
#pragma omp for schedule(static)
                for (int kj = 0; kj < nk * nj; ++kj)
                    update_row(kj, local, invalid_count);
#pragma omp critical(arch_state_repairs)
                u_dest.stage_repairs.combine(local);
            }
        } else {
            arch::state::RepairBudget local(n_spec);
            for (int kj = 0; kj < nk * nj; ++kj)
                update_row(kj, local, invalid_count);
            u_dest.stage_repairs.combine(local);
        }
        if (invalid_count) throw std::runtime_error("Hydro candidate rejected: invalid density, composition or unresolved/internal energy");
    }

    // Helper 3: Evaluate fluxes in every active dimension.
    /**
     * Observe the final oriented hydro flux on every physical outer face.
     * Only faces that own a stage plane are touched; internal AMR joins and
     * periodic pairs keep null planes, so this writes no simulation state.
     */
    inline void capture_hydro_surface_flux(
        const FluidState& state, const Grid& grid, int dir,
        const std::vector<FluidVector>& flux_buffer,
        const std::vector<double>& spec_flux_buffer)
    {
        const auto storage = state.boundary_flux_capture;
        if (!storage) return;
        const arch::boundary::BoundaryFluxCaptureView view = storage->view();
        const int stride = (dir == 0) ? 1 : ((dir == 1) ? grid.stride_y : grid.stride_z);
        const int species = state.GetNumSpecies();
        const int total_size = grid.GetTotalSize();
        const int lower[3]{grid.Is(), grid.Js(), grid.Ks()};
        const int upper[3]{grid.Ie(), grid.Je(), grid.Ke()};
        const int tangent_a = (dir + 1) % 3, tangent_b = (dir + 2) % 3;
        for (int side = 0; side < 2; ++side) {
            if (!view.stage[2 * dir + side]) continue;
            int face[3]{lower[0], lower[1], lower[2]};
            int cell[3]{lower[0], lower[1], lower[2]};
            face[dir] = side == 0 ? lower[dir] : upper[dir];
            cell[dir] = side == 0 ? lower[dir] : upper[dir] - 1;
            for (int ib = lower[tangent_b]; ib < upper[tangent_b]; ++ib) {
                cell[tangent_b] = face[tangent_b] = ib;
                for (int ia = lower[tangent_a]; ia < upper[tangent_a]; ++ia) {
                    cell[tangent_a] = face[tangent_a] = ia;
                    const int index = grid.GetIndex(cell[0], cell[1], cell[2])
                        + (side == 1 ? stride : 0);
                    arch::boundary::CaptureBoundaryFlux(view, dir,
                        face[0], face[1], face[2],
                        lower[0], upper[0], lower[1], upper[1], lower[2], upper[2],
                        flux_buffer[index],
                        species > 0 ? spec_flux_buffer.data() + index : nullptr,
                        species, total_size, 0.0);
                }
            }
        }
    }

    /** Apply the selected Hydro patch math with a caller-owned source adapter.
     * Workflow: preflight geometry before clearing outputs/cache; compute
     * each selected flux; accumulate original divergence and same-stage face
     * work; register coarse-fine fluxes; apply geometry and body sources.
     * The registry alone applies original
     * area/sign/RK weights and reflux dt. Source type deduction keeps concrete
     * field ownership and potential history in the Hydro composition owner;
     * diffusion can reuse divergence without importing that source owner.
     */
    // FluxSchemePolicy supplies compute_fluxes for the selected reconstruction.
    template <typename FluxSchemePolicy, typename EosType, typename SourceType>
    inline void evaluate_all_dimensions(
        amr::AMRControl* amr_ctrl, int block_id,
        const FluidState &state, const EosType &eos, const Grid &grid, double dt,
        std::vector<FluidVector> &dU, std::vector<double> &d_spec,
        std::vector<FluidVector> &flux_buffer, std::vector<double> &spec_flux_buffer,
        SourceType& source,
        double entropy_fix_coeff, double flux_weight = 1.0, bool roe_wave_speed = true,
        const arch::boundary::HydroBoundaryView& hydro_boundary = {})
    {
        // Validate the chart and reject consumers not yet migrated before any
        // output/cache mutation. Runtime Grid still uses its existing chart.
        (void)GridMetrics::make_geometry_view(grid);
        int n_spec = state.GetNumSpecies();
        std::fill(dU.begin(), dU.end(), FluidVector());
        std::fill(d_spec.begin(), d_spec.end(), 0.0);

        // Reset at every patch-stage: no result crosses RK, AMR, or restart
        // state versions. The cache only removes duplicate exact mean queries
        // for every flux policy; grouped EOS queries remain optional at the EOS leaf.
        static thread_local FluxAdmissibility::MeanThermoCache mean_cache;
        mean_cache.reset(grid.GetTotalSize());
        mean_cache.roe_wave_speed = roe_wave_speed;
        mean_cache.hydro_boundary = hydro_boundary;

        for (int dir = 0; dir < grid.dim; ++dir)
        {
            std::fill(flux_buffer.begin(), flux_buffer.end(), FluidVector());
            std::fill(spec_flux_buffer.begin(), spec_flux_buffer.end(), 0.0);
            FluxSchemePolicy::compute_fluxes(
                state, eos, grid, flux_buffer, spec_flux_buffer,
                dir, entropy_fix_coeff, &mean_cache);

            capture_hydro_surface_flux(state, grid, dir, flux_buffer, spec_flux_buffer);

            accumulate_divergence(dU,d_spec,flux_buffer,spec_flux_buffer,grid,dt,dir,n_spec);

            // Same actual stage: source work consumes this axis's immutable
            // Riemann mass flux immediately after its original divergence.
            source.add_flux_work(dU,flux_buffer,state,grid,dt,dir);

            // Flux registration has one shared face-index convention for all AMR operators.
            if (amr_ctrl && block_id >= 0) {
                source.register_coarse_fine_fluxes(*amr_ctrl,block_id,grid,dir,
                    flux_buffer,spec_flux_buffer,n_spec,flux_weight);
            }
        }

        add_geometric_sources(dU,state,eos,grid,dt);
        source.add_sources(dU,state,grid,dt);
    }
} // namespace TimeIntegration
