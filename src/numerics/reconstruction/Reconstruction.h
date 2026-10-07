/**
 * @file Reconstruction.h
 * @brief Spatial reconstruction from cell averages to interface states.
 *
 * Workflow:
 * 1. Select the existing PCM, MUSCL limiter or PPM reconstruction policy.
 * 2. Gather required conservative/primitive cell means and actual EOS pressure.
 * 3. Build the selected scalar/primitive endpoints with shared Host/device math.
 * 4. Probe reconstructed EOS energy; the calling owner handles trial fallback.
 * PPM exposes both endpoints of one owning cell from its existing five-value
 * stencil, so geometry adapters need no extra seventh axial stencil value.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "numerics/reconstruction/Limiters.h"

#include "data/FluidState.h"
#include "numerics/state/StateAdmissibility.h"

namespace ReconstructionMath {
// A reconstructed primitive face is a trial state. If its EOS inverse is
// outside the physical table, the owner publishes its original cell mean
// and matching composition. Required stencil/mean queries remain strict.
template <class Eos>
ARCH_INLINE double probe_face_energy(const Eos& eos, double rho, double u,
    double v, double w, double pressure, const double* x)
{
#if defined(__CUDA_ARCH__)
    if constexpr (requires { eos.probe_total_energy_primitive(rho, u, v, w, pressure, x); })
        return eos.probe_total_energy_primitive(rho, u, v, w, pressure, x);
    return eos.get_total_energy_primitive(rho, u, v, w, pressure, x);
#else
    try {
        if constexpr (requires { eos.probe_total_energy_primitive(rho, u, v, w, pressure, x); })
            return eos.probe_total_energy_primitive(rho, u, v, w, pressure, x);
        return eos.get_total_energy_primitive(rho, u, v, w, pressure, x);
    } catch (const std::runtime_error&) {
        return arch::state::invalid();
    }
#endif
}
// One simplex normalization for all reconstructed face compositions. Applying
// independent nonlinear limiters does not preserve sum(X)=1 even when all
// stencil cells are normalized. The Riemann species flux must sum to the mass
// flux before AMR registration; normalizing only the updated cell is too late.
ARCH_INLINE void normalize_species_faces(int n_spec, double* X_L, double* X_R)
{
    if (!arch::state::normalize_composition(X_L, n_spec))
        for (int i = 0; i < n_spec; ++i) X_L[i] = arch::state::invalid();
    if (!arch::state::normalize_composition(X_R, n_spec))
        for (int i = 0; i < n_spec; ++i) X_R[i] = arch::state::invalid();
}
} // namespace ReconstructionMath

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
ARCH_INLINE double compute_limited_slope(double q_m1, double q_0, double q_p1)
{
    // Forward difference (denominator)
    double del_plus = q_p1 - q_0;
    // Backward difference (numerator)
    double del_minus = q_0 - q_m1;

    // A vanishing forward difference cannot define a reliable ratio.
    if (del_plus == 0.0 || (del_minus > 0.0) != (del_plus > 0.0))
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

    static ARCH_INLINE void reconstruct(
        const FluidVector& U_i, const FluidVector& U_ip1,
        FluidVector& U_L, FluidVector& U_R)
    {
        U_L = U_i;
        U_R = U_ip1;
    }

    static ARCH_INLINE void reconstruct_species(
        double X_i, double X_ip1, double& X_L, double& X_R)
    {
        X_L = X_i;
        X_R = X_ip1;
    }
    /**
     * @brief Apply reconstruction.
     * PCM consumes only the two adjacent cell averages; its compact signature
     * is adapted by the common reconstruction dispatcher.
     */
    static std::pair<FluidVector, FluidVector> apply(
        const FluidVector &U_i,
        const FluidVector &U_ip1)
    {
        FluidVector U_L;
        FluidVector U_R;
        reconstruct(U_i, U_ip1, U_L, U_R);
        return {U_L, U_R};
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
            reconstruct_species(
                state.X(k, i), state.X(k, i + stride), X_L[k], X_R[k]);
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
 * @return std::pair<FluidVector, FluidVector> {U_L, U_R}
 */

template <typename Limiter>
struct MusclReconstruction
{
    static std::string name() { return "MUSCL-" + Limiter::name(); }

    static constexpr int NG = 2;

    static ARCH_INLINE void normalize_species_faces(
        int n_spec, double* X_L, double* X_R)
    {
        ReconstructionMath::normalize_species_faces(n_spec, X_L, X_R);
    }

    static ARCH_INLINE void reconstruct_species(
        double X_im1, double X_i, double X_ip1, double X_ip2,
        double& X_L, double& X_R)
    {
        X_L = X_i + compute_limited_slope<Limiter>(X_im1, X_i, X_ip1);
        X_R = X_ip1 - compute_limited_slope<Limiter>(X_i, X_ip1, X_ip2);
    }

    static ARCH_INLINE void reconstruct(
        const FluidVector& U_im1, const FluidVector& U_i,
        const FluidVector& U_ip1, const FluidVector& U_ip2,
        FluidVector& U_L, FluidVector& U_R)
    {
        U_L.rho = U_i.rho + compute_limited_slope<Limiter>(U_im1.rho, U_i.rho, U_ip1.rho);
        U_L.mom_u = U_i.mom_u + compute_limited_slope<Limiter>(U_im1.mom_u, U_i.mom_u, U_ip1.mom_u);
        U_L.mom_v = U_i.mom_v + compute_limited_slope<Limiter>(U_im1.mom_v, U_i.mom_v, U_ip1.mom_v);
        U_L.mom_w = U_i.mom_w + compute_limited_slope<Limiter>(U_im1.mom_w, U_i.mom_w, U_ip1.mom_w);
        U_L.eng = U_i.eng + compute_limited_slope<Limiter>(U_im1.eng, U_i.eng, U_ip1.eng);

        U_R.rho = U_ip1.rho - compute_limited_slope<Limiter>(U_i.rho, U_ip1.rho, U_ip2.rho);
        U_R.mom_u = U_ip1.mom_u - compute_limited_slope<Limiter>(U_i.mom_u, U_ip1.mom_u, U_ip2.mom_u);
        U_R.mom_v = U_ip1.mom_v - compute_limited_slope<Limiter>(U_i.mom_v, U_ip1.mom_v, U_ip2.mom_v);
        U_R.mom_w = U_ip1.mom_w - compute_limited_slope<Limiter>(U_i.mom_w, U_ip1.mom_w, U_ip2.mom_w);
        U_R.eng = U_ip1.eng - compute_limited_slope<Limiter>(U_i.eng, U_ip1.eng, U_ip2.eng);
    }

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
        reconstruct(U_im1, U_i, U_ip1, U_ip2, U_L, U_R);
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

            reconstruct_species(x_im1, x_i, x_ip1, x_ip2, X_L[k], X_R[k]);
        }
        normalize_species_faces(n_spec, X_L, X_R);
    }
};

/**
 * @struct PPMReconstruction
 * @brief Third-order piecewise-parabolic reconstruction with continuous
 * curvature-supported face bounds and a CW endpoint blend.
 */
struct PPMReconstruction
{
    static std::string name() { return "PPM (3rd Order)"; }

    static constexpr int NG = 3;

private:
    // Curvature support follows the second-difference limiting principle of
    // Sekora/Colella (2009, arXiv:0903.4200). The continuous projection/blend
    // below is stated explicitly rather than using their conditional switches.
    // This shared dimensionless coefficient bounds local curvature relative to
    // neighboring second differences; it is a numerical method coefficient.
    static constexpr double kCurvatureLimitCoefficient = 1.25;

    static ARCH_INLINE double interpolate_face_4th(double u_im1, double u_i, double u_ip1, double u_ip2)
    {
        return (7.0 / 12.0) * (u_i + u_ip1) - (1.0 / 12.0) * (u_im1 + u_ip2);
    }

    // Continuous extended-interval face projection between the cell averages
    // u_i and u_ip1. The fourth-order face f is clamped into
    // [min(u_i,u_ip1) - kp/6, max(u_i,u_ip1) + kn/6], where kp and kn are the
    // positive and negative curvature supports bounded by the two adjacent
    // second differences. Clamp and bounds are continuous in the data and scale
    // equivariant; a zero supported curvature reduces the projection to the
    // ordinary adjacent-average bound, so no inside/outside switch or midpoint
    // replacement is needed. Cell averages are not point samples: the retained
    // curvature keeps the true extremum face of an exact quadratic instead of
    // flattening it to the average of the two cell means.
    static ARCH_INLINE double project_face(
        double f, double u_im1, double u_i, double u_ip1, double u_ip2)
    {
        const double d_left = (u_ip1 - u_i) - (u_i - u_im1);
        const double d_right = (u_ip2 - u_ip1) - (u_ip1 - u_i);
        const double k_positive = std::max(0.0, std::min(
            kCurvatureLimitCoefficient * d_left, kCurvatureLimitCoefficient * d_right));
        const double k_negative = std::max(0.0, std::min(
            -kCurvatureLimitCoefficient * d_left, -kCurvatureLimitCoefficient * d_right));
        const double lower = std::min(u_i, u_ip1) - k_positive / 6.0;
        const double upper = std::max(u_i, u_ip1) + k_negative / 6.0;
        return std::max(lower, std::min(f, upper));
    }

    // Colella/Woodward two-to-one endpoint bound used by the profile blend.
    static ARCH_INLINE void apply_cw_bound(
        double &u_left, double &u_right, double u_average)
    {
        const double delta_left = u_left - u_average;
        const double delta_right = u_right - u_average;

        if ((delta_left > 0.0 && delta_right > 0.0)
            || (delta_left < 0.0 && delta_right < 0.0))
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

    // Continuous curvature-supported CW blend for one cell. The original
    // Colella/Woodward two-to-one endpoint bound is computed unconditionally from
    // the input faces fL/fR and the cell mean u_i, then blended back toward the
    // original faces by theta = min(1, k/|P|). Here P = 6*(aL+aR) is the
    // dimensionless parabolic second derivative, supported by the
    // three adjacent second differences. theta=1 keeps the original profile
    // (quadratics satisfy |P| <= C*min(|dL|,|dC|,|dR|)) and theta=0 keeps the CW
    // bound for unsupported sharp profiles; both limits join continuously because
    // the CW correction is itself bounded by |P|/6 per endpoint, so the weighted
    // blend tends to the original face as P tends to zero even when theta has a
    // directional limit. A zero P is an exact linear profile and returns the
    // original faces instead of dividing 0/0.
    static ARCH_INLINE void apply_cell_limiter(
        double &fL, double &fR,
        double u_im2, double u_im1, double u_i, double u_ip1, double u_ip2)
    {
        double cw_left = fL;
        double cw_right = fR;
        apply_cw_bound(cw_left, cw_right, u_i);

        const double a_left = fL - u_i;
        const double a_right = fR - u_i;
        const double P = 6.0 * (a_left + a_right);
        if (P == 0.0)
            return; // linear profile: the original faces already lie on it

        const double d_left = (u_i - u_im1) - (u_im1 - u_im2);
        const double d_center = (u_ip1 - u_i) - (u_i - u_im1);
        const double d_right = (u_ip2 - u_ip1) - (u_ip1 - u_i);
        const double S = P > 0.0 ? 1.0 : -1.0;
        const double k = std::max(0.0, std::min({
            kCurvatureLimitCoefficient * S * d_left,
            kCurvatureLimitCoefficient * S * d_center,
            kCurvatureLimitCoefficient * S * d_right}));
        const double theta = std::min(1.0, k / std::abs(P));
        if (theta == 1.0)
            return; // full original profile, exact by construction
        if (theta == 0.0) {
            fL = cw_left;
            fR = cw_right;
            return;
        }
        fL = cw_left + theta * (fL - cw_left);
        fR = cw_right + theta * (fR - cw_right);
    }

    /** Apply the existing CW/curvature rule to already projected own-cell faces.
     * The five means are ordered [i-2,i-1,i,i+1,i+2]. Keeping projection outside
     * lets the ordinary six-point pair preserve its original three-face order.
     */
    static ARCH_INLINE void limit_own_scalar_cell(
        const double (&v)[5], double& lower, double& upper)
    {
        apply_cell_limiter(lower, upper, v[0], v[1], v[2], v[3], v[4]);
    }

    /** Original primitive pressure/density ray for one owning-cell face.
     * theta=min(1,rho_mean/[2*(rho_mean-rho_face)],
     * p_mean/[2*(p_mean-p_face)]) only for a nonpositive trial rho/p.
     * All primitive components follow that same original per-face ray.
     */
    static ARCH_INLINE void limit_own_primitive_face(
        double mean_rho, double mean_velocity_x, double mean_velocity_y,
        double mean_velocity_z, double mean_pressure,
        double& rho, double& velocity_x, double& velocity_y,
        double& velocity_z, double& pressure)
    {
        double theta=1.0;
        if (!(rho > 0.0)) theta=std::min(theta,0.5*mean_rho/(mean_rho-rho));
        if (!(pressure > 0.0)) theta=std::min(theta,0.5*mean_pressure/(mean_pressure-pressure));
        if (theta < 1.0) {
            rho=mean_rho+theta*(rho-mean_rho);
            pressure=mean_pressure+theta*(pressure-mean_pressure);
            velocity_x=mean_velocity_x+theta*(velocity_x-mean_velocity_x);
            velocity_y=mean_velocity_y+theta*(velocity_y-mean_velocity_y);
            velocity_z=mean_velocity_z+theta*(velocity_z-mean_velocity_z);
        }
    }

    /** Assemble the same physical conserved trial and original EOS probe.
     * m=rho*u and E=EOS(rho,u,p,X); unresolved energy remains nonfinite for the
     * caller to reject or restore with the matching owning composition.
     */
    template <typename EosType>
    static ARCH_INLINE void assemble_own_eos_point(
        double rho, double velocity_x, double velocity_y, double velocity_z,
        double pressure, const double* composition, const EosType& eos,
        FluidVector& point)
    {
        point.rho = rho;
        point.mom_u = rho * velocity_x;
        point.mom_v = rho * velocity_y;
        point.mom_w = rho * velocity_z;
        point.eng = ReconstructionMath::probe_face_energy(
            eos, rho, velocity_x, velocity_y, velocity_z, pressure, composition);
    }

public:
    /** Reconstruct both endpoints of one owning scalar cell from five means.
     * Workflow: original fourth-order left/right face interpolation and
     * projection, then the unchanged owning CW/curvature limiter.
     * f_- = project((7*(v1+v2)-(v0+v3))/12),
     * f_+ = project((7*(v2+v3)-(v1+v4))/12).
     * Interpolation itself retains the original evaluated coefficient formula.
     */
    static ARCH_INLINE void reconstruct_scalar_cell_ppm(
        const double (&v)[5], double& lower, double& upper)
    {
        lower = project_face(
            interpolate_face_4th(v[0], v[1], v[2], v[3]), v[0], v[1], v[2], v[3]);
        upper = project_face(
            interpolate_face_4th(v[1], v[2], v[3], v[4]), v[1], v[2], v[3], v[4]);
        limit_own_scalar_cell(v, lower, upper);
    }

    /** Return the original left/right states at one face from six means.
     * All three projected faces retain their original evaluation order; the
     * two exact five-value windows delegate only their owning-cell limiting.
     */
    static ARCH_INLINE void reconstruct_scalar_ppm(
        const double (&v)[6], double& left, double& right)
    {
        const double u_face_imhalf = project_face(
            interpolate_face_4th(v[0], v[1], v[2], v[3]), v[0], v[1], v[2], v[3]);
        const double u_face_iphalf = project_face(
            interpolate_face_4th(v[1], v[2], v[3], v[4]), v[1], v[2], v[3], v[4]);
        const double u_face_ip3half = project_face(
            interpolate_face_4th(v[2], v[3], v[4], v[5]), v[2], v[3], v[4], v[5]);

        double u_L_cell_i = u_face_imhalf;
        double u_R_cell_i = u_face_iphalf;
        const double own_i[5]{v[0], v[1], v[2], v[3], v[4]};
        limit_own_scalar_cell(own_i, u_L_cell_i, u_R_cell_i);

        double u_L_cell_ip1 = u_face_iphalf;
        double u_R_cell_ip1 = u_face_ip3half;
        const double own_ip1[5]{v[1], v[2], v[3], v[4], v[5]};
        limit_own_scalar_cell(own_ip1, u_L_cell_ip1, u_R_cell_ip1);

        left = u_R_cell_i;
        right = u_L_cell_ip1;
    }

    static ARCH_INLINE void reconstruct_species_value(
        const double (&stencil)[6], double& left, double& right)
    {
        reconstruct_scalar_ppm(stencil, left, right);
        left = std::max(0.0, std::min(1.0, left));
        right = std::max(0.0, std::min(1.0, right));
    }

    static ARCH_INLINE void normalize_species_faces(
        int n_spec, double* X_L, double* X_R)
    {
        ReconstructionMath::normalize_species_faces(n_spec, X_L, X_R);
    }

    template <typename EosType>
    static ARCH_INLINE void gather_eos_stencil_point(
        const FluidVector& state, const double* composition,
        const EosType& eos, double& rho, double& velocity_x,
        double& velocity_y, double& velocity_z, double& pressure)
    {
        rho = state.rho;
        velocity_x = state.mom_u / rho;
        velocity_y = state.mom_v / rho;
        velocity_z = state.mom_w / rho;
        pressure = eos.get_pressure(state, composition);
    }

    template <typename EosType>
    static ARCH_INLINE void reconstruct_eos(
        const double (&rho)[6], const double (&velocity_x)[6],
        const double (&velocity_y)[6], const double (&velocity_z)[6],
        const double (&pressure)[6], const double* X_left,
        const double* X_right, const EosType& eos,
        FluidVector& left, FluidVector& right)
    {
        double face_rho[2], face_velocity_x[2], face_velocity_y[2];
        double face_velocity_z[2], face_pressure[2];
        reconstruct_scalar_ppm(rho, face_rho[0], face_rho[1]);
        reconstruct_scalar_ppm(velocity_x, face_velocity_x[0], face_velocity_x[1]);
        reconstruct_scalar_ppm(velocity_y, face_velocity_y[0], face_velocity_y[1]);
        reconstruct_scalar_ppm(velocity_z, face_velocity_z[0], face_velocity_z[1]);
        reconstruct_scalar_ppm(pressure, face_pressure[0], face_pressure[1]);

        // Ray-limit primitive pressure/density against each owning mean before
        // asking an EOS to invert the face. No dimensional pressure floor.
        for (int side=0; side<2; ++side) {
            const int center=side+2;
            limit_own_primitive_face(
                rho[center], velocity_x[center], velocity_y[center],
                velocity_z[center], pressure[center],
                face_rho[side], face_velocity_x[side], face_velocity_y[side],
                face_velocity_z[side], face_pressure[side]);
        }
        const double rho_left = face_rho[0];
        const double rho_right = face_rho[1];
        const double pressure_left = face_pressure[0];
        const double pressure_right = face_pressure[1];

        assemble_own_eos_point(
            rho_left, face_velocity_x[0], face_velocity_y[0],
            face_velocity_z[0], pressure_left, X_left, eos, left);
        assemble_own_eos_point(
            rho_right, face_velocity_x[1], face_velocity_y[1],
            face_velocity_z[1], pressure_right, X_right, eos, right);
    }

    /** Expose original own-cell primitive endpoints AFTER its rho/p ray.
     * The caller can derive matching rhoX/rho before any actual EOS inverse;
     * no trial material is substituted and no new limiting formula is added.
     */
    static ARCH_INLINE void reconstruct_cell_primitives(
        const double (&rho)[5], const double (&velocity_x)[5],
        const double (&velocity_y)[5], const double (&velocity_z)[5],
        const double (&pressure)[5], double (&face_rho)[2],
        double (&face_velocity_x)[2], double (&face_velocity_y)[2],
        double (&face_velocity_z)[2], double (&face_pressure)[2])
    {
        reconstruct_scalar_cell_ppm(rho, face_rho[0], face_rho[1]);
        reconstruct_scalar_cell_ppm(velocity_x, face_velocity_x[0], face_velocity_x[1]);
        reconstruct_scalar_cell_ppm(velocity_y, face_velocity_y[0], face_velocity_y[1]);
        reconstruct_scalar_cell_ppm(velocity_z, face_velocity_z[0], face_velocity_z[1]);
        reconstruct_scalar_cell_ppm(pressure, face_pressure[0], face_pressure[1]);
        for (int side=0; side<2; ++side) {
            limit_own_primitive_face(
                rho[2], velocity_x[2], velocity_y[2], velocity_z[2], pressure[2],
                face_rho[side], face_velocity_x[side], face_velocity_y[side],
                face_velocity_z[side], face_pressure[side]);
        }
    }

    /** Reconstruct physical primitive/EOS trials at both faces of one cell.
     * Workflow: reuse the original scalar owning-cell PPM for rho/u/p, apply
     * the original per-face rho/p ray against index two, then probe the actual
     * EOS with each supplied matching face composition. Required stencil EOS
     * gathering remains the caller's strict responsibility. This entry performs
     * no species normalization, baseline restore or native common-theta rule.
     */
    template <typename EosType>
    static ARCH_INLINE void reconstruct_cell_eos(
        const double (&rho)[5], const double (&velocity_x)[5],
        const double (&velocity_y)[5], const double (&velocity_z)[5],
        const double (&pressure)[5], const double* X_lower,
        const double* X_upper, const EosType& eos,
        FluidVector& lower, FluidVector& upper)
    {
        double face_rho[2], face_velocity_x[2], face_velocity_y[2];
        double face_velocity_z[2], face_pressure[2];
        reconstruct_cell_primitives(rho, velocity_x, velocity_y, velocity_z,
            pressure, face_rho, face_velocity_x, face_velocity_y,
            face_velocity_z, face_pressure);
        assemble_own_eos_point(
            face_rho[0], face_velocity_x[0], face_velocity_y[0],
            face_velocity_z[0], face_pressure[0], X_lower, eos, lower);
        assemble_own_eos_point(
            face_rho[1], face_velocity_x[1], face_velocity_y[1],
            face_velocity_z[1], face_pressure[1], X_upper, eos, upper);
    }

private:
    /**
     * @brief Reconstruct both states at face i+1/2 from cells i-2 through i+3.
     */
    static std::pair<double, double> reconstruct_scalar_ppm(const double (&v)[6])
    {
        double left;
        double right;
        reconstruct_scalar_ppm(v, left, right);
        return {left, right};
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
            double rho = U_stencil[k]->rho;

            double vel_u = U_stencil[k]->mom_u / rho;
            double vel_v = U_stencil[k]->mom_v / rho;
            double vel_w = U_stencil[k]->mom_w / rho;

            double kin = 0.5 * (vel_u * vel_u + vel_v * vel_v + vel_w * vel_w);
            const double eint_density = U_stencil[k]->eng - rho * kin;

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
        double rho_L = res_rho.first;
        double rho_R = res_rho.second;

        double eint_density_L = res_eint_density.first;
        double eint_density_R = res_eint_density.second;

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
        double *X_left, double *X_right, double *X_cell,
        int stride = 1)
    {
        const int indices[6] = {
            i - 2 * stride, i - stride, i,
            i + stride, i + 2 * stride, i + 3 * stride};
        double rho[6], velocity_x[6], velocity_y[6], velocity_z[6], pressure[6];
        for (int stencil_index = 0; stencil_index < 6; ++stencil_index) {
            const int cell = indices[stencil_index];
            const FluidVector U = state.get(cell);
            for (int species = 0; species < n_spec; ++species)
                X_cell[species] = state.X(species, cell);
            gather_eos_stencil_point(
                U, n_spec > 0 ? X_cell : nullptr, eos,
                rho[stencil_index], velocity_x[stencil_index],
                velocity_y[stencil_index], velocity_z[stencil_index],
                pressure[stencil_index]);
        }
        FluidVector left;
        FluidVector right;
        reconstruct_eos(
            rho, velocity_x, velocity_y, velocity_z, pressure,
            X_left, X_right, eos, left, right);
        // The first-order fallback is the owning conserved cell average.
        // Restore its composition at the same time: an EOS-valid mean with
        // reconstructed X could otherwise leave the material table domain.
        if (!std::isfinite(left.eng)) {
            left = state.get(i);
            for (int species = 0; species < n_spec; ++species)
                X_left[species] = state.X(species, i);
        }
        if (!std::isfinite(right.eng)) {
            right = state.get(i + stride);
            for (int species = 0; species < n_spec; ++species)
                X_right[species] = state.X(species, i + stride);
        }
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
        for (int k = 0; k < n_spec; ++k)
        {
            for (int s = 0; s < 6; ++s)
                stencil[s] = state.X(k, indices[s]);

            reconstruct_species_value(stencil, X_L[k], X_R[k]);
        }
        normalize_species_faces(n_spec, X_L, X_R);
    }
};
