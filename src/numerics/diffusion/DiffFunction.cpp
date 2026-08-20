/**
 * @file DiffFunction.cpp
 * @brief Computes Runge-Kutta-Legendre (RKL) super-time-stepping stages and coefficients.
 *
 * Workflow:
 * 1. Calculate the required number of stages (s) based on the ratio dt_hydro / dt_diff.
 * 2. Generate the recurrence coefficients (mu, nu, tilde_mu, gamma) for each stage j=1..s.
 */

#include <algorithm>
#include <cmath>

#include "DiffFunction.h"

// RKL stage-count and recurrence-coefficient utilities.

namespace DiffFunction
{
    // 1. Stage calculations
    int compute_stages_rkl1(double dt_hydro, double dt_diff, double cfl, int max_stages)
    {
        const int stage_cap = usable_max_stages_rkl1(max_stages);
        if (dt_hydro <= cfl * dt_diff) return 1;
        // Shifted Legendre stability interval: dt_STS/dt_FE = s(s+1)/2.
        const double ratio = dt_hydro / (cfl * dt_diff);
        int s = static_cast<int>(std::ceil(0.5 * (std::sqrt(1.0 + 8.0 * ratio) - 1.0)));
        s = std::max(1, s);
        return std::min(s, stage_cap);
    }

    int compute_stages_rkl2(double dt_hydro, double dt_diff, double cfl, int max_stages)
    {
        const int stage_cap = usable_max_stages_rkl2(max_stages);
        if (dt_hydro <= cfl * dt_diff) return 2;
        // For RKL2, dt_STS \approx (s^2 + s - 2)/4 dt_diff
        // 4 * dt_hydro / (cfl * dt_diff) = s^2 + s - 2
        // s \approx sqrt(4 * ratio + 2.25) - 0.5
        double ratio = dt_hydro / (cfl * dt_diff);
        int s = static_cast<int>(std::ceil(std::sqrt(4.0 * ratio + 2.25) - 0.5));

        // RKL2 requires at least two stages. Odd stage counts are selected below
        // to match the supported recurrence and stage-cap convention.
        if (s < 2) s = 2;
        // Use the next odd stage count when it remains within the configured cap.
        if (s % 2 == 0) s++;

        if (s > stage_cap) {
            s = stage_cap;
            if (s % 2 == 0 && s > 2) s--;
        }
        return s;
    }


    int usable_max_stages_rkl1(int max_stages)
    {
        return std::max(1, max_stages);
    }

    int usable_max_stages_rkl2(int max_stages)
    {
        int stages = std::max(2, max_stages);
        if (stages % 2 == 0 && stages > 2) --stages;
        return stages;
    }

    double stable_step_rkl1(double dt_forward_euler, double cfl, int stages)
    {
        return cfl * dt_forward_euler * stages * (stages + 1.0) / 2.0;
    }

    double stable_step_rkl2(double dt_forward_euler, double cfl, int stages)
    {
        return cfl * dt_forward_euler * (stages * stages + stages - 2.0) / 4.0;
    }

    // 2. RKL Coefficients

    RKLCoeffs get_rkl1_coeffs(int j, int s)
    {
        RKLCoeffs c;
        const double w1 = 2.0 / (s * (s + 1.0));
        if (j == 1) {
            c.mu = 0.0;
            c.nu = 0.0;
            c.tilde_mu = w1;
            c.gamma = 0.0;
            return c;
        }

        // Meyer 2012 RKL1
        c.mu = (2.0 * j - 1.0) / j;
        c.nu = (1.0 - j) / j;
        c.tilde_mu = c.mu * w1;
        c.gamma = 0.0; // Not used in RKL1
        return c;
    }

    // Helper for Meyer 2014 RKL2 / RKL3 b_j
    static double b_j(int j)
    {
        if (j == 0 || j == 1) return 1.0 / 3.0;
        return (j * j + j - 2.0) / (2.0 * j * (j + 1.0));
    }

    RKLCoeffs get_rkl2_coeffs(int j, int s)
    {
        RKLCoeffs c;
        // w1 is the weighting factor for RKL2
        double w1 = 4.0 / (s * s + s - 2.0);

        if (j == 1) {
            c.mu = 0.0;
            c.nu = 0.0;
            c.tilde_mu = b_j(1) * w1;
            c.gamma = 0.0;
            return c;
        }

        double bj = b_j(j);
        double bj_minus_1 = b_j(j - 1);
        double bj_minus_2 = b_j(j - 2);

        c.mu = ((2.0 * j - 1.0) / j) * (bj / bj_minus_1);
        c.nu = ((1.0 - j) / j) * (bj / bj_minus_2);

        c.tilde_mu = c.mu * w1; // Meyer 2014 definition

        // gamma_j = -(1 - b_{j-1}) * tilde_mu_j
        c.gamma = - (1.0 - bj_minus_1) * c.tilde_mu;

        return c;
    }

    int compute_stages(RKLOrder order, double dt_hydro, double dt_diff, double cfl, int max_stages)
    {
        return order == RKLOrder::First
            ? compute_stages_rkl1(dt_hydro, dt_diff, cfl, max_stages)
            : compute_stages_rkl2(dt_hydro, dt_diff, cfl, max_stages);
    }

    int usable_max_stages(RKLOrder order, int max_stages)
    {
        return order == RKLOrder::First
            ? usable_max_stages_rkl1(max_stages)
            : usable_max_stages_rkl2(max_stages);
    }

    double stable_step(RKLOrder order, double dt_forward_euler, double cfl, int stages)
    {
        return order == RKLOrder::First
            ? stable_step_rkl1(dt_forward_euler, cfl, stages)
            : stable_step_rkl2(dt_forward_euler, cfl, stages);
    }

    RKLCoeffs get_rkl_coeffs(RKLOrder order, int stage, int stages)
    {
        return order == RKLOrder::First
            ? get_rkl1_coeffs(stage, stages)
            : get_rkl2_coeffs(stage, stages);
    }
}
