/**
 * @file FluxFunctions.h
 * @brief Collection of flux calculation routines for Euler equations.
 * * This file provides the fundamental building blocks for finite volume schemes:
 * * 1. Analytic Physical Flux F(U).
 * * 2. Intermediate Flux for Richtmyer (Lax-Wendroff) scheme.
 * * 3. Steger-Warming Flux Vector Splitting (FVS) for upwind schemes.
 */

#pragma once

#include <vector>

#include "../../data/FluidState.h"

// ------------------------------------------------------------------
// 1. Analytic Physical Flux
// ------------------------------------------------------------------

/**
 * @brief Computes the physical flux vector F(U) for the 1D Euler equations.
 * * Vector Form:
 * * F = [ rho * u,
 * * rho * u^2 + p,
 * * (E + p) * u ]
 * * @tparam EosType Equation of State class.
 * @param U  Conservative state vector (rho, mom, eng).
 * @param Yi Species mass fractions.
 * @param eos EOS object for pressure calculation.
 * @return FluidVector3 The flux vector F.
 */
template <typename EosType>
FluidVector3 get_flux(const FluidVector3 &U, const double *Yi, const EosType &eos)
{
    double rho = U.rho;
    // Safety: check against vacuum
    double u = (rho > 1e-12) ? U.mom / rho : 0.0;

    // Compute pressure using EOS
    double p = eos.get_pressure(rho, U.mom, U.eng, Yi);

    FluidVector3 F;
    F.rho = U.mom;           // Mass Flux: rho * u
    F.mom = U.mom * u + p;   // Momentum Flux: rho * u^2 + p
    F.eng = (U.eng + p) * u; // Energy Flux: (E + p) * u = H * rho * u
    return F;
}

// ------------------------------------------------------------------
// 2. Richtmyer Scheme Helper (Predictor Step)
// ------------------------------------------------------------------

/**
 * @brief Computes the flux at the half-step state (Richtmyer predictor).
 * * Used in the Two-Step Lax-Wendroff method.
 * * Step 1: Compute intermediate state U_{i+1/2}^{n+1/2} using Lax-Friedrichs average.
 * * Step 2: Compute Flux F(U_{half}).
 * * @param U_L  State at index i.
 * @param Yi_L Species at index i.
 * @param U_R  State at index i+1.
 * @param Yi_R Species at index i+1.
 * @param Yi_half_buffer Output buffer for averaged species.
 * @param n_species Number of species.
 * @param eos EOS object.
 * @param dt  Time step.
 * @param grid Grid info (for dx).
 * @return FluidVector3 Flux evaluated at the half-step state.
 */
template <typename EosType>
FluidVector3 compute_half_step_flux(const FluidVector3 &U_L, const double *Yi_L,
                                    const FluidVector3 &U_R, const double *Yi_R,
                                    double *Yi_half_buffer,
                                    int n_species,
                                    const EosType &eos, double dt, const Grid &grid)
{
    double dx = grid.dx;

    // Calculate fluxes at the left and right states
    FluidVector3 F_L = get_flux(U_L, Yi_L, eos); // flux of i-1/2
    FluidVector3 F_R = get_flux(U_R, Yi_R, eos); // flux of i+1/2

    // Evolution Formula (Taylor expansion approximation):
    // U_half = Average(U) - (dt/2dx) * delta(F)
    FluidVector3 U_half = 0.5 * (U_L + U_R) - 0.5 * (dt / dx) * (F_R - F_L);

    // Simple arithmetic average for species
    for (int k = 0; k < n_species; ++k)
    {
        Yi_half_buffer[k] = 0.5 * (Yi_L[k] + Yi_R[k]);
    }

    // Return the flux based on this updated half-state
    return get_flux(U_half, Yi_half_buffer, eos);
}

// ------------------------------------------------------------------
// 3. Steger-Warming Flux Vector Splitting
// ------------------------------------------------------------------

/**
 * @brief Calculates the Split Flux F+ or F- using Steger-Warming method.
 * * Decomposes the flux based on the signs of the eigenvalues (u, u+c, u-c).
 * * Used for upwind discretization to capture shocks effectively.
 * * @param U    Conservative state.
 * @param Yi   Species mass fractions.
 * @param eos  EOS object.
 * @param sign Direction indicator (+1 for F_plus, -1 for F_minus).
 * @return FluidVector3 The split flux vector.
 */
template <typename EosType>
FluidVector3 calc_split_flux(const FluidVector3 &U, const double *Yi,
                             const EosType &eos, int sign)
{
    double rho = std::max(U.rho, 1e-12); // Prevent division by zero
    double u = U.mom / rho;

    double p = eos.get_pressure(rho, U.mom, U.eng, Yi);
    double c = eos.get_sound_speed(rho, p, Yi);
    double H = (U.eng + p) / rho; // Total Enthalpy
    double gamma = eos.get_gamma(Yi);

    // Lambda helper:
    // If sign > 0, returns max(lambda, 0) -> Positive Eigenvalues
    // If sign < 0, returns min(lambda, 0) -> Negative Eigenvalues
    double eps = 0.1 * c;

    auto split_lambda = [&](double l)
    {
        double l_abs_smoothed = (std::abs(l) < eps) ? (l * l + eps * eps) / (2.0 * eps) : std::abs(l);
        return (sign > 0) ? 0.5 * (l + l_abs_smoothed) : 0.5 * (l - l_abs_smoothed);
    };

    // 1. Eigenvalues of the Jacobian matrix
    double l1 = split_lambda(u);
    double l2 = split_lambda(u + c);
    double l3 = split_lambda(u - c);

    // 2. Steger-Warming weighting factors
    // These derived from the homogeneity property of the Euler equations
    double f1 = (gamma - 1.0) / gamma;
    double f2 = 0.5 / gamma;

    double w1 = f1 * l1;
    double w2 = f2 * l2;
    double w3 = f2 * l3;

    // 3. Reconstruct Split Flux Vector F_split
    FluidVector3 F_split;

    // Mass Flux Component
    F_split.rho = (w1 + w2 + w3) * rho;

    // Momentum Flux Component
    F_split.mom = w1 * (rho * u) + w2 * (rho * (u + c)) + w3 * (rho * (u - c));

    // Energy Flux Component
    // Term w1 corresponds to entropy wave (speed u)
    // Terms w2, w3 correspond to acoustic waves (speed u +/- c)
    F_split.eng = w1 * (0.5 * rho * u * u) + w2 * (rho * (H + u * c)) + w3 * (rho * (H - u * c));

    return F_split;
}

// ------------------------------------------------------------------
// 4. Vinokur-Von Leer Flux Vector Splitting
// ------------------------------------------------------------------

/**
 * @brief Vinokur-Van Leer Flux Vector Splitting for General EOS
 * * @param U      Conserved variables (rho, mom, eng)
 * @param Yi     Species mass fractions array
 * @param eos    Equation of State object
 * @param sign   Direction indicator:
 * > 0: Calculate F+ (Forward/Positive flux component)
 * < 0: Calculate F- (Backward/Negative flux component)
 */
template <typename EosType>
FluidVector3 calc_vinokur_flux(const FluidVector3 &U, const double *Yi,
                               const EosType &eos, int sign)
{
    // 1. Pre-calculation & Protection
    double rho = std::max(U.rho, 1e-12);
    double u = U.mom / rho;

    // Get thermodynamics from EOS
    // Note: ensure your eos.get_pressure can handle small rho
    double p = eos.get_pressure(rho, U.mom, U.eng, Yi);
    double c = eos.get_sound_speed(rho, p, Yi);

    // Calculate Equivalent Gamma (Vinokur's Gamma)
    // Protection for vacuum/zero pressure is crucial here
    double gamma_eff = (p > 1e-12) ? (rho * c * c / p) : 1.4;

    // Calculate Mach Number
    double M = u / c;
    // 2. Supersonic Branching (Optimization & Validity)
    // If we want F+ (sign>0) and flow is supersonic backward (M <= -1), F+ is 0.
    // If we want F- (sign<0) and flow is supersonic forward (M >= 1), F- is 0.

    if (sign > 0)
    { // Calculating F+
        if (M >= 1.0)
        {
            // Full Flux F
            double E_total = U.eng;
            return FluidVector3{
                U.mom,            // rho * u
                U.mom * u + p,    // rho * u^2 + p
                (E_total + p) * u // (E + p) * u
            };
        }
        if (M <= -1.0)
        {
            return FluidVector3{0.0, 0.0, 0.0};
        }
    }
    else
    { // Calculating F- (sign < 0)
        if (M >= 1.0)
        {
            return FluidVector3{0.0, 0.0, 0.0};
        }
        if (M <= -1.0)
        {
            // Full Flux F
            double E_total = U.eng;
            return FluidVector3{
                U.mom,            // rho * u
                U.mom * u + p,    // rho * u^2 + p
                (E_total + p) * u // (E + p) * u
            };
        }
    }

    // 3. Subsonic Branching (|M| < 1) - The Vinokur Polynomials

    // Common factor term: (M +/- 1)
    // If sign > 0 (F+), we use (M + 1)
    // If sign < 0 (F-), we use (M - 1)
    double factor = (sign > 0) ? (M + 1.0) : (M - 1.0);

    // Mass Flux (The split mass term)
    // f_mass = +/- rho * c * (M +/- 1)^2 / 4
    double term_sign = (sign > 0) ? 1.0 : -1.0;
    double f_mass = term_sign * 0.25 * rho * c * factor * factor;

    // 4. Construct Split Flux Vector
    FluidVector3 F_split;

    // --- Mass Equation ---
    F_split.rho = f_mass;

    // --- Momentum Equation ---
    // Vinokur Momentum Term using Equivalent Gamma
    // D +/- = [ (gamma_eff - 1)*u +/- 2*c ] / gamma_eff
    double term_2c = (sign > 0) ? (2.0 * c) : (-2.0 * c);
    double D_split = ((gamma_eff - 1.0) * u + term_2c) / gamma_eff;

    F_split.mom = f_mass * D_split;

    // --- Energy Equation (Crucial Vinokur Correction) ---

    // Part A: Ideal Gas Energy Term (using gamma_eff)
    // H_ideal = D_split^2 * gamma_eff^2 / (2 * (gamma_eff^2 - 1))
    // Simplifies to the form below:
    double numerator = ((gamma_eff - 1.0) * u + term_2c);
    double E_ideal_term = (numerator * numerator) / (2.0 * (gamma_eff * gamma_eff - 1.0));

    // Part B: The Correction Term for General EOS
    // We need real specific enthalpy h = e + p/rho
    // Note: U.eng is rho * E_total -> specific internal energy e = (U.eng/rho) - 0.5*u*u
    double e_internal = (U.eng / rho) - 0.5 * u * u;
    double h_real = e_internal + p / rho;

    // Ideal enthalpy based on sound speed
    double h_ideal = (c * c) / (gamma_eff - 1.0);

    // Combine: F_energy = f_mass * ( E_ideal_term + (h_real - h_ideal) )
    F_split.eng = f_mass * (E_ideal_term + (h_real - h_ideal));

    return F_split;
}
