/**
 * @file RKL2TimeIntegrator.h
 * @brief Second-order Runge-Kutta-Legendre (RKL2) Super-Time-Stepping scheme for parabolic terms.
 * *
 * * Workflow:
 * * 1. Compute the optimal number of RKL2 stages `s`.
 * * 2. Evaluate the initial diffusion operator L(Y_0), which is reused in every stage.
 * * 3. Perform Stage 1 integration.
 * * 4. Iteratively evaluate Stages 2 to s, updating state using the Meyer (2014) formulation.
 */

#pragma once

#include "../../data/FluidState.h"
#include "../../grid/Grid.h"
#include "../../data/GlobalDefs.h"
#include "DiffFlux.h"
#include "DiffFunction.h"

// =========================================================
// ================= RKL2TimeIntegrator ====================
// =========================================================

struct RKL2TimeIntegrator
{
    static void integrate(FluidState& state, const auto& eos, const Grid& grid, const SimConfig& config, double dt_hydro, double dt_diff, const auto& bc_handler)
    {
        double cfl = config.physics.diffusion.diff_cfl;
        int max_stages = config.physics.diffusion.max_stages;
        int s = DiffFunction::compute_stages_rkl2(dt_hydro, dt_diff, cfl, max_stages);
        if (s == 0) return;

        static FluidState Y0, Y_jm1, Y_jm2, L_U0, L_U;
        if (Y0.total_size_ != state.total_size_) {
            Y0.Resize(grid, state.GetNumSpecies());
            Y_jm1.Resize(grid, state.GetNumSpecies());
            Y_jm2.Resize(grid, state.GetNumSpecies());
            L_U0.Resize(grid, state.GetNumSpecies());
            L_U.Resize(grid, state.GetNumSpecies());
        }

        Y0 = state;
        Y_jm2 = state; 

        // Initial evaluation L(Y_0) is used in every step for RKL2
        DiffFlux::compute_diffusion_operator(Y0, L_U0, eos, grid, config);
        
        auto c1 = DiffFunction::get_rkl2_coeffs(1, s);
        
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < grid.GetTotalSize(); ++i) {
            Y_jm1.rho[i]   = Y0.rho[i]   + c1.tilde_mu * dt_hydro * L_U0.rho[i];
            Y_jm1.mom_x[i] = Y0.mom_x[i] + c1.tilde_mu * dt_hydro * L_U0.mom_x[i];
            Y_jm1.mom_y[i] = Y0.mom_y[i] + c1.tilde_mu * dt_hydro * L_U0.mom_y[i];
            Y_jm1.mom_z[i] = Y0.mom_z[i] + c1.tilde_mu * dt_hydro * L_U0.mom_z[i];
            Y_jm1.eng[i]   = Y0.eng[i]   + c1.tilde_mu * dt_hydro * L_U0.eng[i];
            for (int k = 0; k < state.GetNumSpecies(); ++k) {
                double rhoX_new = Y0.rho[i] * Y0.X(k, i) + c1.tilde_mu * dt_hydro * L_U0.X(k, i);
                Y_jm1.X(k, i) = std::max(0.0, rhoX_new / std::max(Y_jm1.rho[i], 1e-12));
            }
        }

        bc_handler.apply(Y_jm1, grid);

        for (int j = 2; j <= s; ++j) {
            DiffFlux::compute_diffusion_operator(Y_jm1, L_U, eos, grid, config);
            auto cj = DiffFunction::get_rkl2_coeffs(j, s);

            FluidState Y_j = Y0; 
            
            #pragma omp parallel for schedule(static)
            for (int i = 0; i < grid.GetTotalSize(); ++i) {
                double w0 = (1.0 - cj.mu - cj.nu);
                
                Y_j.rho[i]   = cj.mu * Y_jm1.rho[i]   + cj.nu * Y_jm2.rho[i]   + w0 * Y0.rho[i]   + dt_hydro * (cj.tilde_mu * L_U.rho[i]   + cj.gamma * L_U0.rho[i]);
                Y_j.mom_x[i] = cj.mu * Y_jm1.mom_x[i] + cj.nu * Y_jm2.mom_x[i] + w0 * Y0.mom_x[i] + dt_hydro * (cj.tilde_mu * L_U.mom_x[i] + cj.gamma * L_U0.mom_x[i]);
                Y_j.mom_y[i] = cj.mu * Y_jm1.mom_y[i] + cj.nu * Y_jm2.mom_y[i] + w0 * Y0.mom_y[i] + dt_hydro * (cj.tilde_mu * L_U.mom_y[i] + cj.gamma * L_U0.mom_y[i]);
                Y_j.mom_z[i] = cj.mu * Y_jm1.mom_z[i] + cj.nu * Y_jm2.mom_z[i] + w0 * Y0.mom_z[i] + dt_hydro * (cj.tilde_mu * L_U.mom_z[i] + cj.gamma * L_U0.mom_z[i]);
                Y_j.eng[i]   = cj.mu * Y_jm1.eng[i]   + cj.nu * Y_jm2.eng[i]   + w0 * Y0.eng[i]   + dt_hydro * (cj.tilde_mu * L_U.eng[i]   + cj.gamma * L_U0.eng[i]);
                for (int k = 0; k < state.GetNumSpecies(); ++k) {
                    double rhoX_new = cj.mu * Y_jm1.rho[i] * Y_jm1.X(k, i) + cj.nu * Y_jm2.rho[i] * Y_jm2.X(k, i) + w0 * Y0.rho[i] * Y0.X(k, i) + dt_hydro * (cj.tilde_mu * L_U.X(k, i) + cj.gamma * L_U0.X(k, i));
                    Y_j.X(k, i) = std::max(0.0, rhoX_new / std::max(Y_j.rho[i], 1e-12));
                }
            }

            bc_handler.apply(Y_j, grid);

            Y_jm2 = Y_jm1;
            Y_jm1 = Y_j;
        }

        state = Y_jm1;
    }
};
