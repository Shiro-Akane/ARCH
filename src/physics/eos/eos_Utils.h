/**
 * @file EOS_Utils.h
 * @brief Common thermodynamic and kinematic utilities for EOS solvers.
 */
#pragma once

#include <algorithm>
#include <cmath>

#include "eos.h" // Provides FluidVector through the EOS policy surface.

#ifndef EOS_INLINE
#define EOS_INLINE inline
#endif

namespace eos_utils
{
    // Shared kinetic/internal-energy conversions.
    EOS_INLINE double calc_kinetic_energy(double rho, double u, double v, double w)
    {
        return 0.5 * rho * (u * u + v * v + w * w);
    }

    // Extract specific internal energy from a conservative state.
    EOS_INLINE double extract_specific_internal_energy(const FluidVector &U)
    {
        if (U.rho < 1e-12)
            return 0.0;
        double kinetic_density = 0.5 * (U.mom_u * U.mom_u + U.mom_v * U.mom_v + U.mom_w * U.mom_w) / U.rho;
        return (U.eng - kinetic_density) / U.rho;
    }

    // Generic Newton pressure inversion for three- and four-dimensional tables.
    // TEOSView must provide get_pressure_from_rho_e and get_dp_de_rho.
    template <typename TEOSView>
    EOS_INLINE double solve_total_energy(const TEOSView &eos_view,
                                         double rho, double u, double v, double w,
                                         double target_p, const double *Xi)
    {
        // Initialize with the gamma=1.4 ideal-gas estimate. This is a numerical
        // seed only; all accepted iterates use the selected tabular EOS.
        double e_guess = target_p / ((1.4 - 1.0) * rho);

        for (int iter = 0; iter < 20; ++iter)
        {
            double p_guess = eos_view.get_pressure_from_rho_e(rho, e_guess, Xi);
            double dp_de = eos_view.get_dp_de_rho(rho, e_guess, Xi);

            if (std::abs(dp_de) < 1e-12)
                break;

            double delta_e = (target_p - p_guess) / dp_de;

            // Backtrack until the trial specific internal energy remains above
            // the 1e-12 positivity floor.
            while (e_guess + delta_e <= 1e-12)
            {
                delta_e *= 0.5;
            }
            e_guess += delta_e;

            if (std::abs(delta_e) < 1e-6 * e_guess)
                break;
        }

        return rho * e_guess + calc_kinetic_energy(rho, u, v, w);
    }
}
