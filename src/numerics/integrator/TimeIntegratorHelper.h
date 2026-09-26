/**
 * @file TimeIntegratorHelper.h
 * @brief Common helper functions for dimensional sweeping and state updates.
 *
 * Shared cell leaves compute delta U = dt*(A_left*F_left - A_right*F_right)/V
 * and combine stages as U_new = w_n*U_old + w_flux*(U_current + delta U).
 * A and V are physical face/cell measures from GridMetrics. Species increments
 * carry rho*X, then recover mass fractions after the density update and state
 * admissibility repairs. Host traversal calls these leaves and registers
 * coarse-fine fluxes; the integrator supplies weights and the driver schedules exchange.
 */

#pragma once

#include <algorithm>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif
#include "numerics/state/StateAdmissibility.h"
#include "numerics/flux/InvariantDomainFlux.h"

#include "amr/AMRControl.h"
#include "amr/flux/AMRFluxRegistering.h"
#include "data/FluidState.h"
#include "grid/Grid.h"
#include "grid/GridMetrics.h"
#include "physics/gravity/IGravityPolicy.h"
#include "numerics/integrator/GeometricSources.h"

namespace TimeIntegration
{
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

    inline void validate_reflux_state(const amr::AMRControl& control,
                                      const NumericsConfig& config,
                                      FluidState amr::Block::* slot = &amr::Block::fluid_state)
    {
        for (int id : control.tree->GetActiveBlocks()) {
            const auto& block = control.pool->GetBlock(id);
            try { validate_stage_state(block.*slot, block.grid, config); }
            catch (const std::exception& error) {
                throw std::runtime_error("AMR block=" + std::to_string(id) + ": " + error.what());
            }
        }
    }

    ARCH_INLINE void accumulate_cell_divergence(
        const FluidVector& lower_flux, const FluidVector& upper_flux,
        const double* lower_species_flux, const double* upper_species_flux,
        int n_spec, int species_stride,
        double area_l, double area_r, double volume, double dt,
        FluidVector& dU, double* d_spec)
    {
        double dt_over_vol = dt / volume;
        dU = dU + (lower_flux * area_l - upper_flux * area_r) * dt_over_vol;
        for (int s = 0; s < n_spec; ++s)
        {
            int off = s * species_stride;
            d_spec[off] += (lower_species_flux[off] * area_l - upper_species_flux[off] * area_r) * dt_over_vol;
        }
    }

    // Helper 1: Accumulate Flux Divergence
    inline void accumulate_divergence(
        std::vector<FluidVector> &dU, std::vector<double> &d_spec,
        const std::vector<FluidVector> &fluxes, const std::vector<double> &spec_fluxes,
        const Grid &grid, double dt, int dir, int n_spec)
    {
        int stride = (dir == 0) ? 1 : ((dir == 1) ? grid.stride_y : grid.stride_z);
        int total_size = grid.GetTotalSize();

        const int ks = grid.Ks(), ke = grid.Ke();
        const int js = grid.Js(), je = grid.Je();
        const int nk = ke - ks, nj = je - js;

        const auto accumulate_row = [&](int kj) {
            int k = ks + kj / nj;
            int j = js + kj % nj;
            for (int i = grid.Is(); i < grid.Ie(); ++i)
            {
                int idx = grid.GetIndex(i, j, k);

                const double volume = GridMetrics::CellVolume(grid, i, j, k);
                const double area_l = GridMetrics::FaceArea(grid, dir, i, j, k, false);
                const double area_r = GridMetrics::FaceArea(grid, dir, i, j, k, true);

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
        arch::state::HostFailure failure;
        if (grid.geometry == "cartesian")
            return;

        int n_spec = state.GetNumSpecies();
        const auto geometry = GridMetrics::make_geometry_view(grid);
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

    // Helper: Physical Source Terms (Gravity)
    inline void add_gravity_sources(
        std::vector<FluidVector> &dU,
        const FluidState &state,
        const Grid &grid,
        double dt,
        const Physical::Gravity::IGravityPolicy* gravity)
    {
        // Gravity policies own the patch loop; this call preserves the same
        // source-term contract for host and backend-specific implementations.
        if (gravity) {
            gravity->add_sources_on_patch(dU, state, grid, dt, nullptr);
        }
    }    // ---------------------------------------------------------
    ARCH_INLINE arch::state::Status update_stage_cell(
        const FluidVector& U_old, const FluidVector& U_curr,
        const FluidVector& delta,
        const double* Xi_old, const double* Xi_curr, const double* d_spec,
        int n_spec, int species_stride,
        double weight_n, double weight_flux,
        double sml_rho, double min_eint, double max_eint,
        FluidVector& U_new, double* Xi_new, arch::state::RepairView repairs = {},
        double cell_volume = 1.0, int cell = 0)
    {
        U_new = weight_n * U_old + weight_flux * (U_curr + delta);

        const double raw_density = U_new.rho;
        const auto repair = arch::state::apply_bounds(U_new, sml_rho, min_eint, max_eint);
        if (!arch::state::accepted(repair.status)) {
            U_new.eng = arch::state::invalid();
            return repair.status;
        }
        double sum = 0.0;
        bool composition_repaired = false;
        for (int s = 0; s < n_spec; ++s) {
            const int off = s * species_stride;
            const double density = weight_n * U_old.rho * Xi_old[off]
                + weight_flux * (U_curr.rho * Xi_curr[off] + d_spec[off]);
            double fraction = density / raw_density;
            if (!std::isfinite(fraction) || fraction < -64.0 * std::numeric_limits<double>::epsilon()) {
                U_new.eng = arch::state::invalid();
                return arch::state::Status::invalid_composition;
            }
            composition_repaired = composition_repaired || fraction < 0.0;
            fraction = std::max(0.0, fraction);
            Xi_new[off] = fraction;
            sum += fraction;
        }
        if (n_spec && (!std::isfinite(sum) || std::abs(sum - 1.0)
                > 512.0 * n_spec * std::numeric_limits<double>::epsilon())) {
            U_new.eng = arch::state::invalid();
            return arch::state::Status::invalid_composition;
        }
        if (composition_repaired && !arch::state::normalize_composition(Xi_new,n_spec,species_stride)) {
            U_new.eng = arch::state::invalid();
            return arch::state::Status::invalid_composition;
        }
        if (repair.status == arch::state::Status::repaired || composition_repaired) {
            repairs.event(cell_volume, cell);
            const auto delta = cell_volume * repair.delta;
            repairs.conserved(delta.rho,delta.mom_u,delta.mom_v,delta.mom_w,delta.eng);
            for (int s = 0; s < n_spec; ++s) {
                const int off = s * species_stride;
                const double before = weight_n * U_old.rho * Xi_old[off]
                    + weight_flux * (U_curr.rho * Xi_curr[off] + d_spec[off]);
                repairs.species_mass(s, cell_volume * (U_new.rho * Xi_new[off] - before));
            }
            return arch::state::Status::repaired;
        }
        return arch::state::Status::valid;
    }

    // Helper 2: Generalized Weighted RK Update
    inline void perform_stage_update(
        const FluidState &u_n, const FluidState &u_current, FluidState &u_dest,
        const std::vector<FluidVector> &dU, const std::vector<double> &d_spec,
        const Grid &grid, double weight_n, double weight_flux,
        double sml_rho, double min_eint, double max_eint)
    {
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
                    GridMetrics::CellVolume(grid, i, j, k), idx);
                if (!arch::state::accepted(status)) ++local_invalid;
                u_dest.set(idx, U_new);
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
    // FluxSchemePolicy supplies compute_fluxes for the selected reconstruction.
    template <typename FluxSchemePolicy, typename EosType>
    inline void evaluate_all_dimensions(
        amr::AMRControl* amr_ctrl, int block_id,
        const FluidState &state, const EosType &eos, const Grid &grid, double dt,
        std::vector<FluidVector> &dU, std::vector<double> &d_spec,
        std::vector<FluidVector> &flux_buffer, std::vector<double> &spec_flux_buffer,
        const Physical::Gravity::IGravityPolicy* gravity,
        double entropy_fix_coeff, double flux_weight = 1.0)
    {
        int n_spec = state.GetNumSpecies();
        std::fill(dU.begin(), dU.end(), FluidVector());
        std::fill(d_spec.begin(), d_spec.end(), 0.0);

        // Reset at every patch-stage: no result crosses RK, AMR, or restart
        // state versions. The cache only removes duplicate exact mean queries
        // for flux policies and EOS views that support the grouped path.
        static thread_local FluxAdmissibility::MeanThermoCache mean_cache;
        constexpr bool grouped_mean_eos = requires(
            const EosType& candidate, double& pressure, double& sound) {
            candidate.get_pressure_and_sound_speed(
                1.0, 1.0, static_cast<const double*>(nullptr), pressure, sound);
        };
        if constexpr (grouped_mean_eos)
            mean_cache.reset(grid.GetTotalSize());

        for (int dir = 0; dir < grid.dim; ++dir)
        {
            std::fill(flux_buffer.begin(), flux_buffer.end(), FluidVector());
            std::fill(spec_flux_buffer.begin(), spec_flux_buffer.end(), 0.0);
            if constexpr (grouped_mean_eos && requires {
                FluxSchemePolicy::compute_fluxes(
                    state, eos, grid, flux_buffer, spec_flux_buffer,
                    dir, entropy_fix_coeff, &mean_cache);
            }) {
                FluxSchemePolicy::compute_fluxes(
                    state, eos, grid, flux_buffer, spec_flux_buffer,
                    dir, entropy_fix_coeff, &mean_cache);
            } else {
                FluxSchemePolicy::compute_fluxes(
                    state, eos, grid, flux_buffer, spec_flux_buffer,
                    dir, entropy_fix_coeff);
            }

            accumulate_divergence(dU, d_spec, flux_buffer, spec_flux_buffer, grid, dt, dir, n_spec);

            if (gravity) gravity->add_flux_work_on_patch(dU, flux_buffer, state, grid, dt, dir);

            // Flux registration has one shared face-index convention for all AMR operators.
            if (amr_ctrl && block_id >= 0) {
                amr::RegisterCoarseFineFluxes(*amr_ctrl, block_id, grid, dir,
                                               flux_buffer, spec_flux_buffer, n_spec, flux_weight);
            }
        }

        add_geometric_sources(dU, state, eos, grid, dt);
        add_gravity_sources(dU, state, grid, dt, gravity);
    }
} // namespace TimeIntegration
