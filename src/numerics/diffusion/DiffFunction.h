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
     * @brief Selects the temporal accuracy of an RKL super-time-stepping polynomial.
     * First is an explicit first-order option; Second is the default production method.
     */
    enum class RKLOrder { First, Second };
    /**
     * @brief Computes the required number of STS stages 's'
     * based on hydro time step and explicit diffusion time step.
     */
    int compute_stages_rkl1(double dt_hydro, double dt_diff, double cfl, int max_stages);
    int compute_stages_rkl2(double dt_hydro, double dt_diff, double cfl, int max_stages);
    int compute_stages(RKLOrder order, double dt_hydro, double dt_diff, double cfl, int max_stages);

    /// Largest admissible stage count after RKL order-specific constraints.
    int usable_max_stages_rkl1(int max_stages);
    int usable_max_stages_rkl2(int max_stages);
    int usable_max_stages(RKLOrder order, int max_stages);

    /// Maximum macro step stably represented by a bounded RKL polynomial.
    double stable_step_rkl1(double dt_forward_euler, double cfl, int stages);
    double stable_step_rkl2(double dt_forward_euler, double cfl, int stages);
    double stable_step(RKLOrder order, double dt_forward_euler, double cfl, int stages);

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
    RKLCoeffs get_rkl_coeffs(RKLOrder order, int j, int s);
}
