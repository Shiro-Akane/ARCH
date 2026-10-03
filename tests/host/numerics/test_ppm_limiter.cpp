// Focused independent checks of the shared PPM curvature limiter.
//
// The reference functions below independently transcribe the continuous
// extended-interval face projection and the
// curvature-supported CW blend, both with the single method coefficient
// C = 1.25 (Sekora/Colella 2009, arXiv:0903.4200 informs the curvature
// support). They call no PPM internals, so a mismatch means the production PPM
// face or profile formulas drifted from the frozen design. Cell averages are the
// reconstruction input; every tolerance is a floating-point rounding allowance
// and carries no physical units.
#include "numerics/reconstruction/Reconstruction.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {

constexpr double kEps = std::numeric_limits<double>::epsilon();
constexpr double kCoefficient = 1.25;
constexpr double kCurvatureBand = 64.0;

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

// Allow only accumulated rounding, never a different limiter branch.
void close_relative(double actual, double expected, double scale, const char* message)
{
    const double tolerance = 32.0 * kEps * std::max(scale, std::abs(expected));
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        std::cerr << message << ": " << actual << " vs " << expected
                  << " (tolerance " << tolerance << ")\n";
        throw std::runtime_error(message);
    }
}

double reference_interpolate_face(double u_im1, double u_i, double u_ip1, double u_ip2)
{
    return (7.0 / 12.0) * (u_i + u_ip1) - (1.0 / 12.0) * (u_im1 + u_ip2);
}

// Contract face projection: clamp the fourth-order face into the continuous
// extended interval [min - kp/6, max + kn/6] built from the positive/negative
// curvature supports. No binary inside/outside switch.
double reference_face(
    double f, double u_im1, double u_i, double u_ip1, double u_ip2)
{
    const double d_left = (u_ip1 - u_i) - (u_i - u_im1);
    const double d_right = (u_ip2 - u_ip1) - (u_ip1 - u_i);
    const double k_positive = std::max(0.0,
        std::min(kCoefficient * d_left, kCoefficient * d_right));
    const double k_negative = std::max(0.0,
        std::min(-kCoefficient * d_left, -kCoefficient * d_right));
    const double lower = std::min(u_i, u_ip1) - k_positive / 6.0;
    const double upper = std::max(u_i, u_ip1) + k_negative / 6.0;
    return std::max(lower, std::min(f, upper));
}

// Contract cell profile: the CW two-to-one bound is computed unconditionally,
// then blended back toward the original faces by the curvature-supported theta.
void reference_cell(double& fL, double& fR,
    double u_im2, double u_im1, double u_i, double u_ip1, double u_ip2)
{
    double cw_left = fL;
    double cw_right = fR;
    const double delta_left = cw_left - u_i;
    const double delta_right = cw_right - u_i;
    if ((delta_left > 0.0 && delta_right > 0.0)
        || (delta_left < 0.0 && delta_right < 0.0)) {
        cw_left = u_i;
        cw_right = u_i;
    } else if (std::abs(delta_left) >= 2.0 * std::abs(delta_right)) {
        cw_left = u_i - 2.0 * delta_right;
    } else if (std::abs(delta_right) >= 2.0 * std::abs(delta_left)) {
        cw_right = u_i - 2.0 * delta_left;
    }

    const double a_left = fL - u_i;
    const double a_right = fR - u_i;
    const double P = 6.0 * (a_left + a_right);
    if (P == 0.0)
        return; // exact linear profile: original faces already lie on it
    const double d_left = (u_i - u_im1) - (u_im1 - u_im2);
    const double d_center = (u_ip1 - u_i) - (u_i - u_im1);
    const double d_right = (u_ip2 - u_ip1) - (u_ip1 - u_i);
    const double S = P > 0.0 ? 1.0 : -1.0;
    const double k = std::max(0.0, std::min({kCoefficient * S * d_left,
        kCoefficient * S * d_center, kCoefficient * S * d_right}));
    const double theta = std::min(1.0, k / std::abs(P));
    if (theta == 1.0)
        return;
    if (theta == 0.0) {
        fL = cw_left;
        fR = cw_right;
        return;
    }
    fL = cw_left + theta * (fL - cw_left);
    fR = cw_right + theta * (fR - cw_right);
}

// v always points at the six cell averages v[0]..v[5] of one stencil.
void reference_ppm(const double* v, double& left, double& right)
{
    const double face_imhalf = reference_face(
        reference_interpolate_face(v[0], v[1], v[2], v[3]), v[0], v[1], v[2], v[3]);
    const double face_iphalf = reference_face(
        reference_interpolate_face(v[1], v[2], v[3], v[4]), v[1], v[2], v[3], v[4]);
    const double face_ip3half = reference_face(
        reference_interpolate_face(v[2], v[3], v[4], v[5]), v[2], v[3], v[4], v[5]);

    double left_cell = face_imhalf;
    double right_cell = face_iphalf;
    reference_cell(left_cell, right_cell, v[0], v[1], v[2], v[3], v[4]);

    double left_next = face_iphalf;
    double right_next = face_ip3half;
    reference_cell(left_next, right_next, v[1], v[2], v[3], v[4], v[5]);

    left = right_cell;
    right = left_next;
}

std::pair<double, double> production(const double* v)
{
    double left = 0.0;
    double right = 0.0;
    double stencil[6];
    for (int k = 0; k < 6; ++k) stencil[k] = v[k];
    PPMReconstruction::reconstruct_scalar_ppm(stencil, left, right);
    require(std::isfinite(left) && std::isfinite(right), "production face not finite");
    return {left, right};
}

double stencil_scale(const double* v)
{
    double scale = 0.0;
    for (int k = 0; k < 6; ++k) scale = std::max(scale, std::abs(v[k]));
    return scale;
}

void check_matches_reference(const double* v, const char* message)
{
    double expected_left = 0.0;
    double expected_right = 0.0;
    reference_ppm(v, expected_left, expected_right);
    const auto [left, right] = production(v);
    const double scale = stencil_scale(v);
    close_relative(left, expected_left, scale, message);
    close_relative(right, expected_right, scale, message);
}

// A constant field is flat in every cell, so both faces must return the
// constant exactly: no limiter may displace a zero-curvature profile.
void constants()
{
    for (double c : {1.0, -3.5, 1e-100, 1e100}) {
        double v[6] = {c, c, c, c, c, c};
        const auto [left, right] = production(v);
        require(left == c && right == c, "constant field must reconstruct exactly");
        check_matches_reference(v, "constant field reference");
    }
}

// Affine cell averages are reproduced at both interfaces by construction, and
// the CW endpoint bound must leave such a profile untouched.
void affine_fields()
{
    for (double slope : {0.5, -0.25, 3.0}) {
        for (double intercept : {2.0, -7.5}) {
            double v[6];
            for (int k = 0; k < 6; ++k) v[k] = intercept + slope * static_cast<double>(k - 2);
            const auto [left, right] = production(v);
            const double exact = 0.5 * (v[2] + v[3]);
            const double scale = std::max({std::abs(v[2]), std::abs(v[3]), std::abs(exact)});
            close_relative(left, exact, scale, "affine left interface");
            close_relative(right, exact, scale, "affine right interface");
            check_matches_reference(v, "affine reference");
        }
    }
}

// PPM is exact for a quadratic: with uniform cell means u_i = q(x_i) + c/12 the
// interface value is 0.5*(u_i+u_{i+1}) - c/3, and neither curvature limit may
// reduce the retained curvature below that exact parabola. Convex (c > 0) and
// concave (c < 0) profiles are both covered, including cells that straddle the
// analytic extremum.
void exact_quadratics()
{
    for (double c : {0.25, -0.5, 2.0}) {
        for (double b : {0.5, -1.25}) {
            for (int center = -3; center <= 3; ++center) {
                double v[6];
                for (int k = 0; k < 6; ++k) {
                    const double x = static_cast<double>(center - 2 + k);
                    v[k] = 2.0 + b * x + c * x * x + c / 12.0;
                }
                const auto [left, right] = production(v);
                const double exact = 0.5 * (v[2] + v[3]) - c / 3.0;
                const double scale = std::max(stencil_scale(v), std::abs(exact));
                close_relative(left, exact, scale, "quadratic left interface");
                close_relative(right, exact, scale, "quadratic right interface");
                check_matches_reference(v, "quadratic reference");
            }
        }
    }
}

// Two-ULP witness continuity: the old binary smooth-extremum bypass moved the
// left face by 0.0052083333333332593 for the documented input delta, because a
// two-ULP change flipped a boolean branch. The curvature-limited formulas must
// stay within 64 eps of the value scale for the same, the reversed and the
// rescaled witnesses, and for every single-entry two-ULP perturbation.
void witness_continuity()
{
    const double a = 1.0 / 32.0;
    const double forward[6] = {1.0 - 2.5 * a, 1.0 - a, 1.0, 1.0 - a, 1.0 - 4.0 * a, 1.0 - 9.0 * a};
    double reversed[6];
    for (int k = 0; k < 6; ++k) reversed[k] = forward[5 - k];

    const double* witnesses[2] = {forward, reversed};
    for (const double* witness : witnesses) {
        const auto [base_left, base_right] = production(witness);
        const double band = kCurvatureBand * kEps * stencil_scale(witness);
        check_matches_reference(witness, "witness stencil reference");

        // Documented witness: two nextafter steps of values[0] toward 1.
        double perturbed[6];
        for (int k = 0; k < 6; ++k) perturbed[k] = witness[k];
        perturbed[0] = std::nextafter(std::nextafter(perturbed[0], 1.0), 1.0);
        require(perturbed[0] - witness[0] == 2.2204460492503131e-16,
            "witness perturbation is not the documented two-ULP delta");
        const auto [left, right] = production(perturbed);
        require(std::abs(left - base_left) <= band, "witness left face jump exceeds 64 eps");
        require(std::abs(right - base_right) <= band, "witness right face jump exceeds 64 eps");
        // The retired bypass jump was 0.0052083333333332593 of the value scale.
        require(std::abs(left - base_left) < 1e-3 * stencil_scale(witness),
            "witness left face reproduced the retired bypass jump");

        // Every stencil entry, two nextafter steps toward 1 and toward 0.
        for (int entry = 0; entry < 6; ++entry) {
            for (double target : {1.0, 0.0}) {
                double single[6];
                for (int k = 0; k < 6; ++k) single[k] = witness[k];
                single[entry] = std::nextafter(std::nextafter(single[entry], target), target);
                const auto [left_one, right_one] = production(single);
                require(std::abs(left_one - base_left) <= band, "two-ULP entry scan left jump");
                require(std::abs(right_one - base_right) <= band, "two-ULP entry scan right jump");
            }
        }

        // Reversed/scaled witnesses: 1e-100 and 1e100 keep the relative shape.
        for (double factor : {1e-100, 1e100}) {
            double scaled[6];
            double scaled_perturbed[6];
            for (int k = 0; k < 6; ++k) {
                scaled[k] = witness[k] * factor;
                scaled_perturbed[k] = scaled[k];
            }
            scaled_perturbed[0] =
                std::nextafter(std::nextafter(scaled_perturbed[0], 1.0), 1.0);
            const auto [scaled_left, scaled_right] = production(scaled);
            const auto [scaled_left_one, scaled_right_one] = production(scaled_perturbed);
            const double scaled_band = kCurvatureBand * kEps * stencil_scale(scaled);
            require(std::abs(scaled_left_one - scaled_left) <= scaled_band,
                "scaled witness left face jump exceeds 64 eps");
            require(std::abs(scaled_right_one - scaled_right) <= scaled_band,
                "scaled witness right face jump exceeds 64 eps");
            // Scale invariance of the face formulas: faces scale with the data.
            close_relative(scaled_left / factor, base_left, stencil_scale(witness),
                "scaled witness left face not scale invariant");
            close_relative(scaled_right / factor, base_right, stencil_scale(witness),
                "scaled witness right face not scale invariant");
        }
    }
}

// These nonsmooth controls must retain finite faces inside their stencil range.
// This is not a bound on all smooth profiles: a quadratic's true extremum face
// can lie outside the range of its cell averages.
void discontinuity_controls()
{
    const double controls[][6] = {
        {1.0, 1.0, 1.0, 2.0, 2.0, 2.0},
        {2.0, 2.0, 2.0, 1.0, 1.0, 1.0},
        {1.0, 1.0, 1.0, 1.5, 1.0, 1.0},
        {1.0, 2.0, 1.0, 2.0, 1.0, 2.0},
        {0.0, 0.0, 0.0, 1e-300, 1e-300, 1e-300},
        {-1e300, 1e300, -1e300, 1e300, -1e300, 1e300},
        {1e-300, 2e-300, 3e-300, 4e-300, 5e-300, 6e-300},
    };
    for (const auto& v : controls) {
        const auto [left, right] = production(v);
        const double low = *std::min_element(std::begin(v), std::end(v));
        const double high = *std::max_element(std::begin(v), std::end(v));
        require(left >= low && left <= high, "left face left the stencil range");
        require(right >= low && right <= high, "right face left the stencil range");
        check_matches_reference(v, "control stencil reference");
    }
}

// Deterministic stencil sweep: every six-entry combination of small integer
// levels, then the same levels offset by an affine ramp and by a fixed
// fractional step. The reference comparison is what pins the shipped method to
// the frozen coefficient C = 1.25 and to the frozen extremum/CW branch choice,
// neither of which the analytic checks alone can separate.
void reference_sweep()
{
    for (int code = 0; code < 15625; ++code) {
        double v[6];
        int digits = code;
        for (int k = 0; k < 6; ++k) {
            v[k] = static_cast<double>((digits % 5) - 2);
            digits /= 5;
        }
        check_matches_reference(v, "integer stencil sweep");
    }
    for (int code = 0; code < 15625; ++code) {
        double v[6];
        int digits = code;
        for (int k = 0; k < 6; ++k) {
            v[k] = 0.375 * static_cast<double>((digits % 5) - 2)
                 + 0.125 * static_cast<double>(k) - 0.0625;
            digits /= 5;
        }
        check_matches_reference(v, "ramped stencil sweep");
        if (code % 977 == 0) {
            for (double factor : {1e-100, 1e100}) {
                double scaled[6];
                for (int k = 0; k < 6; ++k) scaled[k] = v[k] * factor;
                check_matches_reference(scaled, "scaled stencil sweep");
            }
        }
    }
}

// Manager-reproduced positive witnesses plus their sign-reversed controls and
// scales. Every stencil entry is perturbed by one and two ULP steps in both
// directions; the continuous projection/blend must keep each face within
// 64 eps of the stencil scale for every perturbation, and the reference must
// still reproduce the perturbed value.
void perturbation_witness(const double* v, const char* label)
{
    const auto [base_left, base_right] = production(v);
    check_matches_reference(v, label);
    const double band = kCurvatureBand * kEps * stencil_scale(v);

    for (int entry = 0; entry < 6; ++entry) {
        for (double target : {std::numeric_limits<double>::infinity(),
                              -std::numeric_limits<double>::infinity()}) {
            double perturbed[6];
            for (int k = 0; k < 6; ++k) perturbed[k] = v[k];
            for (int step = 0; step < 2; ++step) {
                perturbed[entry] = std::nextafter(perturbed[entry], target);
                const auto [left, right] = production(perturbed);
                require(std::abs(left - base_left) <= band,
                    "ULP-perturbed witness left face jump exceeds 64 eps");
                require(std::abs(right - base_right) <= band,
                    "ULP-perturbed witness right face jump exceeds 64 eps");
                check_matches_reference(perturbed, "ULP-perturbed witness reference");
            }
        }
    }
}

void perturbation_witnesses()
{
    const double witnesses[][6] = {
        {0.7, 0.5, 1.0, 1.2, 0.5, 0.9},
        {0.25, 0.25, 0.25, 4.0, 4.0, 0.25},
    };
    for (const auto& v : witnesses) {
        perturbation_witness(v, "positive witness");

        double reversed[6];
        for (int k = 0; k < 6; ++k) reversed[k] = -v[k];
        perturbation_witness(reversed, "sign-reversed witness");

        for (double factor : {1e-100, 1e100}) {
            double scaled[6];
            for (int k = 0; k < 6; ++k) scaled[k] = v[k] * factor;
            perturbation_witness(scaled, "scaled witness");
        }
    }
}

// Bounded deterministic perturbation sweep over small integer stencils: one ULP
// in each direction for every entry must stay inside the same 64 eps band. The
// analytic and reference checks above pin the formulas; this sweep exercises the
// continuous branch joins on a dense deterministic family.
void perturbation_sweep()
{
    for (int code = 0; code < 1296; ++code) { // 6^4 small stencils
        double v[6];
        int digits = code;
        for (int k = 0; k < 6; ++k) {
            v[k] = static_cast<double>((digits % 6) - 1);
            digits /= 6;
        }
        const auto [base_left, base_right] = production(v);
        const double band = kCurvatureBand * kEps * stencil_scale(v);
        for (int entry = 0; entry < 6; ++entry) {
            for (double target : {1e300, -1e300}) {
                double perturbed[6];
                for (int k = 0; k < 6; ++k) perturbed[k] = v[k];
                perturbed[entry] = std::nextafter(perturbed[entry], target);
                const auto [left, right] = production(perturbed);
                require(std::abs(left - base_left) <= band,
                    "sweep-perturbed left face jump exceeds 64 eps");
                require(std::abs(right - base_right) <= band,
                    "sweep-perturbed right face jump exceeds 64 eps");
            }
        }
    }
}

} // namespace

int main()
{
    try {
        constants();
        affine_fields();
        exact_quadratics();
        witness_continuity();
        discontinuity_controls();
        reference_sweep();
        perturbation_witnesses();
        perturbation_sweep();
        std::cout << "PPM curvature limiter checks passed (C=1.25, 64 eps witness band)\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
