/**
 * @file Limiters.h
 * @brief Slope limiters for MUSCL reconstruction.
 * Defines the slope limiter functions phi(r).
 * r is the ratio of successive gradients: r = (u_i - u_{i-1}) / (u_{i+1} - u_i)
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include "../../core/ArchPortability.h"

/**
 * @struct NoLimiter
 * @brief Suppress the reconstructed gradient and recover first-order PCM.
 * Here "None" disables reconstruction: phi(r)=0 selects a first-order policy,
 * not an unlimited second-order slope.
 */
struct NoLimiter
{
    static std::string name() { return "None (1st Order)"; }

    static ARCH_INLINE double calc(double r)
    {
        return 0.0;
    }
};

/**
 * @struct MinMod
 * @brief Diffusive total-variation-diminishing limiter.
 * phi(r) = max(0, min(1, r))
 */
struct MinMod
{
    static std::string name() { return "MinMod"; }

    static ARCH_INLINE double calc(double r)
    {
        return (r > 0.0) ? (r < 1.0 ? r : 1.0) : 0.0;
    }
};

/**
 * @struct SuperBee
 * @brief Compressive limiter with sharp contact resolution.
 * phi(r) = max(0, min(2r, 1), min(r, 2))
 * Its compression can distort smooth waves even while sharpening contacts.
 */
struct SuperBee
{
    static std::string name() { return "SuperBee"; }

    static ARCH_INLINE double calc(double r)
    {
        if (r <= 0.0)
            return 0.0;
        if (r >= 2.0)
            return 2.0; // TVD upper bound for the SuperBee limiter.
        if (r <= 0.5)
            return 2.0 * r;
        if (r >= 1.0)
            return r; // min(r, 2) dominates for 1 <= r < 2.

        // Standard form: max(0, min(1,2r), min(2,r)).
        double a = (2.0 * r < 1.0) ? 2.0 * r : 1.0;
        double b = (r < 2.0) ? r : 2.0;
        return (a > b) ? a : b;
    }
};

/**
 * @struct VanLeer
 * @brief Smooth limiter between MinMod diffusion and SuperBee compression.
 * phi(r) = (r + |r|) / (1 + |r|)
 * It provides a conventional compromise between MinMod and SuperBee.
 */
struct VanLeer
{
    static std::string name() { return "VanLeer"; }

    static ARCH_INLINE double calc(double r)
    {
        // Opposite-sign slopes require phi=0 to preserve monotonicity.
        if (r <= 0.0)
            return 0.0;
        return (2.0 * r) / (1.0 + r);
    }
};

/**
 * @struct McLimiter
 * @brief Monotonized Central (MC) Limiter.
 * phi(r) = max(0, min(2r, 0.5*(1+r), 2))
 * In smooth regions this limiter approaches its higher-order centered branch.
 */
struct McLimiter
{
    static std::string name() { return "MC"; }

    static ARCH_INLINE double calc(double r)
    {
        if (r <= 0.0)
            return 0.0;

        double term2 = 0.5 * (1.0 + r);
        double term1 = 2.0 * r;
        double term3 = 2.0;

        // min(term1, term2, term3)
        double min_val = (term1 < term2) ? term1 : term2;
        min_val = (min_val < term3) ? min_val : term3;

        return min_val;
    }
};
