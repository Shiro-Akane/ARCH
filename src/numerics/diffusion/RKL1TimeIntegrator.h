/**
 * @file RKL1TimeIntegrator.h
 * @brief First-order Runge-Kutta-Legendre (RKL1) Super-Time-Stepping scheme for parabolic terms.
 * *
 * * Workflow:
 * * 1. Compute the optimal number of RKL1 stages `s`.
 * * 2. Perform the first explicit Euler-like stage (Stage 1).
 * * 3. Iteratively evaluate Stages 2 to s, using recursive polynomial coefficients.
 * * 4. Ensure stable time integration of the diffusion term over a time step dt_hydro.
 */

#pragma once

#include "../../data/FluidState.h"
#include "../../grid/Grid.h"
#include "../../data/GlobalDefs.h"
#include "DiffFlux.h"
#include "DiffFunction.h"

// =========================================================
// ================= RKL1TimeIntegrator ====================
// =========================================================

struct RKL1TimeIntegrator
{
    static void integrate(FluidState& state, const auto& eos, const Grid& grid, const SimConfig& config, double dt_hydro, double dt_diff, const auto& bc_handler)
    {
        double cfl = config.physics.diffusion.diff_cfl;
        int max_stages = config.physics.diffusion.max_stages;
        int s = DiffFunction::compute_stages_rkl1(dt_hydro, dt_diff, cfl, max_stages);
        if (s == 0) return;

        // Static buffers to avoid reallocation overhead every time step
        static FluidState Y0, Y_jm1, Y_jm2, L_U;
        if (Y0.total_size_ != state.total_size_) {
            Y0.Resize(grid, state.GetNumSpecies());
            Y_jm1.Resize(grid, state.GetNumSpecies());
            Y_jm2.Resize(grid, state.GetNumSpecies());
            L_U.Resize(grid, state.GetNumSpecies());
        }

        // Y0 = U^n
        Y0 = state;
        Y_jm2 = state; // For RKL1, j-2 is initially Y0

        // Stage 1
        DiffFlux::compute_diffusion_operator(Y0, L_U, eos, grid, config);
        auto c1 = DiffFunction::get_rkl1_coeffs(1, s);
        
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < grid.GetTotalSize(); ++i) {
            Y_jm1.rho[i]   = Y0.rho[i]   + c1.tilde_mu * dt_hydro * L_U.rho[i];
            Y_jm1.mom_x[i] = Y0.mom_x[i] + c1.tilde_mu * dt_hydro * L_U.mom_x[i];
            Y_jm1.mom_y[i] = Y0.mom_y[i] + c1.tilde_mu * dt_hydro * L_U.mom_y[i];
            Y_jm1.mom_z[i] = Y0.mom_z[i] + c1.tilde_mu * dt_hydro * L_U.mom_z[i];
            Y_jm1.eng[i]   = Y0.eng[i]   + c1.tilde_mu * dt_hydro * L_U.eng[i];
            for (int k = 0; k < state.GetNumSpecies(); ++k) {
                double rhoX_new = Y0.rho[i] * Y0.X(k, i) + c1.tilde_mu * dt_hydro * L_U.X(k, i);
                Y_jm1.X(k, i) = std::max(0.0, rhoX_new / std::max(Y_jm1.rho[i], 1e-12));
            }
        }

        bc_handler.apply(Y_jm1, grid);

        // Stages 2 to s
        for (int j = 2; j <= s; ++j) {
            DiffFlux::compute_diffusion_operator(Y_jm1, L_U, eos, grid, config);
            auto cj = DiffFunction::get_rkl1_coeffs(j, s);

            FluidState Y_j = Y0; // Temp to hold next stage, reuse Y0's sizing structure though we manually assign
            
            #pragma omp parallel for schedule(static)
            for (int i = 0; i < grid.GetTotalSize(); ++i) {
                Y_j.rho[i]   = cj.mu * Y_jm1.rho[i]   + cj.nu * Y_jm2.rho[i]   + cj.tilde_mu * dt_hydro * L_U.rho[i];
                Y_j.mom_x[i] = cj.mu * Y_jm1.mom_x[i] + cj.nu * Y_jm2.mom_x[i] + cj.tilde_mu * dt_hydro * L_U.mom_x[i];
                Y_j.mom_y[i] = cj.mu * Y_jm1.mom_y[i] + cj.nu * Y_jm2.mom_y[i] + cj.tilde_mu * dt_hydro * L_U.mom_y[i];
                Y_j.mom_z[i] = cj.mu * Y_jm1.mom_z[i] + cj.nu * Y_jm2.mom_z[i] + cj.tilde_mu * dt_hydro * L_U.mom_z[i];
                Y_j.eng[i]   = cj.mu * Y_jm1.eng[i]   + cj.nu * Y_jm2.eng[i]   + cj.tilde_mu * dt_hydro * L_U.eng[i];
                for (int k = 0; k < state.GetNumSpecies(); ++k) {
                    double rhoX_new = cj.mu * Y_jm1.rho[i] * Y_jm1.X(k, i) + cj.nu * Y_jm2.rho[i] * Y_jm2.X(k, i) + cj.tilde_mu * dt_hydro * L_U.X(k, i);
                    Y_j.X(k, i) = std::max(0.0, rhoX_new / std::max(Y_j.rho[i], 1e-12));
                }
            }

            bc_handler.apply(Y_j, grid);

            // Shift history
            Y_jm2 = Y_jm1;
            Y_jm1 = Y_j;
        }

        state = (s == 1) ? Y_jm1 : Y_jm1; // Y_jm1 holds the final result
    }
};
