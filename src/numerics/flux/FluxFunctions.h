/**
 * @file FluxFunctions.h
 * @brief Shared flux mathematics for the Euler equations.
 *
 * This file provides stateless kernels shared by finite-volume flux policies:
 * 1. Physical flux F(U) for a conservative state.
 * 2. Steger-Warming flux-vector splitting.
 * 3. Vinokur-Van Leer flux-vector splitting.
 */

#pragma once

#include <cmath>
#include <vector>

#include "../../data/FluidState.h"

// Direction map: dir=0, 1, and 2 select x, y, and z. Flux formulas below use
// local normal and tangential components and map the result back afterward.

/// Return the velocity normal to the selected coordinate direction.
ARCH_INLINE double get_un(const FluidVector &U, int dir)
{
    if (dir == 0)
        return U.mom_u / U.rho;
    if (dir == 1)
        return U.mom_v / U.rho;
    return U.mom_w / U.rho;
}

/// Return the first tangential velocity for the selected normal direction.
ARCH_INLINE double get_ut1(const FluidVector &U, int dir)
{
    if (dir == 0)
        return U.mom_v / U.rho;
    if (dir == 1)
        return U.mom_w / U.rho;
    return U.mom_u / U.rho;
}

/// Return the second tangential velocity for the selected normal direction.
ARCH_INLINE double get_ut2(const FluidVector &U, int dir)
{
    if (dir == 0)
        return U.mom_w / U.rho;
    if (dir == 1)
        return U.mom_u / U.rho;
    return U.mom_v / U.rho;
}

/// Map normal and tangential flux components back to a global FluidVector.
ARCH_INLINE FluidVector set_flux_vector(double f_rho, double f_un, double f_ut1, double f_ut2, double f_eng, int dir)
{
    FluidVector F;
    F.rho = f_rho;
    F.eng = f_eng;
    if (dir == 0)
    {
        F.mom_u = f_un;
        F.mom_v = f_ut1;
        F.mom_w = f_ut2;
    }
    else if (dir == 1)
    {
        F.mom_v = f_un;
        F.mom_w = f_ut1;
        F.mom_u = f_ut2;
    }
    else
    {
        F.mom_w = f_un;
        F.mom_u = f_ut1;
        F.mom_v = f_ut2;
    }
    return F;
}

// Physical flux.

/**
 * @brief Compute the Euler physical flux F(U) in one coordinate direction.
 *
 * In local normal coordinates:
 * F = [rho*u, rho*u^2+p, rho*u*ut1, rho*u*ut2, (E+p)*u].
 *
 * @tparam EosType Equation-of-state policy.
 * @param U Conservative state (rho, rho*u, rho*v, rho*w, E).
 * @param Xi Species mass fractions.
 * @param eos EOS object used to recover pressure from the conservative state.
 * @param dir Coordinate index normal to the flux face.
 * @return Conservative flux in global coordinates.
 */
template <typename EosType>
ARCH_INLINE FluidVector get_flux(const FluidVector &U, const double *Xi, const EosType &eos, int dir)
{
    double rho = U.rho;
    if (rho < 1e-12)
        return FluidVector(); // 1e-12 is the flux-layer near-vacuum density threshold.
    double un = get_un(U, dir);
    double ut1 = get_ut1(U, dir);
    double ut2 = get_ut2(U, dir);

    // Recover pressure through the selected EOS; the flux layer makes no
    // ideal-gas assumption.
    double p = eos.get_pressure(U, Xi);

    double f_rho = rho * un;
    double f_un = rho * un * un + p;
    double f_ut1 = rho * un * ut1;
    double f_ut2 = rho * un * ut2;
    double f_eng = (U.eng + p) * un;
    return set_flux_vector(f_rho, f_un, f_ut1, f_ut2, f_eng, dir);
}

// Use this overload when wave-speed estimation has already recovered pressure,
// avoiding a duplicate EOS evaluation.
ARCH_INLINE FluidVector get_flux(const FluidVector &U, double p, int dir)
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

// Steger-Warming flux-vector splitting.

/**
 * @brief Split the flux according to the signs of u, u+c, and u-c.
 * @param U Conservative state.
 * @param Xi Species mass fractions.
 * @param eos Equation-of-state object.
 * @param sign Split direction: +1 selects F+ and -1 selects F-.
 * @param smoothing_coeff Entropy-fix coefficient around zero eigenvalues.
 * @param dir Flux-face normal direction.
 * @return Flux component with the requested sign.
 */
template <typename EosType>
ARCH_INLINE FluidVector calc_split_flux(const FluidVector &U, const double *Xi,
                            const EosType &eos, int sign, double smoothing_coeff, int dir)
{
    double rho = std::max(U.rho, 1e-12); // Match the physical-flux near-vacuum floor.
    double un = get_un(U, dir);
    double ut1 = get_ut1(U, dir);
    double ut2 = get_ut2(U, dir);
    double V2 = un * un + ut1 * ut1 + ut2 * ut2; // Three-dimensional |v|^2.

    double p = eos.get_pressure(U, Xi);
    double c = eos.get_sound_speed(U, p, Xi);
    double H = (U.eng + p) / rho;
    double gamma = eos.get_gamma(Xi);

    // eps scales the entropy-fix width with local sound speed. The 1e-12 floor
    // keeps its denominator nonzero in stationary or low-sound-speed states.
    double eps = std::max(smoothing_coeff * c, 1e-12);

    auto split_lambda = [&](double l)
    {
        double l_abs_smoothed = (std::abs(l) < eps) ? (l * l + eps * eps) / (2.0 * eps) : std::abs(l);
        return (sign > 0) ? 0.5 * (l + l_abs_smoothed) : 0.5 * (l - l_abs_smoothed);
    };

    // Split eigenvalues for the entropy wave and two acoustic waves.
    double l1 = split_lambda(un);
    double l2 = split_lambda(un + c);
    double l3 = split_lambda(un - c);

    // Steger-Warming weights derived from the homogeneity of the Euler system.
    double f1 = (gamma - 1.0) / gamma;
    double f2 = 0.5 / gamma;

    double w1 = f1 * l1;
    double w2 = f2 * l2;
    double w3 = f2 * l3;
    double w_sum = w1 + w2 + w3;

    // Mass flux.
    double f_rho = w_sum * rho;

    // Normal momentum contains the acoustic waves; tangential momentum is
    // passively advected with mass flux.
    double f_un = w1 * (rho * un) + w2 * (rho * (un + c)) + w3 * (rho * (un - c));
    double f_ut1 = f_rho * ut1;
    double f_ut2 = f_rho * ut2;

    // w1 represents the entropy wave at u; w2 and w3 represent the acoustic
    // waves at u+c and u-c.
    double f_eng = w1 * (0.5 * rho * V2) + w2 * rho * (H + un * c) + w3 * rho * (H - un * c);

    return set_flux_vector(f_rho, f_un, f_ut1, f_ut2, f_eng, dir);
}

// Vinokur-Van Leer flux-vector splitting for a general EOS.

/**
 * @brief Construct a general-EOS split flux through an effective heat-capacity ratio.
 * @param U Conservative state.
 * @param Xi Species mass fractions.
 * @param eos Equation-of-state object.
 * @param sign Positive values compute F+; negative values compute F-.
 * @param dir Flux-face normal direction.
 */
template <typename EosType>
ARCH_INLINE FluidVector calc_vinokur_flux(const FluidVector &U, const double *Xi,
                              const EosType &eos, int sign, int dir)
{
    // Precompute local velocity. The 1e-12 density floor matches the other
    // flux kernels' near-vacuum contract.
    double rho = std::max(U.rho, 1e-12);
    double un = get_un(U, dir);
    double ut1 = get_ut1(U, dir);
    double ut2 = get_ut2(U, dir);

    // Thermodynamic values come from the EOS, which must accept states already
    // protected by the density floor.
    double p = eos.get_pressure(U, Xi);
    double c = eos.get_sound_speed(U, p, Xi);

    // Define the Vinokur effective ratio through c^2=gamma_eff*p/rho. If the
    // EOS sound speed is invalid or below 1e-8, gamma=1.4 supplies a finite
    // ideal-gas fallback rather than a physical claim about the material.
    if (std::isnan(c) || c < 1e-8)
        c = std::sqrt(1.4 * p / rho);
    double gamma_eff = (p > 1e-12) ? (rho * c * c / p) : 1.4;

    // The Vinokur energy flux contains denominators gamma_eff-1 and
    // gamma_eff^2-1, which become singular near one. A broad empirical clamp
    // would corrupt valid low-gamma materials, so only values within 1e-6 of
    // one move to 1+1e-6 to prevent exact division by zero. A remaining NaN
    // triggers the full-upwind fallback at the end of the function.
    if (std::abs(gamma_eff - 1.0) < 1e-6)
        gamma_eff = 1.000001;

    // Normal Mach number.
    double M = un / c;
    // Supersonic information travels in one direction: F+ vanishes for M<=-1,
    // while F- vanishes for M>=1.

    if (sign > 0)
    {
        if (M >= 1.0)
            return get_flux(U, p, dir);
        if (M <= -1.0)
            return FluidVector(); // No positive-going characteristic contribution.
    }
    else
    {
        if (M >= 1.0)
            return FluidVector(); // No negative-going characteristic contribution.
        if (M <= -1.0)
            return get_flux(U, p, dir);
    }

    // In the subsonic region |M|<1, the Vinokur polynomial uses M+1 for F+
    // and M-1 for F-.
    double factor = (sign > 0) ? (M + 1.0) : (M - 1.0); // (M ± 1)

    // Split mass flux: f_mass=±rho*c*(M±1)^2/4.
    double term_sign = (sign > 0) ? 1.0 : -1.0;

    // The factor 1/4 is part of the Vinokur subsonic splitting polynomial.
    double f_rho = term_sign * 0.25 * rho * c * factor * factor;

    // Momentum factor: D±=[(gamma_eff-1)u±2c]/gamma_eff.
    double term_2c = (sign > 0) ? (2.0 * c) : (-2.0 * c);
    double D_split = ((gamma_eff - 1.0) * un + term_2c) / gamma_eff;

    double f_un = f_rho * D_split;

    // Tangential momentum is passively advected with mass flux.
    double f_ut1 = f_rho * ut1;
    double f_ut2 = f_rho * ut2;

    // Ideal-gas part of the energy equation:
    // H_ideal=D_split^2*gamma_eff^2/[2(gamma_eff^2-1)].
    double numerator = ((gamma_eff - 1.0) * un + term_2c);
    double E_ideal_term = (numerator * numerator) / (2.0 * (gamma_eff * gamma_eff - 1.0));

    // The general-EOS correction uses the actual specific enthalpy h=e+p/rho,
    // where e=E/rho-|v|^2/2 and U.eng stores total-energy density.
    double e_internal = (U.eng / rho) - 0.5 * (un * un + ut1 * ut1 + ut2 * ut2);
    double h_real = e_internal + p / rho;

    // Ideal enthalpy expressed through sound speed and gamma_eff.
    double h_ideal = (c * c) / (gamma_eff - 1.0);

    // Combine ideal energy, the real-EOS enthalpy correction, and tangential
    // kinetic energy.
    double f_eng = f_rho * (E_ideal_term + (h_real - h_ideal) + 0.5 * (ut1 * ut1 + ut2 * ut2));

    if (std::isnan(f_eng))
    {
        return (sign > 0) ? get_flux(U, p, dir) : FluidVector();
    }

    return set_flux_vector(f_rho, f_un, f_ut1, f_ut2, f_eng, dir);
}

// Shared Roe-Glaister flux functions.
/**
 * @brief Apply Harten's entropy fix at zero-crossing eigenvalues.
 * For |lambda|<epsilon, a smooth parabola replaces the absolute value so the
 * function and its first derivative remain continuous.
 */
ARCH_INLINE double entropy_fix(double lambda, double epsilon)
{
    double abs_lambda = std::abs(lambda);
    if (abs_lambda < epsilon)
    {
        return (lambda * lambda + epsilon * epsilon) / (2.0 * epsilon);
    }
    return abs_lambda;
}

/**
 * @struct RoeGlaisterState
 * @brief Store Roe-averaged kinematics and Glaister thermodynamic derivatives.
 */
struct RoeGlaisterState
{
    double rho_hat;             // Roe-averaged density.
    double u_hat, v_hat, w_hat; // Roe-averaged velocity.
    double H_hat;               // Roe-averaged total enthalpy.
    double c_hat;               // Roe-averaged sound speed.

    // Glaister derivatives for a general EOS.
    double chi;   // (∂p/∂rho)_e.
    double kappa; // (∂p/∂e)_rho.
};

/**
 * @brief Compute the Roe-Glaister averaged state.
 *
 * Kinematic values use standard square-root-density weighting. Thermodynamic
 * derivatives use pressure differences between states and fall back to EOS
 * derivatives when the finite-difference interval is too small.
 * @param U_L Left conservative state.
 * @param U_R Right conservative state.
 * @param P_L Left pressure.
 * @param P_R Right pressure.
 * @param e_L Left specific internal energy.
 * @param e_R Right specific internal energy.
 * @param H_L Left total enthalpy.
 * @param H_R Right total enthalpy.
 * @param Xi_avg Averaged mass fractions passed to the EOS.
 * @param eos EOS object providing pressure inversion and both derivatives.
 */
template <typename EosType>
ARCH_INLINE RoeGlaisterState calc_glaister_state(
    const FluidVector &U_L, const FluidVector &U_R,
    double P_L, double P_R,
    double e_L, double e_R,
    double H_L, double H_R,
    const double *Xi_avg,
    const EosType &eos)
{
    RoeGlaisterState res;

    // Standard Roe kinematic average.
    double rho_L = std::max(U_L.rho, 1e-12);
    double rho_R = std::max(U_R.rho, 1e-12);

    double sq_rho_L = std::sqrt(rho_L);
    double sq_rho_R = std::sqrt(rho_R);
    double inv_denom = 1.0 / (sq_rho_L + sq_rho_R);

    // rho_hat=sqrt(rho_L*rho_R); velocity and total enthalpy use sqrt(rho)
    // weights.
    res.rho_hat = sq_rho_L * sq_rho_R;
    res.u_hat = (sq_rho_L * (U_L.mom_u / rho_L) + sq_rho_R * (U_R.mom_u / rho_R)) * inv_denom;
    res.v_hat = (sq_rho_L * (U_L.mom_v / rho_L) + sq_rho_R * (U_R.mom_v / rho_R)) * inv_denom;
    res.w_hat = (sq_rho_L * (U_L.mom_w / rho_L) + sq_rho_R * (U_R.mom_w / rho_R)) * inv_denom;
    res.H_hat = (sq_rho_L * H_L + sq_rho_R * H_R) * inv_denom;

    // Choose chi and kappa so the pressure difference matches the state jump.

    double d_rho = rho_R - rho_L;
    double d_e = e_R - e_L;

    // The density-difference threshold is 1e-7 times the density sum with a
    // 1e-10 absolute floor. Below it, subtraction amplifies roundoff, so the
    // implementation switches to an EOS derivative.
    double epsilon = std::max(1e-7 * (rho_L + rho_R), 1e-10);

    // p*=p(rho_R,e_L) decomposes the pressure jump into density and internal-
    // energy contributions.
    double p_star = eos.get_pressure_from_rho_e(rho_R, e_L, Xi_avg);

    // chi=(∂p/∂rho)_e.
    if (std::abs(d_rho) > epsilon)
    {
        // Finite difference across density.
        res.chi = (p_star - P_L) / d_rho;
    }
    else
    {
        // Analytic derivative at the left state.
        res.chi = eos.get_dp_drho_e(rho_L, e_L, Xi_avg);
    }

    // kappa=(∂p/∂e)_rho. The 1e-10 threshold protects subtraction of
    // nearly equal specific internal energies.
    if (std::abs(d_e) > 1e-10)
    { // Use a finite difference only when the energy interval is resolvable.
        res.kappa = (P_R - p_star) / d_e;
    }
    else
    {
        // Analytic derivative at the right state.
        res.kappa = eos.get_dp_de_rho(rho_R, e_R, Xi_avg);
    }

    if (res.kappa < 1e-12)
        res.kappa = 1e-12;

    // General-EOS sound speed: c^2=chi+kappa*p/rho^2.
    double p_ref = 0.5 * (P_L + P_R);
    double term2 = (res.kappa * p_ref) / (res.rho_hat * res.rho_hat + 1e-20);
    double c2 = res.chi + term2;

    if (c2 < 0.0 || std::isnan(c2))
    {
        // If the derivative combination is invalid, gamma=1.4 supplies a
        // finite ideal-gas fallback: c^2=gamma*p/rho.
        c2 = 1.4 * p_ref / (res.rho_hat + 1e-12);
    }
    res.c_hat = std::sqrt(std::max(c2, 1e-8)); // Keeps Roe amplitude denominators finite.

    return res;
}

/**
 * @brief Assemble the final flux from the Roe-averaged state.
 * F_Roe=0.5(F_L+F_R)-0.5*sum(alpha*|lambda|*K).
 */
ARCH_INLINE FluidVector calc_roe_flux_hydro(
    const FluidVector &F_L, const FluidVector &F_R,
    const FluidVector &U_L, const FluidVector &U_R,
    double P_L, double P_R,
    const RoeGlaisterState &rs,
    double fix_coeff, int dir)
{
    // Transform left, right, and averaged velocity into local normal coordinates.
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

    // Characteristic amplitudes: alpha_1 is u-c, alpha_2 is the entropy wave
    // at u, and alpha_3 is u+c.
    double alpha_1 = (d_p - rho_c * d_un) / (2.0 * c2_safe);
    double alpha_2 = d_rho - (d_p / c2_safe);
    double alpha_3 = (d_p + rho_c * d_un) / (2.0 * c2_safe);
    double alpha_4 = rs.rho_hat * d_ut1; // First tangential wave.
    double alpha_5 = rs.rho_hat * d_ut2; // Second tangential wave.

    // Scale the entropy-fix width by the local spectral radius. The 1e-12
    // floor remains nonzero in a stationary state.
    double spectral_radius = std::abs(rs.u_hat) + rs.c_hat;
    double epsilon_val = fix_coeff * spectral_radius;
    epsilon_val = std::max(epsilon_val, 1e-12);

    // Apply Harten's entropy fix to all three normal eigenvalues.
    double l1 = entropy_fix(un_hat - rs.c_hat, epsilon_val);
    double l2 = entropy_fix(un_hat, epsilon_val);
    double l3 = entropy_fix(un_hat + rs.c_hat, epsilon_val);

    // Accumulate the |A_hat|*Delta U dissipation by conservative component.
    double diss_rho = l1 * alpha_1 + l2 * alpha_2 + l3 * alpha_3;

    double diss_un = l1 * alpha_1 * (un_hat - rs.c_hat) + l2 * alpha_2 * un_hat + l3 * alpha_3 * (un_hat + rs.c_hat);

    // Tangential momentum contains advected mass dissipation and its independent wave.
    double diss_ut1 = diss_rho * ut1_hat + l2 * alpha_4;
    double diss_ut2 = diss_rho * ut2_hat + l2 * alpha_5;

    // When kappa<1e-8, the thermodynamic denominator lacks useful precision,
    // so the entropy-wave energy component falls back to kinetic energy.
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
        // A correction larger than 100 enthalpy scales is treated as nearly
        // singular so it cannot dominate energy dissipation.
        K2_eng = (std::abs(term_singular) > 100.0 * (std::abs(rs.H_hat) + 1.0))
                     ? 0.5 * (rs.u_hat * rs.u_hat + rs.v_hat * rs.v_hat + rs.w_hat * rs.w_hat)
                     : rs.H_hat - term_singular;
    }

    double diss_eng = l1 * alpha_1 * K1_eng + l2 * alpha_2 * K2_eng + l3 * alpha_3 * K3_eng + l2 * (alpha_4 * ut1_hat + alpha_5 * ut2_hat);

    FluidVector diss = set_flux_vector(diss_rho, diss_un, diss_ut1, diss_ut2, diss_eng, dir);

    return 0.5 * (F_L + F_R - diss);
}

// Shared HLL and HLLC flux functions.

/**
 * @brief Compute one-state sound speed from general-EOS derivatives.
 * c^2 = chi + (p / rho^2) * kappa
 */
template <typename EosType>
ARCH_INLINE double calc_sound_speed_thermo(
    double rho, double p, double e, const double *Xi,
    const EosType &eos)
{
    // 1e-12 matches the flux kernels' near-vacuum density threshold.
    if (rho < 1e-12)
        return 0.0;

    // Obtain derivatives at constant specific internal energy and density.
    // chi = dp/drho | e
    double chi = eos.get_dp_drho_e(rho, e, Xi);
    // kappa = dp/de | rho
    double kappa = eos.get_dp_de_rho(rho, e, Xi);

    // General-EOS sound-speed relation.
    double term2 = (kappa * p) / (rho * rho);
    double c2 = chi + term2;

    // If the derivative combination is negative or NaN, gamma=1.4 supplies a
    // finite ideal-gas fallback.
    if (c2 < 0.0 || std::isnan(c2))
    {
        return std::sqrt(1.4 * p / rho);
    }
    return std::sqrt(c2);
}

/**
 * @brief Estimate HLL wave speeds with Einfeldt bounds from both states and the Roe average.
 */
ARCH_INLINE void calc_hll_wave_speeds(
    double un_L, double c_L,
    double un_R, double c_R,
    const RoeGlaisterState &rs, int dir,
    double &S_L, double &S_R)
{
    // Einfeldt wave-speed bounds:
    // S_L = min(u_L - c_L, u_hat - c_hat)
    // S_R = max(u_R + c_R, u_hat + c_hat)

    double un_hat = (dir == 0) ? rs.u_hat : ((dir == 1) ? rs.v_hat : rs.w_hat);

    S_L = std::min(un_L - c_L, un_hat - rs.c_hat);
    S_R = std::max(un_R + c_R, un_hat + rs.c_hat);
}

/**
 * @brief Compute HLL flux according to the signs of S_L and S_R.
 * If the wave speeds straddle zero, use the two-wave intermediate state;
 * otherwise return the physical flux from the upwind side.
 */
ARCH_INLINE FluidVector calc_hll_flux_hydro(
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

/**
 * @brief Compute the HLLC contact speed S_star from Rankine-Hugoniot relations.
 * Formula:
 * S_* = (rho_R*u_R*(S_R - u_R) - rho_L*u_L*(S_L - u_L) + (p_L - p_R))
 * (rho_R*(S_R - u_R) - rho_L*(S_L - u_L))
 */
ARCH_INLINE double calc_hllc_star_speed(
    double un_L, double rho_L, double p_L, double S_L,
    double un_R, double rho_R, double p_R, double S_R)
{
    double term_L = rho_L * (S_L - un_L);
    double term_R = rho_R * (S_R - un_R);
    double denom = term_R - term_L;

    // 1e-10 is the absolute degeneracy threshold for the contact-speed
    // denominator; the arithmetic mean is the finite fallback.
    if (std::abs(denom) < 1e-10)
        return 0.5 * (un_L + un_R);
    return (term_R * un_R - term_L * un_L + (p_L - p_R)) / denom;
}
