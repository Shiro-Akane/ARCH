/**
 * @file TimeIntegratorHelper.h
 * @brief Common helper functions for dimensional sweeping and state updates.
 * Centralizes the core loops for all explicit time integrators.
 */

/**
 * Workflow:
 * 1. Evaluate block-local flux divergence and physical source terms.
 * 2. Combine stages with the documented Euler, RK2, or RK3 coefficients.
 * 3. Leave AMR communication and reflux ownership with the common driver services.
 */

#pragma once

#include <algorithm>
#include <vector>

#include "../../amr/AMRControl.h"
#include "../../amr/AMRFluxRegistering.h"
#include "../../data/FluidState.h"
#include "../../grid/Grid.h"
#include "../../grid/GridMetrics.h"
#include "../../physics/gravity/IGravityPolicy.h"
#include "GeometricSources.h"

namespace TimeIntegration
{
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

#pragma omp parallel for schedule(static)
        for (int kj = 0; kj < nk * nj; ++kj)
        {
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
                int k = ks + kj / nj;
                int j = js + kj % nj;
                for (int i = grid.Is(); i < grid.Ie(); ++i)
                {
                    int idx = grid.GetIndex(i, j, k);
                    state.get_species_to_buffer(idx, Xi.data());
                    add_geometric_source_cell(
                        state.get(idx), Xi.data(), eos, geometry, i, j, dt, dU[idx]);
                }
            }
        }
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
    ARCH_INLINE void update_stage_cell(
        const FluidVector& U_old, const FluidVector& U_curr,
        const FluidVector& delta,
        const double* Xi_old, const double* Xi_curr, const double* d_spec,
        int n_spec, int species_stride,
        double weight_n, double weight_flux,
        double sml_rho, double min_eint, double max_eint,
        FluidVector& U_new, double* Xi_new)
    {
        U_new = weight_n * U_old + weight_flux * (U_curr + delta);

        if (U_new.rho < sml_rho)
        {
            U_new.rho = sml_rho;
            U_new.mom_u = 0.0;
            U_new.mom_v = 0.0;
            U_new.mom_w = 0.0;
            // Keep the device and host repair paths on the configured floor.
            U_new.eng = sml_rho * min_eint;
        }
        else
        {
            // Kinetic-energy density removed before applying the internal-energy bounds.
            double e_kin = 0.5 * (U_new.mom_u * U_new.mom_u + U_new.mom_v * U_new.mom_v + U_new.mom_w * U_new.mom_w) / U_new.rho;

            // Limit repaired states to 1e10 cm/s so vanishing density
            // cannot inject an unbounded kinetic-energy density.
            double max_vel = 1e10;
            double v_sq = 2.0 * e_kin / U_new.rho;
            if (v_sq > max_vel * max_vel) {
                double scale = max_vel / std::sqrt(v_sq);
                U_new.mom_u *= scale;
                U_new.mom_v *= scale;
                U_new.mom_w *= scale;
                e_kin = 0.5 * (U_new.mom_u * U_new.mom_u + U_new.mom_v * U_new.mom_v + U_new.mom_w * U_new.mom_w) / U_new.rho;
            }

            // Configured bounds prevent EOS calls at invalid internal energy.
            double current_eint = (U_new.eng - e_kin) / U_new.rho;

            if (current_eint < min_eint || current_eint > max_eint)
            {
                current_eint = std::max(min_eint, std::min(current_eint, max_eint));
                U_new.eng = U_new.rho * current_eint + e_kin;
            }
        }

        double rho_new = std::max(U_new.rho, sml_rho);
        double sum_X = 0.0;
        for (int s = 0; s < n_spec; ++s)
        {
            int off = s * species_stride;
            double rhoX_old = U_old.rho * Xi_old[off];
            double rhoX_curr = U_curr.rho * Xi_curr[off];
            double rhoX_comb = weight_n * rhoX_old + weight_flux * (rhoX_curr + d_spec[off]);

            double X_k = std::max(0.0, rhoX_comb / rho_new);
            Xi_new[off] = X_k;
            sum_X += X_k;
        }

        if (n_spec == 0)
        {
            return;
        }
        if (sum_X > 1e-13)
        {
            double inv_sum = 1.0 / sum_X;
            for (int s = 0; s < n_spec; ++s)
                Xi_new[s * species_stride] *= inv_sum;
        }
        else
        {
            double inv_n = 1.0 / n_spec;
            for (int s = 0; s < n_spec; ++s)
                Xi_new[s * species_stride] = inv_n;
        }
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

#pragma omp parallel for schedule(static)
        for (int kj = 0; kj < nk * nj; ++kj)
        {
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
                update_stage_cell(
                    U_old, U_curr, dU[idx], Xi_old, Xi_curr, species_delta,
                    n_spec, total_size, weight_n, weight_flux,
                    sml_rho, min_eint, max_eint, U_new, Xi_new);
                u_dest.set(idx, U_new);
            }
        }
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

        for (int dir = 0; dir < grid.dim; ++dir)
        {
            std::fill(flux_buffer.begin(), flux_buffer.end(), FluidVector());
            std::fill(spec_flux_buffer.begin(), spec_flux_buffer.end(), 0.0);
            FluxSchemePolicy::compute_fluxes(state, eos, grid, flux_buffer, spec_flux_buffer, dir, entropy_fix_coeff);

            accumulate_divergence(dU, d_spec, flux_buffer, spec_flux_buffer, grid, dt, dir, n_spec);

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
