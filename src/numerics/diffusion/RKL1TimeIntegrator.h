/**
 * @file RKL1TimeIntegrator.h
 * @brief First-order Runge-Kutta-Legendre (RKL1) Super-Time-Stepping scheme for parabolic terms.
 *
 * Workflow:
 * 1. Compute the optimal number of RKL1 stages `s`.
 * 2. Perform the first explicit Euler-like stage (Stage 1).
 * 3. Iteratively evaluate Stages 2 to s, using recursive polynomial coefficients.
 * 4. Ensure stable time integration of the diffusion term over a time step dt_hydro.
 */

#pragma once

#include "DiffFlux.h"
#include "DiffFunction.h"
#include "DiffusionAMRStages.h"

#include "../../data/FluidState.h"
#include "../../data/GlobalDefs.h"
#include "../../grid/Grid.h"

// First-order RKL super-time-step integrator.

struct RKL1TimeIntegrator
{
    static void integrate(FluidState& state, const auto& eos, const Grid& grid, const SimConfig& config, double dt_hydro, double dt_diff, const auto& bc_handler)
    {
        double cfl = config.physics.diffusion.diff_cfl;
        int max_stages = config.physics.diffusion.max_stages;
        int s = DiffFunction::compute_stages(DiffFunction::RKLOrder::First, dt_hydro, dt_diff, cfl, max_stages);
        if (s == 0) return;

        // Static buffers to avoid reallocation overhead every time step
        static FluidState Y0, Y_jm1, Y_jm2, L_U;
        if (Y0.rho.size() != static_cast<size_t>(grid.GetTotalSize())
            || Y0.GetNumSpecies() != state.GetNumSpecies()) {
            for (FluidState* buffer : {&Y0, &Y_jm1, &Y_jm2, &L_U}) {
                buffer->Preallocate(grid.GetTotalSize());
                buffer->InitSpecies(state.GetNumSpecies());
            }
        }

        // Y0 = U^n
        Y0 = state;
        Y_jm2 = state; // For RKL1, j-2 is initially Y0
        Y_jm1 = Y0; // Diffusion stages preserve the burn-owned ENUC diagnostic.

        // Stage 1
        DiffFlux::compute_diffusion_operator(Y0, L_U, eos, grid, config);
        auto c1 = DiffFunction::get_rkl_coeffs(DiffFunction::RKLOrder::First, 1, s);

        #pragma omp parallel for schedule(static)
        for (int i = 0; i < grid.GetTotalSize(); ++i) {
            Y_jm1.rho[i] = ARCH_DIFFUSION_FIRST_RKL_COMPONENT(
                Y0.rho[i], c1.tilde_mu * dt_hydro, L_U.rho[i]);
            Y_jm1.mom_u[i] = ARCH_DIFFUSION_FIRST_RKL_COMPONENT(
                Y0.mom_u[i], c1.tilde_mu * dt_hydro, L_U.mom_u[i]);
            Y_jm1.mom_v[i] = ARCH_DIFFUSION_FIRST_RKL_COMPONENT(
                Y0.mom_v[i], c1.tilde_mu * dt_hydro, L_U.mom_v[i]);
            Y_jm1.mom_w[i] = ARCH_DIFFUSION_FIRST_RKL_COMPONENT(
                Y0.mom_w[i], c1.tilde_mu * dt_hydro, L_U.mom_w[i]);
            Y_jm1.eng[i] = ARCH_DIFFUSION_FIRST_RKL_COMPONENT(
                Y0.eng[i], c1.tilde_mu * dt_hydro, L_U.eng[i]);
            for (int k = 0; k < state.GetNumSpecies(); ++k) {
                const double rhoX_new =
                    ARCH_DIFFUSION_FIRST_RKL_SPECIES_EXPRESSION(
                        Y0.rho[i], Y0.X(k, i), c1.tilde_mu * dt_hydro,
                        L_U.X(k, i));
                Y_jm1.X(k, i) = rhoX_new / Y_jm1.rho[i];
            }
        }

        bc_handler.apply(Y_jm1, grid);

        // Stages 2 to s
        for (int j = 2; j <= s; ++j) {
            DiffFlux::compute_diffusion_operator(Y_jm1, L_U, eos, grid, config);
            auto cj = DiffFunction::get_rkl_coeffs(DiffFunction::RKLOrder::First, j, s);

            FluidState Y_j = Y0; // Reuse Y0's allocated shape for the next stage state.

            #pragma omp parallel for schedule(static)
            for (int i = 0; i < grid.GetTotalSize(); ++i) {
                Y_j.rho[i] = ARCH_DIFFUSION_UNSCALED_RKL1_COMPONENT(
                    cj, Y_jm1.rho[i], Y_jm2.rho[i], L_U.rho[i], dt_hydro);
                Y_j.mom_u[i] = ARCH_DIFFUSION_UNSCALED_RKL1_COMPONENT(
                    cj, Y_jm1.mom_u[i], Y_jm2.mom_u[i], L_U.mom_u[i],
                    dt_hydro);
                Y_j.mom_v[i] = ARCH_DIFFUSION_UNSCALED_RKL1_COMPONENT(
                    cj, Y_jm1.mom_v[i], Y_jm2.mom_v[i], L_U.mom_v[i],
                    dt_hydro);
                Y_j.mom_w[i] = ARCH_DIFFUSION_UNSCALED_RKL1_COMPONENT(
                    cj, Y_jm1.mom_w[i], Y_jm2.mom_w[i], L_U.mom_w[i],
                    dt_hydro);
                Y_j.eng[i] = ARCH_DIFFUSION_UNSCALED_RKL1_COMPONENT(
                    cj, Y_jm1.eng[i], Y_jm2.eng[i], L_U.eng[i], dt_hydro);
                for (int k = 0; k < state.GetNumSpecies(); ++k) {
                    const double rhoX_new =
                        ARCH_DIFFUSION_UNSCALED_RKL1_SPECIES_EXPRESSION(
                            cj, Y_jm1.rho[i], Y_jm1.X(k, i),
                            Y_jm2.rho[i], Y_jm2.X(k, i), L_U.X(k, i),
                            dt_hydro);
                    Y_j.X(k, i) = rhoX_new / Y_j.rho[i];
                }
            }

            bc_handler.apply(Y_j, grid);

            // Shift history
            Y_jm2 = Y_jm1;
            Y_jm1 = Y_j;
        }

        state = Y_jm1;
    }
};

namespace Numerics::Diffusion {

/**
 * @brief Advances all AMR leaves with the conservative composite RKL1 polynomial.
 */
template <typename EosType, typename BCPolicy>
inline void advance_amr_rkl1(amr::AMRControl& amr_ctrl, double dt, double dt_diff_fe,
                             BCPolicy& boundary_condition, const EosType& eos,
                             const SimConfig& config)
{
    advance_amr_rkl(amr_ctrl, dt, dt_diff_fe, boundary_condition, eos, config,
                            DiffFunction::RKLOrder::First);
}

} // namespace Numerics::Diffusion
