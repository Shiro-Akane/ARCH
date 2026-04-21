/**
 * @file TimeIntegratorHelper.h
 * @brief Common helper functions for dimensional sweeping and state updates.
 * Centralizes the core loops for all explicit time integrators.
 */

#pragma once

#include <vector>
#include <algorithm>
#include "../../data/FluidState.h"
#include "../../grid/Grid.h"

namespace TimeIntegration
{
    // ---------------------------------------------------------
    // Helper 1: Accumulate Flux Divergence
    // ---------------------------------------------------------
    inline void accumulate_divergence(
        std::vector<FluidVector> &dU, std::vector<double> &d_spec,
        const std::vector<FluidVector> &fluxes, const std::vector<double> &spec_fluxes,
        const Grid &grid, double dt, int dir, int n_spec)
    {
        int stride = (dir == 0) ? 1 : ((dir == 1) ? grid.stride_y : grid.stride_z);
        double dx = (dir == 0) ? grid.dx : ((dir == 1) ? grid.dy : grid.dz);
        int total_size = grid.GetTotalSize();

        // 非笛卡尔坐标仅对径向方向（dir==0）做面积/体积缩放
        const bool is_radial = (dir == 0) && (grid.geometry != "cartesian");
        const bool is_spherical = (grid.geometry == "spherical");

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

                if (is_radial)
                {
                    double r_l = grid.GetFacePosL(i);
                    double r_r = grid.GetFacePosR(i);
                    double r_c = grid.GetCellCenterX(i);

                    double area_l, area_r, vol;
                    if (is_spherical)
                    {
                        area_l = r_l * r_l;
                        area_r = r_r * r_r;
                        vol    = r_c * r_c * dx;
                    }
                    else // cylindrical
                    {
                        area_l = r_l;
                        area_r = r_r;
                        vol    = r_c * dx;
                    }
                    double dt_over_vol = dt / vol;

                    dU[idx] = dU[idx] + (fluxes[idx] * area_l - fluxes[idx + stride] * area_r) * dt_over_vol;
                    for (int s = 0; s < n_spec; ++s)
                    {
                        int off = s * total_size;
                        d_spec[off + idx] += (spec_fluxes[off + idx] * area_l - spec_fluxes[off + idx + stride] * area_r) * dt_over_vol;
                    }
                }
                else
                {
                    double dt_over_dx = dt / dx;
                    dU[idx] = dU[idx] + (fluxes[idx] - fluxes[idx + stride]) * dt_over_dx;
                    for (int s = 0; s < n_spec; ++s)
                    {
                        int off = s * total_size;
                        d_spec[off + idx] += (spec_fluxes[off + idx] - spec_fluxes[off + idx + stride]) * dt_over_dx;
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------
    // Helper: Geometric Source Terms (cylindrical / spherical)
    // ---------------------------------------------------------
    // 柱坐标：径向动量方程源项 S = +p/r
    // 球坐标：径向动量方程源项 S = +2p/r
    template <typename EosType>
    inline void add_geometric_sources(
        std::vector<FluidVector> &dU,
        const FluidState &state,
        const EosType &eos,
        const Grid &grid,
        double dt)
    {
        if (grid.geometry == "cartesian") return;

        const double geom_coeff = (grid.geometry == "spherical") ? 2.0 : 1.0;
        int n_spec = state.GetNumSpecies();

        const int ks = grid.Ks(), ke = grid.Ke();
        const int js = grid.Js(), je = grid.Je();
        const int nk = ke - ks, nj = je - js;

        #pragma omp parallel
        {
            std::vector<double> Yi(n_spec);
            #pragma omp for schedule(static)
            for (int kj = 0; kj < nk * nj; ++kj)
            {
                int k = ks + kj / nj;
                int j = js + kj % nj;
                for (int i = grid.Is(); i < grid.Ie(); ++i)
                {
                    int idx = grid.GetIndex(i, j, k);
                    double r = grid.GetCellCenterX(i);
                    if (r < 1e-14) continue;

                    state.get_species_to_buffer(idx, Yi.data());
                    double p = eos.get_pressure(state.get(idx), Yi.data());
                    dU[idx].mom_x += dt * geom_coeff * p / r;
                }
            }
        }
    }

    // ---------------------------------------------------------
    // Helper 2: Generalized Weighted RK Update
    // ---------------------------------------------------------
    inline void perform_stage_update(
        const FluidState &u_n, const FluidState &u_current, FluidState &u_dest,
        const std::vector<FluidVector> &dU, const std::vector<double> &d_spec,
        const Grid &grid, double weight_n, double weight_flux)
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

                    if (U_new.rho < 1e-12)
                    {
                        U_new.rho = 1e-12;
                        U_new.mom_x = 0.0;
                        U_new.mom_y = 0.0;
                        U_new.mom_z = 0.0;
                    }

                    u_dest.set(idx, U_new);

                    double rho_new = std::max(U_new.rho, 1e-13);
                    double sum_Y = 0.0;
                    for (int s = 0; s < n_spec; ++s)
                    {
                        int off = s * total_size;
                        double rhoY_old = u_n.rho[idx] * u_n.Y(s, idx);
                        double rhoY_curr = u_current.rho[idx] * u_current.Y(s, idx);
                        double rhoY_comb = weight_n * rhoY_old + weight_flux * (rhoY_curr + d_spec[off + idx]);

                        double Y_k = std::max(0.0, rhoY_comb / rho_new);
                        u_dest.Y(s, idx) = Y_k;
                        sum_Y += Y_k;
                    }

                    if (sum_Y > 1e-13)
                    {
                        double inv_sum = 1.0 / sum_Y;
                        for (int s = 0; s < n_spec; ++s)
                            u_dest.Y(s, idx) *= inv_sum;
                    }
                    else
                    {
                        u_dest.Y(0, idx) = 1.0;
                    }
                }
            }
    }

    // ---------------------------------------------------------
    // Helper 3: Evaluate Fluxes for all Dimensions
    // Note: Template requires FluxSchemePolicy to call compute_fluxes
    // ---------------------------------------------------------
    template <typename FluxSchemePolicy, typename EosType>
    inline void evaluate_all_dimensions(
        const FluidState &state, const EosType &eos, const Grid &grid, double dt,
        std::vector<FluidVector> &dU, std::vector<double> &d_spec,
        std::vector<FluidVector> &flux_buffer, std::vector<double> &spec_flux_buffer,
        double entropy_fix_coeff)
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
        }

        add_geometric_sources(dU, state, eos, grid, dt);
    }
} // namespace TimeIntegration