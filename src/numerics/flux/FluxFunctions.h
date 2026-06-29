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
#include <cmath>

#include "../../data/FluidState.h"

// ==================================================================
// 0. Directional Mapping Helpers (Crucial for 3D)
// ==================================================================

/// Gets the normal velocity component based on the direction (0=x, 1=y, 2=z)
inline double get_un(const FluidVector &U, int dir)
{
    if (dir == 0)
        return U.mom_x / U.rho;
    if (dir == 1)
        return U.mom_y / U.rho;
    return U.mom_z / U.rho;
}

/// Gets the first tangential velocity component
inline double get_ut1(const FluidVector &U, int dir)
{
    if (dir == 0)
        return U.mom_y / U.rho;
    if (dir == 1)
        return U.mom_z / U.rho;
    return U.mom_x / U.rho;
}

/// Gets the second tangential velocity component
inline double get_ut2(const FluidVector &U, int dir)
{
    if (dir == 0)
        return U.mom_z / U.rho;
    if (dir == 1)
        return U.mom_x / U.rho;
    return U.mom_y / U.rho;
}

/// Constructs a FluidVector from normal and tangential flux components
inline FluidVector set_flux_vector(double f_rho, double f_un, double f_ut1, double f_ut2, double f_eng, int dir)
{
    FluidVector F;
    F.rho = f_rho;
    F.eng = f_eng;
    if (dir == 0)
    {
        F.mom_x = f_un;
        F.mom_y = f_ut1;
        F.mom_z = f_ut2;
    }
    else if (dir == 1)
    {
        F.mom_y = f_un;
        F.mom_z = f_ut1;
        F.mom_x = f_ut2;
    }
    else
    {
        F.mom_z = f_un;
        F.mom_x = f_ut1;
        F.mom_y = f_ut2;
    }
    return F;
}

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
 * @param Xi Species mass fractions.
 * @param eos EOS object for pressure calculation.
 * @return FluidVector3 The flux vector F.
 */
template <typename EosType>
FluidVector get_flux(const FluidVector &U, const double *Xi, const EosType &eos, int dir)
{
    double rho = U.rho;
    if (rho < 1e-12)
        return FluidVector(); // Return zeros for vacuum
    // Safety: check against vacuum
    double un = get_un(U, dir);
    double ut1 = get_ut1(U, dir);
    double ut2 = get_ut2(U, dir);

    // Compute pressure using EOS
    double p = eos.get_pressure(U, Xi);

    double f_rho = rho * un;
    double f_un = rho * un * un + p;
    double f_ut1 = rho * un * ut1;
    double f_ut2 = rho * un * ut2;
    double f_eng = (U.eng + p) * un;
    return set_flux_vector(f_rho, f_un, f_ut1, f_ut2, f_eng, dir);
}

// for known pressure situation to save calculation
inline FluidVector get_flux(const FluidVector &U, double p, int dir)
{
    double rho = U.rho;
    if (rho < 1e-12)
        return FluidVector();

    double un = get_un(U, dir);
    double ut1 = get_ut1(U, dir);
    double ut2 = get_ut2(U, dir);

    double f_rho = rho * un;
    double f_un = rho * un * un + p;
    double f_ut1 = rho * un * ut1;
    double f_ut2 = rho * un * ut2;
    double f_eng = (U.eng + p) * un;

    return set_flux_vector(f_rho, f_un, f_ut1, f_ut2, f_eng, dir);
}

// ------------------------------------------------------------------
// 2. Steger-Warming Flux Vector Splitting
// ------------------------------------------------------------------

/**
 * @brief Calculates the Split Flux F+ or F- using Steger-Warming method.
 * * Decomposes the flux based on the signs of the eigenvalues (u, u+c, u-c).
 * * Used for upwind discretization to capture shocks effectively.
 * * @param U    Conservative state.
 * @param Xi   Species mass fractions.
 * @param eos  EOS object.
 * @param sign Direction indicator (+1 for F_plus, -1 for F_minus).
 * @return FluidVector3 The split flux vector.
 */
template <typename EosType>
FluidVector calc_split_flux(const FluidVector &U, const double *Xi,
                            const EosType &eos, int sign, double smoothing_coeff, int dir)
{
    double rho = std::max(U.rho, 1e-12); // Prevent division by zero
    double un = get_un(U, dir);
    double ut1 = get_ut1(U, dir);
    double ut2 = get_ut2(U, dir);
    double V2 = un * un + ut1 * ut1 + ut2 * ut2; // Full kinetic energy

    double p = eos.get_pressure(U, Xi);
    double c = eos.get_sound_speed(U, p, Xi);
    double H = (U.eng + p) / rho;
    double gamma = eos.get_gamma(Xi);

    // Lambda helper:
    // If sign > 0, returns max(lambda, 0) -> Positive Eigenvalues
    // If sign < 0, returns min(lambda, 0) -> Negative Eigenvalues
    double eps = std::max(smoothing_coeff * c, 1e-12);

    auto split_lambda = [&](double l)
    {
        double l_abs_smoothed = (std::abs(l) < eps) ? (l * l + eps * eps) / (2.0 * eps) : std::abs(l);
        return (sign > 0) ? 0.5 * (l + l_abs_smoothed) : 0.5 * (l - l_abs_smoothed);
    };

    // 1. Eigenvalues of the Jacobian matrix
    double l1 = split_lambda(un);
    double l2 = split_lambda(un + c);
    double l3 = split_lambda(un - c);

    // 2. Steger-Warming weighting factors
    // These derived from the homogeneity property of the Euler equations
    double f1 = (gamma - 1.0) / gamma;
    double f2 = 0.5 / gamma;

    double w1 = f1 * l1;
    double w2 = f2 * l2;
    double w3 = f2 * l3;
    double w_sum = w1 + w2 + w3;

    // 3. Reconstruct Split Flux Vector F_split
    // Mass Flux Component
    double f_rho = w_sum * rho;

    // Momentum Flux Component
    double f_un = w1 * (rho * un) + w2 * (rho * (un + c)) + w3 * (rho * (un - c));
    double f_ut1 = f_rho * ut1; // Transverse momentum is purely advected
    double f_ut2 = f_rho * ut2;

    // Energy Flux Component
    // Term w1 corresponds to entropy wave (speed u)
    // Terms w2, w3 correspond to acoustic waves (speed u +/- c)
    double f_eng = w1 * (0.5 * rho * V2) + w2 * rho * (H + un * c) + w3 * rho * (H - un * c);

    return set_flux_vector(f_rho, f_un, f_ut1, f_ut2, f_eng, dir);
}

// ------------------------------------------------------------------
// 3. Vinokur-Von Leer Flux Vector Splitting
// ------------------------------------------------------------------

/**
 * @brief Vinokur-Van Leer Flux Vector Splitting for General EOS
 * * @param U      Conserved variables (rho, mom, eng)
 * @param Xi     Species mass fractions array
 * @param eos    Equation of State object
 * @param sign   Direction indicator:
 * > 0: Calculate F+ (Forward/Positive flux component)
 * < 0: Calculate F- (Backward/Negative flux component)
 */
template <typename EosType>
FluidVector calc_vinokur_flux(const FluidVector &U, const double *Xi,
                              const EosType &eos, int sign, int dir)
{
    // 1. Pre-calculation & Protection
    double rho = std::max(U.rho, 1e-12);
    double un = get_un(U, dir);
    double ut1 = get_ut1(U, dir);
    double ut2 = get_ut2(U, dir);

    // Get thermodynamics from EOS
    // Note: ensure your eos.get_pressure can handle small rho
    double p = eos.get_pressure(U, Xi);
    double c = eos.get_sound_speed(U, p, Xi);

    // Calculate Equivalent Gamma (Vinokur's Gamma)
    // Protection for vacuum/zero pressure is crucial here
    if (std::isnan(c) || c < 1e-8)
        c = std::sqrt(1.4 * p / rho);
    double gamma_eff = (p > 1e-12) ? (rho * c * c / p) : 1.4;

    // ------------------------------------------------------------------------
    // [Note regarding Vinokur Splitting Singularity]
    //
    // Theory:
    // The Vinokur energy flux formula contains divisors (gamma-1) and (gamma^2-1).
    // As gamma_eff -> 1.0 (isothermal limit or numerical error), these terms blow up.
    //
    // Practice:
    // We intentionally DO NOT clamp gamma_eff to a hard floor (e.g., 1.05) here
    // to preserve accuracy for real gases where gamma might be naturally low (e.g. 1.1).
    //
    // Robustness Strategy:
    // Instead of clamping, we rely on the NAN-check at the end of this function.
    // If gamma_eff ~ 1.0 causes the flux to explode (NaN/Inf), the check will
    // catch it and fallback to the robust Supersonic (Full Upwind) Flux.
    // This allows the solver to "survive" bad reconstruction states without
    // artificially altering physics in valid low-gamma regions.
    // ------------------------------------------------------------------------

    // Minimal protection against strict Division-By-Zero
    if (std::abs(gamma_eff - 1.0) < 1e-6)
        gamma_eff = 1.000001;

    // Calculate Mach Number
    double M = un / c;
    // 2. Supersonic Branching (Optimization & Validity)
    // If we want F+ (sign>0) and flow is supersonic backward (M <= -1), F+ is 0.
    // If we want F- (sign<0) and flow is supersonic forward (M >= 1), F- is 0.

    if (sign > 0)
    {
        if (M >= 1.0)
            return get_flux(U, p, dir);
        if (M <= -1.0)
            return FluidVector(); // 返回全 0
    }
    else
    {
        if (M >= 1.0)
            return FluidVector(); // 返回全 0
        if (M <= -1.0)
            return get_flux(U, p, dir);
    }

    // 3. Subsonic Branching (|M| < 1) - The Vinokur Polynomials

    // Common factor term: (M +/- 1)
    // If sign > 0 (F+), we use (M + 1)
    // If sign < 0 (F-), we use (M - 1)
    double factor = (sign > 0) ? (M + 1.0) : (M - 1.0); // (M ± 1)

    // Mass Flux (The split mass term)
    // f_mass = +/- rho * c * (M +/- 1)^2 / 4
    double term_sign = (sign > 0) ? 1.0 : -1.0;

    // 4. Construct Split Flux Vector
    // --- Mass Equation ---
    double f_rho = term_sign * 0.25 * rho * c * factor * factor;

    // --- Momentum Equation ---
    // Vinokur Momentum Term using Equivalent Gamma
    // D +/- = [ (gamma_eff - 1)*u +/- 2*c ] / gamma_eff
    double term_2c = (sign > 0) ? (2.0 * c) : (-2.0 * c);
    double D_split = ((gamma_eff - 1.0) * un + term_2c) / gamma_eff;

    double f_un = f_rho * D_split;

    // Transverse momentum is passively advected with the mass flux
    double f_ut1 = f_rho * ut1;
    double f_ut2 = f_rho * ut2;

    // --- Energy Equation (Crucial Vinokur Correction) ---

    // Part A: Ideal Gas Energy Term (using gamma_eff)
    // H_ideal = D_split^2 * gamma_eff^2 / (2 * (gamma_eff^2 - 1))
    // Simplifies to the form below:
    double numerator = ((gamma_eff - 1.0) * un + term_2c);
    double E_ideal_term = (numerator * numerator) / (2.0 * (gamma_eff * gamma_eff - 1.0));

    // Part B: The Correction Term for General EOS
    // We need real specific enthalpy h = e + p/rho
    // Note: U.eng is rho * E_total -> specific internal energy e = (U.eng/rho) - 0.5*u*u
    double e_internal = (U.eng / rho) - 0.5 * (un * un + ut1 * ut1 + ut2 * ut2);
    double h_real = e_internal + p / rho;

    // Ideal enthalpy based on sound speed
    double h_ideal = (c * c) / (gamma_eff - 1.0);

    // Combine: F_energy = f_mass * ( E_ideal_term + (h_real - h_ideal) )
    double f_eng = f_rho * (E_ideal_term + (h_real - h_ideal) + 0.5 * (ut1 * ut1 + ut2 * ut2));

    if (std::isnan(f_eng))
    {
        return (sign > 0) ? get_flux(U, p, dir) : FluidVector();
    }

    return set_flux_vector(f_rho, f_un, f_ut1, f_ut2, f_eng, dir);
}

// ==================================================================
// 4. Roe-Glaister Flux Solver Helpers
// ==================================================================

// ------------------------------------------------------------------
// 4.1 Entropy Fix (Harten's)
// ------------------------------------------------------------------
/**
 * @brief Harten's Entropy Fix to prevent non-physical shocks (sonic glitch).
 * * Ensures eigenvalues never become exactly zero.
 */
inline double entropy_fix(double lambda, double epsilon)
{
    double abs_lambda = std::abs(lambda);
    if (abs_lambda < epsilon)
    {
        return (lambda * lambda + epsilon * epsilon) / (2.0 * epsilon);
    }
    return abs_lambda;
}

// ------------------------------------------------------------------
// 4.2 Roe-Glaister State Struct
// ------------------------------------------------------------------
/**
 * @struct RoeGlaisterState
 * @brief Holds the Roe-averaged quantities and thermodynamic derivatives.
 */
struct RoeGlaisterState
{
    double rho_hat;             // Roe-averaged density
    double u_hat, v_hat, w_hat; // Roe-averaged velocity
    double H_hat;               // Roe-averaged Total Enthalpy
    double c_hat;               // Roe-averaged Sound Speed

    // Glaister derivatives for General EOS
    double chi;   // dp/drho | constant e
    double kappa; // dp/de   | constant rho
};

// ------------------------------------------------------------------
// 4.3 Roe Averaging Routine (Connects to EOS)
// ------------------------------------------------------------------
/**
 * @brief Computes the Roe-Glaister average state.
 * * This function bridges the Flux Solver and the EOS.
 * * It uses the finite difference of pressure to approximate derivatives (Glaister).
 * * @param U_L, U_R  Conservative variables
 * @param P_L, P_R  Pressure
 * @param e_L, e_R  Specific internal energy (e = E_int / rho)
 * @param H_L, H_R  Total Enthalpy
 * @param Xi_avg    Averaged species mass fractions (passed to EOS)
 * @param eos       EOS object (must support get_pressure_from_rho_e, etc.)
 */
template <typename EosType>
inline RoeGlaisterState calc_glaister_state(
    const FluidVector &U_L, const FluidVector &U_R,
    double P_L, double P_R,
    double e_L, double e_R,
    double H_L, double H_R,
    const double *Xi_avg,
    const EosType &eos)
{
    RoeGlaisterState res;

    // --- A. Standard Roe Averages (Kinematics) ---
    double rho_L = std::max(U_L.rho, 1e-12);
    double rho_R = std::max(U_R.rho, 1e-12);

    double sq_rho_L = std::sqrt(rho_L);
    double sq_rho_R = std::sqrt(rho_R);
    double inv_denom = 1.0 / (sq_rho_L + sq_rho_R);

    // Store averages
    res.rho_hat = sq_rho_L * sq_rho_R;
    res.u_hat = (sq_rho_L * (U_L.mom_x / rho_L) + sq_rho_R * (U_R.mom_x / rho_R)) * inv_denom;
    res.v_hat = (sq_rho_L * (U_L.mom_y / rho_L) + sq_rho_R * (U_R.mom_y / rho_R)) * inv_denom;
    res.w_hat = (sq_rho_L * (U_L.mom_z / rho_L) + sq_rho_R * (U_R.mom_z / rho_R)) * inv_denom;
    res.H_hat = (sq_rho_L * H_L + sq_rho_R * H_R) * inv_denom;

    // --- B. Glaister Thermodynamic Averages (EOS Dependent) ---
    // We need to find chi (dp/drho) and kappa (dp/de) such that Property U is satisfied.

    double d_rho = rho_R - rho_L;
    double d_e = e_R - e_L;

    // Numerical threshold to switch to analytical derivatives
    double epsilon = std::max(1e-7 * (rho_L + rho_R), 1e-10);

    // 1. Calculate intermediate pressure p* = p(rho_R, e_L)
    // This utilizes the new EOS helper you added.
    double p_star = eos.get_pressure_from_rho_e(rho_R, e_L, Xi_avg);

    // 2. Compute Chi (dp/drho)
    if (std::abs(d_rho) > epsilon)
    {
        // Finite difference across rho
        res.chi = (p_star - P_L) / d_rho;
    }
    else
    {
        // Fallback to analytical derivative (at L state)
        res.chi = eos.get_dp_drho_e(rho_L, e_L, Xi_avg);
    }

    // 3. Compute Kappa (dp/de)
    if (std::abs(d_e) > 1e-10)
    { // Energy threshold can be smaller
        // Finite difference across e
        res.kappa = (P_R - p_star) / d_e;
    }
    else
    {
        // Fallback to analytical derivative (at R state)
        res.kappa = eos.get_dp_de_rho(rho_R, e_R, Xi_avg);
    }

    if (res.kappa < 1e-12)
        res.kappa = 1e-12;

    // --- C. Final Sound Speed ---
    // c^2 = chi + kappa * (H_hat - 0.5 * u_hat^2)
    double p_ref = 0.5 * (P_L + P_R);
    double term2 = (res.kappa * p_ref) / (res.rho_hat * res.rho_hat + 1e-20);
    double c2 = res.chi + term2;

    if (c2 < 0.0 || std::isnan(c2))
    {
        // 如果 c2 计算失败，回退到理想气体近似或极小声速
        // c2 = gamma * p / rho -> 1.4 * p_ref / rho_hat
        c2 = 1.4 * p_ref / (res.rho_hat + 1e-12);
    }
    res.c_hat = std::sqrt(std::max(c2, 1e-8)); // Safety floor

    return res;
}

// ------------------------------------------------------------------
// 4.4 Core Roe Flux Assembler
// ------------------------------------------------------------------
/**
 * @brief Assembles the final Roe Flux using the averaged state.
 * * F_Roe = 0.5 * (F_L + F_R) - 0.5 * Sum( alpha * |lambda| * K )
 */
inline FluidVector calc_roe_flux_hydro(
    const FluidVector &F_L, const FluidVector &F_R,
    const FluidVector &U_L, const FluidVector &U_R,
    double P_L, double P_R,
    const RoeGlaisterState &rs,
    double fix_coeff, int dir)
{
    // 1. Extract directional components
    double un_L = get_un(U_L, dir), ut1_L = get_ut1(U_L, dir), ut2_L = get_ut2(U_L, dir);
    double un_R = get_un(U_R, dir), ut1_R = get_ut1(U_R, dir), ut2_R = get_ut2(U_R, dir);

    double un_hat, ut1_hat, ut2_hat;
    if (dir == 0)
    {
        un_hat = rs.u_hat;
        ut1_hat = rs.v_hat;
        ut2_hat = rs.w_hat;
    }
    else if (dir == 1)
    {
        un_hat = rs.v_hat;
        ut1_hat = rs.w_hat;
        ut2_hat = rs.u_hat;
    }
    else
    {
        un_hat = rs.w_hat;
        ut1_hat = rs.u_hat;
        ut2_hat = rs.v_hat;
    }

    double d_rho = U_R.rho - U_L.rho;
    double d_p = P_R - P_L;
    double d_un = un_R - un_L;
    double d_ut1 = ut1_R - ut1_L;
    double d_ut2 = ut2_R - ut2_L;

    double rho_c = rs.rho_hat * rs.c_hat;
    double c2_safe = std::max(rs.c_hat * rs.c_hat, 1e-16);

    // Wave amplitudes
    // alpha_1: u - c
    // alpha_2: u (Entropy)
    // alpha_3: u + c
    double alpha_1 = (d_p - rho_c * d_un) / (2.0 * c2_safe);
    double alpha_2 = d_rho - (d_p / c2_safe);
    double alpha_3 = (d_p + rho_c * d_un) / (2.0 * c2_safe);
    double alpha_4 = rs.rho_hat * d_ut1; // Transverse wave 1
    double alpha_5 = rs.rho_hat * d_ut2; // Transverse wave 2

    // 2. Eigenvalues (Lambda) with Entropy Fix
    double spectral_radius = std::abs(rs.u_hat) + rs.c_hat;
    double epsilon_val = fix_coeff * spectral_radius;
    epsilon_val = std::max(epsilon_val, 1e-12);

    // 3. Eigenvalues
    double l1 = entropy_fix(un_hat - rs.c_hat, epsilon_val);
    double l2 = entropy_fix(un_hat, epsilon_val);
    double l3 = entropy_fix(un_hat + rs.c_hat, epsilon_val);

    // 4. Assemble Dissipation Term Component by Component
    double diss_rho = l1 * alpha_1 + l2 * alpha_2 + l3 * alpha_3;

    double diss_un = l1 * alpha_1 * (un_hat - rs.c_hat) + l2 * alpha_2 * un_hat + l3 * alpha_3 * (un_hat + rs.c_hat);

    // Transverse momentum dissipation is coupled with mass dissipation
    double diss_ut1 = diss_rho * ut1_hat + l2 * alpha_4;
    double diss_ut2 = diss_rho * ut2_hat + l2 * alpha_5;

    // Energy Dissipation
    double K1_eng = rs.H_hat - un_hat * rs.c_hat;
    double K3_eng = rs.H_hat + un_hat * rs.c_hat;

    double K2_eng;
    if (rs.kappa < 1e-8)
    {
        K2_eng = 0.5 * (rs.u_hat * rs.u_hat + rs.v_hat * rs.v_hat + rs.w_hat * rs.w_hat);
    }
    else
    {
        double term_singular = (rs.rho_hat * c2_safe) / rs.kappa;
        K2_eng = (std::abs(term_singular) > 100.0 * (std::abs(rs.H_hat) + 1.0))
                     ? 0.5 * (rs.u_hat * rs.u_hat + rs.v_hat * rs.v_hat + rs.w_hat * rs.w_hat)
                     : rs.H_hat - term_singular;
    }

    double diss_eng = l1 * alpha_1 * K1_eng + l2 * alpha_2 * K2_eng + l3 * alpha_3 * K3_eng + l2 * (alpha_4 * ut1_hat + alpha_5 * ut2_hat);

    FluidVector diss = set_flux_vector(diss_rho, diss_un, diss_ut1, diss_ut2, diss_eng, dir);

    return 0.5 * (F_L + F_R - diss);
}

// ==================================================================
// 5. HLL Flux Solver Helpers
// ==================================================================

// ------------------------------------------------------------------
// 5.1 Sound speed Calculation
// ------------------------------------------------------------------
/**
/**
 * @brief Computes sound speed for a single state based on Note Step 2.
 * c^2 = chi + (p / rho^2) * kappa
 */
template <typename EosType>
inline double calc_sound_speed_thermo(
    double rho, double p, double e, const double *Xi,
    const EosType &eos)
{
    // 防止除零
    if (rho < 1e-12)
        return 0.0;

    // 1. 获取热力学导数
    // chi = dp/drho | e
    double chi = eos.get_dp_drho_e(rho, e, Xi);
    // kappa = dp/de | rho
    double kappa = eos.get_dp_de_rho(rho, e, Xi);

    // 2. 根据笔记公式计算 c^2
    double term2 = (kappa * p) / (rho * rho);
    double c2 = chi + term2;

    // 3. 安全保护
    if (c2 < 0.0 || std::isnan(c2))
    {
        // 回退策略：假设 Gamma=1.4
        return std::sqrt(1.4 * p / rho);
    }
    return std::sqrt(c2);
}

// ------------------------------------------------------------------
// 5.2 Einfeldt Speed Estimate
// ------------------------------------------------------------------
/**
/**
 * @brief Estimates HLL wave speeds S_L and S_R.
 * Ref: Note Step 2 (Roe Average based Einfeldt speeds)
 */
inline void calc_hll_wave_speeds(
    double un_L, double c_L,
    double un_R, double c_R,
    const RoeGlaisterState &rs, int dir, // <--- 复用你的 Roe 状态
    double &S_L, double &S_R)
{
    // 笔记公式:
    // S_L = min(u_L - c_L, u_hat - c_hat)
    // S_R = max(u_R + c_R, u_hat + c_hat)

    double un_hat = (dir == 0) ? rs.u_hat : ((dir == 1) ? rs.v_hat : rs.w_hat);

    S_L = std::min(un_L - c_L, un_hat - rs.c_hat);
    S_R = std::max(un_R + c_R, un_hat + rs.c_hat);
}

// ------------------------------------------------------------------
// 5.3 HLL Flux Calculation
// ------------------------------------------------------------------
/**
 * @brief Computes the HLL Flux.
 * Ref: Note Step 3 (a, b, c branches)
 */
inline FluidVector calc_hll_flux_hydro(
    const FluidVector &F_L, const FluidVector &F_R,
    const FluidVector &U_L, const FluidVector &U_R,
    double S_L, double S_R)
{
    if (S_L >= 0.0)
        return F_L;
    if (S_R <= 0.0)
        return F_R;

    double inv_delta_S = 1.0 / (S_R - S_L);
    return (F_L * S_R - F_R * S_L + (U_R - U_L) * (S_L * S_R)) * inv_delta_S;
}

// ------------------------------------------------------------------
// 5.4 HLLC Star Speed Helper (新增，为HLLC做准备)
// ------------------------------------------------------------------
/**
 * @brief Computes the contact wave speed S_star for HLLC.
 * Formula:
 * S_* = (rho_R*u_R*(S_R - u_R) - rho_L*u_L*(S_L - u_L) + (p_L - p_R))
 * -------------------------------------------------------------
 * (rho_R*(S_R - u_R) - rho_L*(S_L - u_L))
 */
inline double calc_hllc_star_speed(
    double un_L, double rho_L, double p_L, double S_L,
    double un_R, double rho_R, double p_R, double S_R)
{
    double term_L = rho_L * (S_L - un_L);
    double term_R = rho_R * (S_R - un_R);
    double denom = term_R - term_L;

    if (std::abs(denom) < 1e-10)
        return 0.5 * (un_L + un_R);
    return (term_R * un_R - term_L * un_L + (p_L - p_R)) / denom;
}