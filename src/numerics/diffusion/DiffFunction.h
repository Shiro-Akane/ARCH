/**
 * @file DiffFunction.h
 * @brief Computes Runge-Kutta-Legendre (RKL) super-time-stepping stages and coefficients.
 *
 * Workflow:
 * 1. Calculate the required number of stages (s) based on the ratio dt_hydro / dt_diff.
 * 2. Generate the recurrence coefficients (mu, nu, tilde_mu, gamma) for each stage j=1..s.
 */

#pragma once

// RKL stage-count and recurrence-coefficient utilities.

#include <vector>

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

    /**
     * @brief Global final-mass gains of the RKL stage recurrence ("repair weights").
     *
     * For a conservative composite diffusion operator the global species mass follows
     *
     *     M_j = mu_j * M_(j-1) + nu_j * M_(j-2) + (1 - mu_j - nu_j) * M_0 + delta_j,
     *
     * where delta_j collects the bounded repairs applied before and after the stage
     * reflux, mu_j/nu_j come from get_rkl_coeffs (the sole coefficient source), and the
     * operator terms have zero global sum for closed boundaries. Differentiating M_s
     * with respect to a unit impulse delta_k gives the adjoint recurrence
     *
     *     g_s = 1,  g_(s-1) = mu_s,  g_j = mu_(j+1) * g_(j+1) + nu_(j+2) * g_(j+2),
     *
     * so the returned vector holds g_1..g_s with the gain for stage k at index k-1
     * (size == stages). For stages == 1 the single entry is exactly {1}.
     *
     * The gains record the global final-mass correction only; they are not the full
     * spatial response of the repaired nonlinear trajectory. Signed repair budgets use
     * g_j and absolute budgets use |g_j|, and open-boundary flux must be accounted for
     * separately. A rejected step must not publish pending budgets built from these
     * weights.
     *
     * @param order  RKL1 or RKL2 coefficient family.
     * @param stages Number of stages s, must be positive.
     * @throws std::invalid_argument if stages <= 0.
     */
    std::vector<double> repair_weights(RKLOrder order, int stages);
}
