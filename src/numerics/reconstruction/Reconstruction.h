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
    static std::pair<FluidVector, FluidVector> apply(
        const FluidVector &U_i,
        const FluidVector &U_ip1)
    {
        // 直接传递，不做任何修改
        return {U_i, U_ip1};
    }

    static std::pair<FluidVector, FluidVector> run(const FluidState &state, int i, int stride = 1)
    {
        return apply(state.get(i), state.get(i + stride));
    }

    /**
     * @brief Apply Species reconstruction.
     */
    static void run_species(const FluidState &state, int i, int n_spec, double *Y_L, double *Y_R, int stride = 1)
    {
        for (int k = 0; k < n_spec; ++k)
        {
            Y_L[k] = state.Y(k, i);
            Y_R[k] = state.Y(k, i + stride);
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
    static std::pair<FluidVector, FluidVector>
    apply(
        const FluidVector &U_im1,
        const FluidVector &U_i,
        const FluidVector &U_ip1,
        const FluidVector &U_ip2)
    {
        FluidVector U_L, U_R;

        // --- Left State (at i+1/2) ---
        // Based on cell i, looking at i-1 and i+1
        U_L.rho = U_i.rho + compute_limited_slope<Limiter>(U_im1.rho, U_i.rho, U_ip1.rho);
        U_L.mom_x = U_i.mom_x + compute_limited_slope<Limiter>(U_im1.mom_x, U_i.mom_x, U_ip1.mom_x);
        U_L.mom_y = U_i.mom_y + compute_limited_slope<Limiter>(U_im1.mom_y, U_i.mom_y, U_ip1.mom_y);
        U_L.mom_z = U_i.mom_z + compute_limited_slope<Limiter>(U_im1.mom_z, U_i.mom_z, U_ip1.mom_z);
        U_L.eng = U_i.eng + compute_limited_slope<Limiter>(U_im1.eng, U_i.eng, U_ip1.eng);

        // --- Right State (at i+1/2) ---
        // Based on cell i+1, looking at i and i+2
        // Note: Minus sign because we project backwards
        U_R.rho = U_ip1.rho - compute_limited_slope<Limiter>(U_i.rho, U_ip1.rho, U_ip2.rho);
        U_R.mom_x = U_ip1.mom_x - compute_limited_slope<Limiter>(U_i.mom_x, U_ip1.mom_x, U_ip2.mom_x);
        U_R.mom_y = U_ip1.mom_y - compute_limited_slope<Limiter>(U_i.mom_y, U_ip1.mom_y, U_ip2.mom_y);
        U_R.mom_z = U_ip1.mom_z - compute_limited_slope<Limiter>(U_i.mom_z, U_ip1.mom_z, U_ip2.mom_z);
        U_R.eng = U_ip1.eng - compute_limited_slope<Limiter>(U_i.eng, U_ip1.eng, U_ip2.eng);

        return {U_L, U_R};
    }

    static std::pair<FluidVector, FluidVector> run(const FluidState &state, int i, int stride = 1)
    {
        // 这里的 i 代表界面 i+1/2 左侧单元的索引
        // Stencil: i-1, i, i+1, i+2
        return apply(state.get(i - stride), state.get(i), state.get(i + stride), state.get(i + 2 * stride));
    }

    /**
     * @brief Reconstruct Species (Batch).
     * Pointers are used for efficiency since species count is dynamic.
     */
    static void run_species(const FluidState &state, int i, int n_spec, double *Y_L, double *Y_R, int stride = 1)
    {
        int im1 = i - stride;
        int ip1 = i + stride;
        int ip2 = i + 2 * stride;
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

/**
 * @struct PPMReconstruction
 * @brief Third-Order Piecewise Parabolic Method (PPM).
 * * 职责：纯粹的数学插值器。
 * 输入：6点模板 (Stencil)
 * 输出：界面上的左右状态
 * 特点：完全解耦 EOS 和物理含义，仅对输入数据做高阶插值和限制。
 */
struct PPMReconstruction
{
    static std::string name() { return "PPM (3rd Order)"; }

    // PPM 需要 i-2 到 i+3 的 Stencil
    static constexpr int NG = 3;

private:
    // =========================================================
    // Pure Math Kernel
    // =========================================================

    // 4th-order interpolation for interface value
    // estimate u_{i+1/2} using u_{i-1}, u_i, u_{i+1}, u_{i+2}
    static inline double interpolate_face_4th(double u_im1, double u_i, double u_ip1, double u_ip2)
    {
        // Eq: (7/12)(u_i + u_ip1) - (1/12)(u_im1 + u_ip2)
        return (7.0 / 12.0) * (u_i + u_ip1) - (1.0 / 12.0) * (u_im1 + u_ip2);
    }
    // =========================================================
    // Colella-Woodward Limiter
    // Modifies u_L (left face of cell) and u_R (right face of cell) based on cell average u_avg
    // =========================================================
    static inline void apply_cw_limiter(double &u_L, double &u_R, double u_avg)
    {
        double dq_L = u_L - u_avg;
        double dq_R = u_R - u_avg;
        double dq = dq_R - dq_L; // 整个单元的变化量 (u_R - u_L)
        double product = dq_L * dq_R;

        // 1. 极值拉平 (Flatten extrema)
        // 如果 u_L 和 u_R 在 u_avg 的同侧，说明均值是极值，或者出现了非单调
        if (product > 0.0)
        {
            u_L = u_avg;
            u_R = u_avg;
            return;
        }

        // 2. 抛物线单调性修正 (Monotonicity constraint)
        // 这一步对应笔记中的 "B: 左侧/右侧过于陡峭"
        // 原始公式：检查抛物线极值是否在单元内。
        // 条件：|dq_L| > 2|dq_R| 或 |dq_R| > 2|dq_L| 其实是简化版。

        if (std::abs(dq_L) >= 2.0 * std::abs(dq_R))
            u_L = u_avg - 2.0 * dq_R;
        else if (std::abs(dq_R) >= 2.0 * std::abs(dq_L))
            u_R = u_avg - 2.0 * dq_L;
    }

    /**
     * @brief 核心标量重构算法
     * * 计算界面 i+1/2 处的左状态 (u_L) 和右状态 (u_R)。
     * * 逻辑：
     * 1. 界面 i+1/2 的 u_L 来自 Cell i 的右边界。需要构建 Cell i 的抛物线。
     * 2. 界面 i+1/2 的 u_R 来自 Cell i+1 的左边界。需要构建 Cell i+1 的抛物线。
     * * @param v 数组包含 [i-2, i-1, i, i+1, i+2, i+3]
     * @return std::pair<double, double> {u_L(i+1/2), u_R(i+1/2)}
     */
    // 核心标量重构
    static std::pair<double, double> reconstruct_scalar_ppm(const double v[6])
    {
        // 1. 插值得到原始界面值
        double u_face_imhalf = interpolate_face_4th(v[0], v[1], v[2], v[3]);
        double u_face_iphalf = interpolate_face_4th(v[1], v[2], v[3], v[4]);
        double u_face_ip3half = interpolate_face_4th(v[2], v[3], v[4], v[5]);

        // 2. 构造 Cell i (v[2])
        double u_L_cell_i = u_face_imhalf;
        double u_R_cell_i = u_face_iphalf;
        apply_cw_limiter(u_L_cell_i, u_R_cell_i, v[2]); // 限制 Cell i

        // 3. 构造 Cell i+1 (v[3])
        double u_L_cell_ip1 = u_face_iphalf;
        double u_R_cell_ip1 = u_face_ip3half;
        apply_cw_limiter(u_L_cell_ip1, u_R_cell_ip1, v[3]); // 限制 Cell i+1

        // 返回界面 i+1/2 处的值：
        // 左状态来自 Cell i 的右边界
        // 右状态来自 Cell i+1 的左边界
        return {u_R_cell_i, u_L_cell_ip1};
    }

public:
    /**
     * @brief Apply PPM to FluidVectors (Component-wise).
     * 接收完整 Stencil，逐个变量进行纯数学重构。
     */
    static std::pair<FluidVector, FluidVector> apply(
        const FluidVector &U_im2, const FluidVector &U_im1,
        const FluidVector &U_i,
        const FluidVector &U_ip1, const FluidVector &U_ip2, const FluidVector &U_ip3)
    {
        double r[6], u[6], v[6], w[6], e[6]; // e here is epsilon (internal energy per unit mass)
        const FluidVector *U_stencil[6] = {&U_im2, &U_im1, &U_i, &U_ip1, &U_ip2, &U_ip3};

        for (int k = 0; k < 6; ++k)
        {
            double rho = std::max(1e-13, U_stencil[k]->rho);

            double vel_u = U_stencil[k]->mom_x / rho;
            double vel_v = U_stencil[k]->mom_y / rho;
            double vel_w = U_stencil[k]->mom_z / rho;

            double kin = 0.5 * (vel_u * vel_u + vel_v * vel_v + vel_w * vel_w);
            double specific_total = U_stencil[k]->eng / rho;

            // 计算比内能 epsilon = E_total/rho - 0.5*u^2
            double eps = std::max(1e-13, specific_total - kin);

            // 强保护：防止原始数据本身就有问题
            eps = std::max(1e-13, eps);

            r[k] = rho;
            u[k] = vel_u;
            v[k] = vel_v;
            w[k] = vel_w;
            e[k] = eps;
        }

        // --- Step 2: 纯数学重构 ---
        auto res_rho = reconstruct_scalar_ppm(r);
        auto res_u = reconstruct_scalar_ppm(u);
        auto res_v = reconstruct_scalar_ppm(v);
        auto res_w = reconstruct_scalar_ppm(w);
        auto res_eps = reconstruct_scalar_ppm(e);

        // --- Step 3: 物理限制 (Positivity) ---
        double rho_L = std::max(1e-13, res_rho.first);
        double rho_R = std::max(1e-13, res_rho.second);

        double eps_L = std::max(1e-13, res_eps.first);
        double eps_R = std::max(1e-13, res_eps.second);

        double u_L = res_u.first, v_L = res_v.first, w_L = res_w.first;
        double u_R = res_u.second, v_R = res_v.second, w_R = res_w.second;

        // --- Step 4: 转回守恒变量 (Re-assembly) ---
        FluidVector UL, UR;

        // Left State
        UL.rho = rho_L;
        UL.mom_x = rho_L * u_L;
        UL.mom_y = rho_L * v_L;
        UL.mom_z = rho_L * w_L;
        // Total Energy = rho * (epsilon + 0.5 * u^2)
        UL.eng = rho_L * (eps_L + 0.5 * (u_L * u_L + v_L * v_L + w_L * w_L));

        // Right State
        UR.rho = rho_R;
        UR.mom_x = rho_R * u_R;
        UR.mom_y = rho_R * v_R;
        UR.mom_z = rho_R * w_R;
        UR.eng = rho_R * (eps_R + 0.5 * (u_R * u_R + v_R * v_R + w_R * w_R));

        return {UL, UR};
    }

    static std::pair<FluidVector, FluidVector> run(const FluidState &state, int i, int stride = 1)
    {
        return apply(state.get(i - 2 * stride), state.get(i - stride), state.get(i),
                     state.get(i + stride), state.get(i + 2 * stride), state.get(i + 3 * stride));
    }

    /**
     * @brief 组分重构
     */
    // 在 PPMReconstruction 结构体内部更新 run_species

    /**
     * @brief 组分重构 (Species) - 增强版
     * * 修复了震荡问题，增加了：
     * 1. 局部极值钳位 (Local Bounds Clamping)
     * 2. 总和归一化 (Renormalization)
     */
    static void run_species(const FluidState &state, int i, int n_spec, double *Y_L, double *Y_R,
                            int stride = 1)
    {
        double stencil[6];
        int indices[6] = {i - 2 * stride, i - stride, i, i + stride, i + 2 * stride, i + 3 * stride};
        double sum_Y_L = 0.0, sum_Y_R = 0.0;

        for (int k = 0; k < n_spec; ++k)
        {
            for (int s = 0; s < 6; ++s)
                stencil[s] = state.Y(k, indices[s]);

            auto res = reconstruct_scalar_ppm(stencil);
            double yl = res.first;
            double yr = res.second;

            // 基础物理约束
            yl = std::max(0.0, std::min(1.0, yl));
            yr = std::max(0.0, std::min(1.0, yr));

            Y_L[k] = yl;
            Y_R[k] = yr;
            sum_Y_L += yl;
            sum_Y_R += yr;
        }

        // 归一化 (Renormalization) - 必须要有，否则 EOS 会炸
        if (sum_Y_L > 1e-12)
        {
            double inv = 1.0 / sum_Y_L;
            for (int k = 0; k < n_spec; ++k)
                Y_L[k] *= inv;
        }
        if (sum_Y_R > 1e-12)
        {
            double inv = 1.0 / sum_Y_R;
            for (int k = 0; k < n_spec; ++k)
                Y_R[k] *= inv;
        }
    }
};