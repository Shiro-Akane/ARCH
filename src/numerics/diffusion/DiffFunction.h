/**
 * @file DiffFunction.h
 * @brief Computes Runge-Kutta-Legendre (RKL) super-time-stepping stages and coefficients.
 * *
 * * Workflow:
 * * 1. Calculate the required number of stages (s) based on the ratio dt_hydro / dt_diff.
 * * 2. Generate the recurrence coefficients (mu, nu, tilde_mu, gamma) for each stage j=1..s.
 */

#pragma once

// =========================================================
// =================== DiffFunction Namespace ==============
// =========================================================

namespace DiffFunction
{
    /**
     * @brief Computes the required number of STS stages 's' 
     * based on hydro time step and explicit diffusion time step.
     */
    int compute_stages_rkl1(double dt_hydro, double dt_diff, double cfl, int max_stages);
    int compute_stages_rkl2(double dt_hydro, double dt_diff, double cfl, int max_stages);

    struct RKLCoeffs
    {
        double mu;
        double nu;
        double tilde_mu;
        double gamma;
    };

    /**
     * @brief Returns the RKL1 coefficients for stage j out of s.
     * Meyer et al. (2012) formulation.
     */
    RKLCoeffs get_rkl1_coeffs(int j, int s);

    /**
     * @brief Returns the RKL2 coefficients for stage j out of s.
     * Meyer, Balsara, Aslam (2014) formulation.
     */
    RKLCoeffs get_rkl2_coeffs(int j, int s);
}
