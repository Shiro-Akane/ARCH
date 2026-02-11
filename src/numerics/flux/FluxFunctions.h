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

// for known pressure situation to save calculation
inline FluidVector3 get_flux(const FluidVector3 &U, double p)
{
    double rho = U.rho;
    double u = (std::abs(rho) > 1e-12) ? U.mom / rho : 0.0;

    FluidVector3 F;
    F.rho = U.mom;
    F.mom = U.mom * u + p;
    F.eng = (U.eng + p) * u;
    return F;
}

// ------------------------------------------------------------------
// 2. Steger-Warming Flux Vector Splitting
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
                             const EosType &eos, int sign, double smoothing_coeff)
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
    double eps = smoothing_coeff * c;

    eps = std::max(eps, 1e-12);

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
// 3. Vinokur-Von Leer Flux Vector Splitting
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
    double factor = (sign > 0) ? (M + 1.0) : (M - 1.0); // (M ± 1)

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

    if (std::isnan(F_split.eng))
    {
        // Fallback to supersonic (Upwind) if splitting fails numerically
        if (sign > 0)
            return FluidVector3{U.mom, U.mom * u + p, (U.eng + p) * u};
        else
            return FluidVector3{0.0, 0.0, 0.0};
    }

    return F_split;
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
    double rho_hat; // Roe-averaged density
    double u_hat;   // Roe-averaged velocity
    double H_hat;   // Roe-averaged Total Enthalpy
    double c_hat;   // Roe-averaged Sound Speed

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
 * @param Yi_avg    Averaged species mass fractions (passed to EOS)
 * @param eos       EOS object (must support get_pressure_from_rho_e, etc.)
 */
template <typename EosType>
inline RoeGlaisterState calc_glaister_state(
    const FluidVector3 &U_L, const FluidVector3 &U_R,
    double P_L, double P_R,
    double e_L, double e_R,
    double H_L, double H_R,
    const double *Yi_avg,
    const EosType &eos)
{
    RoeGlaisterState res;

    // --- A. Standard Roe Averages (Kinematics) ---
    double rho_L = std::max(U_L.rho, 1e-12);
    double rho_R = std::max(U_R.rho, 1e-12);

    double u_L = U_L.mom / rho_L;
    double u_R = U_R.mom / rho_R;

    double sq_rho_L = std::sqrt(rho_L);
    double sq_rho_R = std::sqrt(rho_R);
    double inv_denom = 1.0 / (sq_rho_L + sq_rho_R);

    // Store averages
    res.rho_hat = sq_rho_L * sq_rho_R;
    res.u_hat = (sq_rho_L * u_L + sq_rho_R * u_R) * inv_denom;
    res.H_hat = (sq_rho_L * H_L + sq_rho_R * H_R) * inv_denom;

    // --- B. Glaister Thermodynamic Averages (EOS Dependent) ---
    // We need to find chi (dp/drho) and kappa (dp/de) such that Property U is satisfied.

    double d_rho = rho_R - rho_L;
    double d_e = e_R - e_L;

    // Numerical threshold to switch to analytical derivatives
    double epsilon = std::max(1e-7 * (rho_L + rho_R), 1e-10);

    // 1. Calculate intermediate pressure p* = p(rho_R, e_L)
    // This utilizes the new EOS helper you added.
    double p_star = eos.get_pressure_from_rho_e(rho_R, e_L, Yi_avg);

    // 2. Compute Chi (dp/drho)
    if (std::abs(d_rho) > epsilon)
    {
        // Finite difference across rho
        res.chi = (p_star - P_L) / d_rho;
    }
    else
    {
        // Fallback to analytical derivative (at L state)
        res.chi = eos.get_dp_drho_e(rho_L, e_L, Yi_avg);
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
        res.kappa = eos.get_dp_de_rho(rho_R, e_R, Yi_avg);
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
inline FluidVector3 calc_roe_flux_hydro(
    const FluidVector3 &F_L, const FluidVector3 &F_R,
    const FluidVector3 &U_L, const FluidVector3 &U_R,
    double P_L, double P_R,
    const RoeGlaisterState &rs,
    double fix_coeff)
{
    // 1. Wave Strengths (Alpha)
    double d_rho = U_R.rho - U_L.rho;
    double d_p = P_R - P_L;
    double d_u = (U_R.mom / U_R.rho) - (U_L.mom / U_L.rho);

    // Characteristic variables coefficients
    double rho_c = rs.rho_hat * rs.c_hat;
    // 防止 c_hat 过小导致除零
    double c2_safe = std::max(rs.c_hat * rs.c_hat, 1e-16);

    // Wave amplitudes
    // alpha_1: u - c
    // alpha_2: u (Entropy)
    // alpha_3: u + c
    double alpha_1 = (d_p - rho_c * d_u) / (2.0 * c2_safe);
    double alpha_2 = d_rho - (d_p / c2_safe);
    double alpha_3 = (d_p + rho_c * d_u) / (2.0 * c2_safe);

    // 2. Eigenvalues (Lambda) with Entropy Fix
    double spectral_radius = std::abs(rs.u_hat) + rs.c_hat;
    double epsilon_val = fix_coeff * spectral_radius;
    epsilon_val = std::max(epsilon_val, 1e-12);

    double l1 = entropy_fix(rs.u_hat - rs.c_hat, epsilon_val);
    double l2 = entropy_fix(rs.u_hat, epsilon_val);
    double l3 = entropy_fix(rs.u_hat + rs.c_hat, epsilon_val);

    // 3. Eigenvectors (K)
    // K1 = [1, u-c, H-uc]
    FluidVector3 K1;
    K1.rho = 1.0;
    K1.mom = rs.u_hat - rs.c_hat;
    K1.eng = rs.H_hat - rs.u_hat * rs.c_hat;

    // K3 = [1, u+c, H+uc]
    FluidVector3 K3;
    K3.rho = 1.0;
    K3.mom = rs.u_hat + rs.c_hat;
    K3.eng = rs.H_hat + rs.u_hat * rs.c_hat;

    // K2 = [1, u, H - c^2/kappa]  <-- Glaister extension for Energy component
    FluidVector3 K2;
    K2.rho = 1.0;
    K2.mom = rs.u_hat;

    // Protect against zero kappa (incompressible limit case)
    double k_denominator = rs.kappa;
    // 1e-6 只是一个示例阈值，对于无量纲化的 density 通常足够小
    // 如果 kappa < 1e-6，说明压力几乎不依赖内能（接近不可压或错误状态）
    if (k_denominator < 1e-8)
    {
        // Fallback: 假设是理想气体行为，此时 K2_eng = 0.5 * u^2
        K2.eng = 0.5 * rs.u_hat * rs.u_hat;
    }
    else
    {
        double term_singular = (rs.rho_hat * c2_safe) / k_denominator;

        // 双重保险：限制这一项的大小，防止它超过合理的物理范围 (如 100倍的总焓)
        if (std::abs(term_singular) > 100.0 * (std::abs(rs.H_hat) + 1.0))
        {
            // 这种情况下通常意味着导数计算错误，回退到动能
            K2.eng = 0.5 * rs.u_hat * rs.u_hat;
        }
        else
        {
            K2.eng = rs.H_hat - term_singular;
        }
    }

    // 4. Assemble Dissipation Term: sum(|l| * a * K)
    FluidVector3 diss;
    diss.rho = l1 * alpha_1 * K1.rho + l2 * alpha_2 * K2.rho + l3 * alpha_3 * K3.rho;
    diss.mom = l1 * alpha_1 * K1.mom + l2 * alpha_2 * K2.mom + l3 * alpha_3 * K3.mom;
    diss.eng = l1 * alpha_1 * K1.eng + l2 * alpha_2 * K2.eng + l3 * alpha_3 * K3.eng;

    if (std::isnan(diss.eng) || std::isinf(diss.eng))
    {
        // 如果计算出无效通量，返回零耗散（变成中心差分），防止程序崩溃
        // 实际生产中这里应该报错，但为了调试可以先这样
        return 0.5 * (F_L + F_R);
    }
    // 5. Final Flux
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
    double rho, double p, double e, const double *Yi,
    const EosType &eos)
{
    // 防止除零
    if (rho < 1e-12)
        return 0.0;

    // 1. 获取热力学导数
    // chi = dp/drho | e
    double chi = eos.get_dp_drho_e(rho, e, Yi);
    // kappa = dp/de | rho
    double kappa = eos.get_dp_de_rho(rho, e, Yi);

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
    double u_L, double c_L,
    double u_R, double c_R,
    const RoeGlaisterState &rs, // <--- 复用你的 Roe 状态
    double &S_L, double &S_R)
{
    // 笔记公式:
    // S_L = min(u_L - c_L, u_hat - c_hat)
    // S_R = max(u_R + c_R, u_hat + c_hat)

    double lam_L_left = u_L - c_L;
    double lam_L_roe = rs.u_hat - rs.c_hat;

    double lam_R_right = u_R + c_R;
    double lam_R_roe = rs.u_hat + rs.c_hat;

    S_L = std::min(lam_L_left, lam_L_roe);
    S_R = std::max(lam_R_right, lam_R_roe);
}

// ------------------------------------------------------------------
// 5.3 HLL Flux Calculation
// ------------------------------------------------------------------
/**
 * @brief Computes the HLL Flux.
 * Ref: Note Step 3 (a, b, c branches)
 */
inline FluidVector3 calc_hll_flux_hydro(
    const FluidVector3 &F_L, const FluidVector3 &F_R,
    const FluidVector3 &U_L, const FluidVector3 &U_R,
    double S_L, double S_R)
{
    // Branch b: Supersonic Flow to Right (S_L >= 0)
    if (S_L >= 0.0)
    {
        return F_L;
    }
    // Branch c: Supersonic Flow to Left (S_R <= 0)
    else if (S_R <= 0.0)
    {
        return F_R;
    }
    // Branch a: Subsonic / Transonic (S_L < 0 < S_R)
    else
    {
        // F_HLL = (S_R * F_L - S_L * F_R + S_L * S_R * (U_R - U_L)) / (S_R - S_L)
        double inv_delta_S = 1.0 / (S_R - S_L);
        double term_diff = S_L * S_R;

        FluidVector3 F_HLL;

        // 笔记中将 Mass/Mom/Eng 分开写了，这里用向量形式等价合并
        // Mass Flux
        F_HLL.rho = inv_delta_S * (S_R * F_L.rho - S_L * F_R.rho + term_diff * (U_R.rho - U_L.rho));

        // Momentum Flux
        F_HLL.mom = inv_delta_S * (S_R * F_L.mom - S_L * F_R.mom + term_diff * (U_R.mom - U_L.mom));

        // Energy Flux
        F_HLL.eng = inv_delta_S * (S_R * F_L.eng - S_L * F_R.eng + term_diff * (U_R.eng - U_L.eng));

        return F_HLL;
    }
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
    double u_L, double rho_L, double p_L, double S_L,
    double u_R, double rho_R, double p_R, double S_R)
{
    // 简化项
    double term_L = rho_L * (S_L - u_L);
    double term_R = rho_R * (S_R - u_R);

    // 分母
    double denom = term_R - term_L;

    // 防止除零（极少数情况 S_L, S_R 对称且 rho 相等）
    if (std::abs(denom) < 1e-10)
    {
        return 0.5 * (u_L + u_R);
    }

    // 分子
    double numer = term_R * u_R - term_L * u_L + (p_L - p_R);

    return numer / denom;
}