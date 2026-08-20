/**
 * @file Reconstruction.h
 * @brief Spatial reconstruction from cell averages to interface states.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <utility>

#include "Limiters.h"

#include "../../data/FluidState.h"

/**
 * @brief Helper to calculate slope ratio r and apply limiter.
 * r_i = (q_i - q_{i-1}) / (q_{i+1} - q_i)
 * delta = phi(r) * (q_{i+1} - q_i)
 * @tparam LimiterPolicy The limiter struct (MinMod, SuperBee, etc.)
 * @param q_m1 Value at i-1
 * @param q_0  Value at i
 * @param q_p1 Value at i+1
 * @return double The limited slope contribution: 0.5 * phi(r) * (q_p1 - q_0)
 */
template <typename LimiterPolicy>
inline double compute_limited_slope(double q_m1, double q_0, double q_p1)
{
    // Treat a forward jump below this absolute scale as locally flat to avoid
    // an ill-conditioned slope ratio. Conserved variables are expected to be
    // nondimensionalized or scaled consistently with this threshold.
    const double epsilon = 1e-12;
    // Forward difference (denominator)
    double del_plus = q_p1 - q_0;
    // Backward difference (numerator)
    double del_minus = q_0 - q_m1;

    // A vanishing forward difference cannot define a reliable ratio.
    if (std::abs(del_plus) < epsilon)
    {
        // Returning zero is the monotone fallback for both flat regions and
        // one-sided discontinuities where r would be singular.
        return 0.0;
    }

    double r = del_minus / del_plus;
    double phi = LimiterPolicy::calc(r);

    return 0.5 * phi * del_plus;
}

// 1. PCM: Piecewise Constant Method
/**
 * @struct PCMReconstruction
 * @brief Piecewise Constant Method (1st Order).
 * Logic:
 * U_L(i+1/2) = U_i
 * U_R(i+1/2) = U_{i+1}
 * No limiters, no gradients. Most diffusive but strictly monotonic.
 */
struct PCMReconstruction
{

    // Human-readable policy name for logs.
    static std::string name() { return "PCM (1st Order)"; }

    static constexpr int NG = 1;
    /**
     * @brief Apply reconstruction.
     * PCM consumes only the two adjacent cell averages; its compact signature
     * is adapted by the common reconstruction dispatcher.
     */
    static std::pair<FluidVector, FluidVector> apply(
        const FluidVector &U_i,
        const FluidVector &U_ip1)
    {
        // PCM passes the two adjacent cell averages directly to the face.
        return {U_i, U_ip1};
    }

    static std::pair<FluidVector, FluidVector> run(const FluidState &state, int i, int stride = 1)
    {
        return apply(state.get(i), state.get(i + stride));
    }

    /**
     * @brief Apply Species reconstruction.
     */
    static void run_species(const FluidState &state, int i, int n_spec, double *X_L, double *X_R, int stride = 1)
    {
        for (int k = 0; k < n_spec; ++k)
        {
            X_L[k] = state.X(k, i);
            X_R[k] = state.X(k, i + stride);
        }
    }
};

// 2. MUSCL: Monotonic Upstream-Centered Scheme
/**
 * @brief Performs MUSCL reconstruction for interface i+1/2.
 * Reconstructs the state at the interface between cell i and cell i+1.
 * U_L (at i+1/2) comes from expanding cell i to its right edge.
 * U_R (at i+1/2) comes from expanding cell i+1 to its left edge.
 * Stencil required: i-1, i, i+1, i+2.
 * @tparam Limiter The limiter policy (e.g., MinMod, VanLeer).
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
        U_L.mom_u = U_i.mom_u + compute_limited_slope<Limiter>(U_im1.mom_u, U_i.mom_u, U_ip1.mom_u);
        U_L.mom_v = U_i.mom_v + compute_limited_slope<Limiter>(U_im1.mom_v, U_i.mom_v, U_ip1.mom_v);
        U_L.mom_w = U_i.mom_w + compute_limited_slope<Limiter>(U_im1.mom_w, U_i.mom_w, U_ip1.mom_w);
        U_L.eng = U_i.eng + compute_limited_slope<Limiter>(U_im1.eng, U_i.eng, U_ip1.eng);

        // --- Right State (at i+1/2) ---
        // Based on cell i+1, looking at i and i+2
        // Subtract the limited slope to project cell i+1 to its left face.
        U_R.rho = U_ip1.rho - compute_limited_slope<Limiter>(U_i.rho, U_ip1.rho, U_ip2.rho);
        U_R.mom_u = U_ip1.mom_u - compute_limited_slope<Limiter>(U_i.mom_u, U_ip1.mom_u, U_ip2.mom_u);
        U_R.mom_v = U_ip1.mom_v - compute_limited_slope<Limiter>(U_i.mom_v, U_ip1.mom_v, U_ip2.mom_v);
        U_R.mom_w = U_ip1.mom_w - compute_limited_slope<Limiter>(U_i.mom_w, U_ip1.mom_w, U_ip2.mom_w);
        U_R.eng = U_ip1.eng - compute_limited_slope<Limiter>(U_i.eng, U_ip1.eng, U_ip2.eng);

        return {U_L, U_R};
    }

    static std::pair<FluidVector, FluidVector> run(const FluidState &state, int i, int stride = 1)
    {
        // i denotes the cell immediately left of face i+1/2.
        // Stencil: i-1, i, i+1, i+2
        return apply(state.get(i - stride), state.get(i), state.get(i + stride), state.get(i + 2 * stride));
    }

    /**
     * @brief Reconstruct Species (Batch).
     * Pointers are used for efficiency since species count is dynamic.
     */
    static void run_species(const FluidState &state, int i, int n_spec, double *X_L, double *X_R, int stride = 1)
    {
        int im1 = i - stride;
        int ip1 = i + stride;
        int ip2 = i + 2 * stride;
        for (int k = 0; k < n_spec; ++k)
        {
            double x_im1 = state.X(k, im1);
            double x_i = state.X(k, i);
            double x_ip1 = state.X(k, ip1);
            double x_ip2 = state.X(k, ip2);

            X_L[k] = x_i + compute_limited_slope<Limiter>(x_im1, x_i, x_ip1);
            X_R[k] = x_ip1 - compute_limited_slope<Limiter>(x_i, x_ip1, x_ip2);
        }
    }
};

/**
 * @struct PPMReconstruction
 * @brief Third-order piecewise-parabolic reconstruction with smooth-extremum preservation.
 */
struct PPMReconstruction
{
    static std::string name() { return "PPM (3rd Order)"; }

    static constexpr int NG = 3;

private:
    static inline double interpolate_face_4th(double u_im1, double u_i, double u_ip1, double u_ip2)
    {
        return (7.0 / 12.0) * (u_i + u_ip1) - (1.0 / 12.0) * (u_im1 + u_ip2);
    }

    static inline bool is_smooth_extremum(
        double u_im2, double u_im1, double u_i, double u_ip1, double u_ip2)
    {
        const double slope_left = u_i - u_im1;
        const double slope_right = u_ip1 - u_i;
        if (slope_left * slope_right > 0.0)
            return false;

        const double d2_left = u_im2 - 2.0 * u_im1 + u_i;
        const double d2_center = u_im1 - 2.0 * u_i + u_ip1;
        const double d2_right = u_i - 2.0 * u_ip1 + u_ip2;
        const double curvature_scale = std::max({
            std::abs(d2_left), std::abs(d2_center), std::abs(d2_right)});
        const double value_scale = std::max({
            1.0, std::abs(u_im2), std::abs(u_im1), std::abs(u_i),
            std::abs(u_ip1), std::abs(u_ip2)});

        if (curvature_scale <= 1e-12 * value_scale)
            return false;
        if (d2_left * d2_center <= 0.0 || d2_center * d2_right <= 0.0)
            return false;

        const double min_curvature = std::min({
            std::abs(d2_left), std::abs(d2_center), std::abs(d2_right)});
        return min_curvature >= 0.25 * curvature_scale;
    }

    static inline void apply_cw_limiter(
        double &u_left, double &u_right, double u_average,
        bool preserve_smooth_extremum)
    {
        const double delta_left = u_left - u_average;
        const double delta_right = u_right - u_average;

        if (preserve_smooth_extremum)
            return;

        if (delta_left * delta_right > 0.0)
        {
            u_left = u_average;
            u_right = u_average;
            return;
        }

        if (std::abs(delta_left) >= 2.0 * std::abs(delta_right))
            u_left = u_average - 2.0 * delta_right;
        else if (std::abs(delta_right) >= 2.0 * std::abs(delta_left))
            u_right = u_average - 2.0 * delta_left;
    }

    /**
     * @brief Reconstruct both states at face i+1/2 from cells i-2 through i+3.
     */
    static std::pair<double, double> reconstruct_scalar_ppm(const double v[6])
    {
        double u_face_imhalf = interpolate_face_4th(v[0], v[1], v[2], v[3]);
        double u_face_iphalf = interpolate_face_4th(v[1], v[2], v[3], v[4]);
        double u_face_ip3half = interpolate_face_4th(v[2], v[3], v[4], v[5]);

        double u_L_cell_i = u_face_imhalf;
        double u_R_cell_i = u_face_iphalf;
        apply_cw_limiter(
            u_L_cell_i, u_R_cell_i, v[2],
            is_smooth_extremum(v[0], v[1], v[2], v[3], v[4]));

        double u_L_cell_ip1 = u_face_iphalf;
        double u_R_cell_ip1 = u_face_ip3half;
        apply_cw_limiter(
            u_L_cell_ip1, u_R_cell_ip1, v[3],
            is_smooth_extremum(v[1], v[2], v[3], v[4], v[5]));

        return {u_R_cell_i, u_L_cell_ip1};
    }

public:
    /**
     * @brief Apply PPM to FluidVectors (Component-wise).
     * Accept the complete stencil and reconstruct each component independently.
     */
    static std::pair<FluidVector, FluidVector> apply(
        const FluidVector &U_im2, const FluidVector &U_im1,
        const FluidVector &U_i,
        const FluidVector &U_ip1, const FluidVector &U_ip2, const FluidVector &U_ip3)
    {
        double r[6], u[6], v[6], w[6], internal_energy_density[6];
        const FluidVector *U_stencil[6] = {&U_im2, &U_im1, &U_i, &U_ip1, &U_ip2, &U_ip3};

        for (int k = 0; k < 6; ++k)
        {
            double rho = std::max(1e-13, U_stencil[k]->rho);

            double vel_u = U_stencil[k]->mom_u / rho;
            double vel_v = U_stencil[k]->mom_v / rho;
            double vel_w = U_stencil[k]->mom_w / rho;

            double kin = 0.5 * (vel_u * vel_u + vel_v * vel_v + vel_w * vel_w);
            const double eint_density = std::max(
                1e-13 * rho, U_stencil[k]->eng - rho * kin);

            r[k] = rho;
            u[k] = vel_u;
            v[k] = vel_v;
            w[k] = vel_w;
            internal_energy_density[k] = eint_density;
        }

        // Reconstruct primitive-like components independently.
        auto res_rho = reconstruct_scalar_ppm(r);
        auto res_u = reconstruct_scalar_ppm(u);
        auto res_v = reconstruct_scalar_ppm(v);
        auto res_w = reconstruct_scalar_ppm(w);
        auto res_eint_density = reconstruct_scalar_ppm(internal_energy_density);

        // Enforce positive density and internal-energy density.
        double rho_L = std::max(1e-13, res_rho.first);
        double rho_R = std::max(1e-13, res_rho.second);

        double eint_density_L = std::max(1e-13 * rho_L, res_eint_density.first);
        double eint_density_R = std::max(1e-13 * rho_R, res_eint_density.second);

        double u_L = res_u.first, v_L = res_v.first, w_L = res_w.first;
        double u_R = res_u.second, v_R = res_v.second, w_R = res_w.second;

        // Reassemble the conservative state.
        FluidVector UL, UR;

        // Left State
        UL.rho = rho_L;
        UL.mom_u = rho_L * u_L;
        UL.mom_v = rho_L * v_L;
        UL.mom_w = rho_L * w_L;
        UL.eng = eint_density_L
               + 0.5 * rho_L * (u_L * u_L + v_L * v_L + w_L * w_L);

        // Right State
        UR.rho = rho_R;
        UR.mom_u = rho_R * u_R;
        UR.mom_v = rho_R * v_R;
        UR.mom_w = rho_R * w_R;
        UR.eng = eint_density_R
               + 0.5 * rho_R * (u_R * u_R + v_R * v_R + w_R * w_R);

        return {UL, UR};
    }

    static std::pair<FluidVector, FluidVector> run(const FluidState &state, int i, int stride = 1)
    {
        return apply(state.get(i - 2 * stride), state.get(i - stride), state.get(i),
                     state.get(i + stride), state.get(i + 2 * stride), state.get(i + 3 * stride));
    }

    template <typename EosType>
    static std::pair<FluidVector, FluidVector> run_eos(
        const FluidState &state, const EosType &eos, int i, int n_spec,
        const double *X_left, const double *X_right, double *X_cell,
        int stride = 1)
    {
        const int indices[6] = {
            i - 2 * stride, i - stride, i,
            i + stride, i + 2 * stride, i + 3 * stride};
        double rho[6], velocity_x[6], velocity_y[6], velocity_z[6], pressure[6];

        for (int stencil_index = 0; stencil_index < 6; ++stencil_index) {
            const int cell = indices[stencil_index];
            const FluidVector U = state.get(cell);
            const double cell_rho = std::max(1e-13, U.rho);
            for (int species = 0; species < n_spec; ++species)
                X_cell[species] = state.X(species, cell);

            rho[stencil_index] = cell_rho;
            velocity_x[stencil_index] = U.mom_u / cell_rho;
            velocity_y[stencil_index] = U.mom_v / cell_rho;
            velocity_z[stencil_index] = U.mom_w / cell_rho;
            pressure[stencil_index] = eos.get_pressure(U, X_cell);
        }

        const auto face_rho = reconstruct_scalar_ppm(rho);
        const auto face_velocity_x = reconstruct_scalar_ppm(velocity_x);
        const auto face_velocity_y = reconstruct_scalar_ppm(velocity_y);
        const auto face_velocity_z = reconstruct_scalar_ppm(velocity_z);
        const auto face_pressure = reconstruct_scalar_ppm(pressure);

        const double rho_left = std::max(1e-13, face_rho.first);
        const double rho_right = std::max(1e-13, face_rho.second);
        const double pressure_left = std::max(1e-13, face_pressure.first);
        const double pressure_right = std::max(1e-13, face_pressure.second);

        FluidVector left;
        left.rho = rho_left;
        left.mom_u = rho_left * face_velocity_x.first;
        left.mom_v = rho_left * face_velocity_y.first;
        left.mom_w = rho_left * face_velocity_z.first;
        left.eng = eos.get_total_energy_primitive(
            rho_left, face_velocity_x.first, face_velocity_y.first,
            face_velocity_z.first, pressure_left, X_left);

        FluidVector right;
        right.rho = rho_right;
        right.mom_u = rho_right * face_velocity_x.second;
        right.mom_v = rho_right * face_velocity_y.second;
        right.mom_w = rho_right * face_velocity_z.second;
        right.eng = eos.get_total_energy_primitive(
            rho_right, face_velocity_x.second, face_velocity_y.second,
            face_velocity_z.second, pressure_right, X_right);

        return {left, right};
    }

    /**
     * @brief Species reconstruction with local bounds and renormalization.
     */
    static void run_species(const FluidState &state, int i, int n_spec, double *X_L, double *X_R,
                            int stride = 1)
    {
        double stencil[6];
        int indices[6] = {i - 2 * stride, i - stride, i, i + stride, i + 2 * stride, i + 3 * stride};
        double sum_X_L = 0.0, sum_X_R = 0.0;

        for (int k = 0; k < n_spec; ++k)
        {
            for (int s = 0; s < 6; ++s)
                stencil[s] = state.X(k, indices[s]);

            auto res = reconstruct_scalar_ppm(stencil);
            double yl = res.first;
            double yr = res.second;

            yl = std::max(0.0, std::min(1.0, yl));
            yr = std::max(0.0, std::min(1.0, yr));

            X_L[k] = yl;
            X_R[k] = yr;
            sum_X_L += yl;
            sum_X_R += yr;
        }

        // Supply normalized face compositions to the EOS and Riemann solver.
        if (sum_X_L > 1e-12)
        {
            double inv = 1.0 / sum_X_L;
            for (int k = 0; k < n_spec; ++k)
                X_L[k] *= inv;
        }
        if (sum_X_R > 1e-12)
        {
            double inv = 1.0 / sum_X_R;
            for (int k = 0; k < n_spec; ++k)
                X_R[k] *= inv;
        }
    }
};
