/**
 * @file Reconstruction.h
 * @brief Spatial reconstruction schemes (Data -> Interface States).
 * * 职责：
 * 输入：网格上的守恒变量 (Stencil)
 * 输出：界面上的左右状态 (U_L, U_R)
 */

#pragma once

#include <utility>
#include <cmath>
#include <algorithm>

#include "Limiters.h"

#include "../../data/FluidState.h"

/**
 * @brief Helper to calculate slope ratio r and apply limiter.
 * * r_i = (q_i - q_{i-1}) / (q_{i+1} - q_i)
 * delta = phi(r) * (q_{i+1} - q_i)
 * * @tparam LimiterPolicy The limiter struct (MinMod, SuperBee, etc.)
 * @param q_m1 Value at i-1
 * @param q_0  Value at i
 * @param q_p1 Value at i+1
 * @return double The limited slope contribution: 0.5 * phi(r) * (q_p1 - q_0)
 */
template <typename LimiterPolicy>
inline double compute_limited_slope(double q_m1, double q_0, double q_p1)
{
    const double epsilon = 1e-12;
    // Forward difference (denominator)
    double del_plus = q_p1 - q_0;
    // Backward difference (numerator)
    double del_minus = q_0 - q_m1;

    // Avoid division by zero in flat regions
    if (std::abs(del_plus) < epsilon)
    {
        // If both are ~0, slope is 0. If del_minus is significant, r -> inf.
        // Most limiters return 0 or small value if r is huge/undefined,
        // usually we treat flat forward gradient as r=0 case or just return 0 slope.
        return 0.0;
    }

    double r = del_minus / del_plus;
    double phi = LimiterPolicy::calc(r);

    return 0.5 * phi * del_plus;
}

// =========================================================
// 1. PCM: Piecewise Constant Method
// =========================================================
/**
 * @struct PCMReconstruction
 * @brief Piecewise Constant Method (1st Order).
 * * Logic:
 * U_L(i+1/2) = U_i
 * U_R(i+1/2) = U_{i+1}
 * * No limiters, no gradients. Most diffusive but strictly monotonic.
 */
struct PCMReconstruction
{

    // 返回名字
    static std::string name() { return "PCM (1st Order)"; }

    static constexpr int NG = 1;
    /**
     * @brief Apply reconstruction.
     * Note: To maintain API compatibility with MUSCL/WENO, we accept
     * the full 4-point stencil (im1, i, ip1, ip2), but we only use (i, ip1).
     * The compiler will optimize away the unused arguments.
     */
    static std::pair<FluidVector3, FluidVector3> apply(
        const FluidVector3 &U_i,
        const FluidVector3 &U_ip1)
    {
        // 直接传递，不做任何修改
        return {U_i, U_ip1};
    }

    static std::pair<FluidVector3, FluidVector3> run(const FluidState &state, int i)
    {
        return apply(state.get(i), state.get(i + 1));
    }

    /**
     * @brief Apply Species reconstruction.
     */
    static void run_species(const FluidState &state, int i, int n_spec, double *Y_L, double *Y_R)
    {
        for (int k = 0; k < n_spec; ++k)
        {
            Y_L[k] = state.Y(k, i);
            Y_R[k] = state.Y(k, i + 1);
        }
    }
};

// =========================================================
// 2. MUSCL: Monotonic Upstream-Centered Scheme
// =========================================================
/**
 * @brief Performs MUSCL reconstruction for interface i+1/2.
 * * Reconstructs the state at the interface between cell i and cell i+1.
 * U_L (at i+1/2) comes from expanding cell i to its right edge.
 * U_R (at i+1/2) comes from expanding cell i+1 to its left edge.
 * * Stencil required: i-1, i, i+1, i+2.
 * * @tparam Limiter The limiter policy (e.g., MinMod, VanLeer).
 * @param state Global fluid state.
 * @param i Index of the cell to the left of the interface.
 * @return std::pair<FluidVector3, FluidVector3> {U_L, U_R}
 */

template <typename Limiter>
struct MusclReconstruction
{
    static std::string name() { return "MUSCL-" + Limiter::name(); }

    static constexpr int NG = 2;

    /**
     * @brief Reconstruct Conservative Variables.
     * Decoupled from FluidState for better unit testing and portability.
     */
    static std::pair<FluidVector3, FluidVector3>
    apply(
        const FluidVector3 &U_im1,
        const FluidVector3 &U_i,
        const FluidVector3 &U_ip1,
        const FluidVector3 &U_ip2)
    {
        FluidVector3 U_L, U_R;

        // --- Left State (at i+1/2) ---
        // Based on cell i, looking at i-1 and i+1
        U_L.rho = U_i.rho + compute_limited_slope<Limiter>(U_im1.rho, U_i.rho, U_ip1.rho);
        U_L.mom = U_i.mom + compute_limited_slope<Limiter>(U_im1.mom, U_i.mom, U_ip1.mom);
        U_L.eng = U_i.eng + compute_limited_slope<Limiter>(U_im1.eng, U_i.eng, U_ip1.eng);

        // --- Right State (at i+1/2) ---
        // Based on cell i+1, looking at i and i+2
        // Note: Minus sign because we project backwards
        U_R.rho = U_ip1.rho - compute_limited_slope<Limiter>(U_i.rho, U_ip1.rho, U_ip2.rho);
        U_R.mom = U_ip1.mom - compute_limited_slope<Limiter>(U_i.mom, U_ip1.mom, U_ip2.mom);
        U_R.eng = U_ip1.eng - compute_limited_slope<Limiter>(U_i.eng, U_ip1.eng, U_ip2.eng);

        return {U_L, U_R};
    }

    static std::pair<FluidVector3, FluidVector3> run(const FluidState &state, int i)
    {
        // 这里的 i 代表界面 i+1/2 左侧单元的索引
        // Stencil: i-1, i, i+1, i+2
        return apply(state.get(i - 1), state.get(i), state.get(i + 1), state.get(i + 2));
    }

    /**
     * @brief Reconstruct Species (Batch).
     * Pointers are used for efficiency since species count is dynamic.
     */
    static void run_species(const FluidState &state, int i, int n_spec, double *Y_L, double *Y_R)
    {
        int im1 = i - 1;
        int ip1 = i + 1;
        int ip2 = i + 2;
        for (int k = 0; k < n_spec; ++k)
        {
            double y_im1 = state.Y(k, im1);
            double y_i = state.Y(k, i);
            double y_ip1 = state.Y(k, ip1);
            double y_ip2 = state.Y(k, ip2);

            Y_L[k] = y_i + compute_limited_slope<Limiter>(y_im1, y_i, y_ip1);
            Y_R[k] = y_ip1 - compute_limited_slope<Limiter>(y_i, y_ip1, y_ip2);
        }
    }
};