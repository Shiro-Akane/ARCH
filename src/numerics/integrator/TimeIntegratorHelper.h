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

namespace TimeIntegration
{
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

                double dt_over_vol = dt / volume;
                dU[idx] = dU[idx] + (fluxes[idx] * area_l - fluxes[idx + stride] * area_r) * dt_over_vol;
                for (int s = 0; s < n_spec; ++s)
                {
                    int off = s * total_size;
                    d_spec[off + idx] += (spec_fluxes[off + idx] * area_l - spec_fluxes[off + idx + stride] * area_r) * dt_over_vol;
                }
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
                    PointCoords coords = grid.GetPhysicalCoords(i, j, k);
                    double r = coords.r;
                    if (grid.geometry == "cylindrical") r = coords.r_cy;

                    if (r < 1e-14)
                        continue;

                    state.get_species_to_buffer(idx, Xi.data());
                    FluidVector U = state.get(idx);
                    double p = eos.get_pressure(U, Xi.data());

                    double rho = std::max(U.rho, 1e-12);
                    double v_x = U.mom_u / rho;
                    double v_y = U.mom_v / rho;
                    double v_z = U.mom_w / rho;

                    if (grid.geometry == "cylindrical")
                    {
                        // mom_u = v_r. 2D mom_v = v_phi. 3D mom_w = v_phi
                        double v_phi = (grid.dim == 2) ? v_y : ((grid.dim == 3) ? v_z : 0.0);

                        dU[idx].mom_u += dt * (rho * v_phi * v_phi + p) / r;

                        if (grid.dim == 2) {
                            dU[idx].mom_v += dt * (-rho * v_x * v_y) / r;
                        } else if (grid.dim == 3) {
                            dU[idx].mom_w += dt * (-rho * v_x * v_z) / r;
                        }
                    }
                    else if (grid.geometry == "spherical")
                    {
                        // mom_u = v_r. 2D mom_v = v_phi.
                        // 3D mom_v = v_theta, mom_w = v_phi.
                        if (grid.dim == 1) {
                            dU[idx].mom_u += dt * 2.0 * p / r;
                        }
                        else if (grid.dim == 2) {
                            // 2D Spherical falls back to Polar (r, phi)
                            double v_phi = v_y;
                            dU[idx].mom_u += dt * (rho * v_phi * v_phi + p) / r;
                            dU[idx].mom_v += dt * (-rho * v_x * v_y) / r;
                        }
                        else if (grid.dim == 3) {
                            double v_theta = v_y;
                            double v_phi = v_z;
                            double theta = coords.theta;
                            double cot_theta = std::cos(theta) / std::max(std::sin(theta), 1e-14); // Avoid div zero at poles

                            dU[idx].mom_u += dt * (rho * (v_theta * v_theta + v_phi * v_phi) + 2.0 * p) / r;
                            dU[idx].mom_v += dt * (rho * v_phi * v_phi * cot_theta + p * cot_theta - rho * v_x * v_theta) / r;
                            dU[idx].mom_w += dt * (-rho * v_x * v_phi - rho * v_theta * v_phi * cot_theta) / r;
                        }
                    }
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
                FluidVector U_new = weight_n * U_old + weight_flux * (U_curr + dU[idx]);

                if (U_new.rho < sml_rho)
                {
                    U_new.rho = sml_rho;
                    U_new.mom_u = 0.0;
                    U_new.mom_v = 0.0;
                    U_new.mom_w = 0.0;
                    // Use the same configured positive specific-energy floor
                    // as the repair path below.
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

                    // A positive configured floor prevents EOS calls at zero
                    // or negative internal energy.
                    double current_eint = (U_new.eng - e_kin) / U_new.rho;

                    if (current_eint < min_eint || current_eint > max_eint)
                    {
                        current_eint = std::max(min_eint, std::min(current_eint, max_eint));
                        U_new.eng = U_new.rho * current_eint + e_kin;
                    }
                }

                u_dest.set(idx, U_new);

                double rho_new = std::max(U_new.rho, sml_rho);
                double sum_X = 0.0;
                for (int s = 0; s < n_spec; ++s)
                {
                    int off = s * total_size;
                    double rhoX_old = u_n.rho[idx] * u_n.X(s, idx);
                    double rhoX_curr = u_current.rho[idx] * u_current.X(s, idx);
                    double rhoX_comb = weight_n * rhoX_old + weight_flux * (rhoX_curr + d_spec[off + idx]);

                    double X_k = std::max(0.0, rhoX_comb / rho_new);
                    u_dest.X(s, idx) = X_k;
                    sum_X += X_k;
                }

                if (sum_X > 1e-13)
                {
                    double inv_sum = 1.0 / sum_X;
                    for (int s = 0; s < n_spec; ++s)
                        u_dest.X(s, idx) *= inv_sum;
                }
                else
                {
                    double inv_n = 1.0 / n_spec;
                    for (int s = 0; s < n_spec; ++s)
                        u_dest.X(s, idx) = inv_n;
                }
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
